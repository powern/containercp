#include "ConnectionPool.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace containercp::storage {

ConnectionPool::~ConnectionPool() {
    shutdown();
}

bool ConnectionPool::initialize(const std::string& db_path) {
    db_path_ = db_path;

    // Write connection
    auto write_conn = std::make_unique<SQLiteDB>();
    if (!open_connection(*write_conn, db_path)) {
        return false;
    }
    write_conn_ = std::move(write_conn);

    // Read connections
    for (int i = 0; i < kReadPoolSize; ++i) {
        auto conn = std::make_unique<SQLiteDB>();
        if (!open_connection(*conn, db_path)) {
            // Close already-opened read connections
            for (int j = 0; j < i; ++j) {
                read_conns_[j]->close();
                read_conns_[j].reset();
            }
            write_conn_->close();
            write_conn_.reset();
            return false;
        }
        read_conns_[i] = std::move(conn);
    }

    return true;
}

bool ConnectionPool::open_connection(SQLiteDB& conn, const std::string& path) {
    if (!conn.open(path)) return false;
    // PRAGMAs are applied by SQLiteDB::open() via apply_pragmas()
    return true;
}

SQLiteDB& ConnectionPool::write_connection() {
    return *write_conn_;
}

void ConnectionPool::lock_write() {
    write_mutex_.lock();
}

void ConnectionPool::unlock_write() {
    write_mutex_.unlock();
}

SQLiteDB* ConnectionPool::lease_read() {
    // Round-robin: try each connection up to kReadPoolSize times.
    // If all are busy, wait and retry (with timeout via busy_timeout
    // on the connection itself, not on the lease).
    for (int attempt = 0; attempt < kReadPoolSize * 2; ++attempt) {
        int idx = read_next_.fetch_add(1) % kReadPoolSize;
        bool expected = false;
        if (read_in_use_[idx].compare_exchange_strong(expected, true)) {
            return read_conns_[idx].get();
        }
        // Brief pause before retry to avoid tight spinning
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    // Fallback: linear scan with blocking wait
    for (int i = 0; i < kReadPoolSize * 50; ++i) {
        for (int idx = 0; idx < kReadPoolSize; ++idx) {
            bool expected = false;
            if (read_in_use_[idx].compare_exchange_strong(expected, true)) {
                return read_conns_[idx].get();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return nullptr;  // all connections busy (should not happen with busy_timeout)
}

void ConnectionPool::return_read(SQLiteDB* db) {
    for (int i = 0; i < kReadPoolSize; ++i) {
        if (read_conns_[i].get() == db) {
            read_in_use_[i].store(false);
            return;
        }
    }
    // db not found in pool — ignore (may happen after shutdown)
}

void ConnectionPool::shutdown() {
    // Mark all read connections as available (force-release)
    for (int i = 0; i < kReadPoolSize; ++i) {
        read_in_use_[i].store(false);
    }

    if (write_conn_) {
        write_conn_->close();
        write_conn_.reset();
    }

    for (int i = 0; i < kReadPoolSize; ++i) {
        if (read_conns_[i]) {
            read_conns_[i]->close();
            read_conns_[i].reset();
        }
    }
}

bool ConnectionPool::backup(const std::string& dest_path) {
    // Lock write mutex to ensure consistent snapshot
    lock_write();
    // Use the write connection for backup since it serializes all mutations
    SQLiteDB& src = *write_conn_;

    // Open destination database
    sqlite3* dest_db = nullptr;
    int rc = sqlite3_open(dest_path.c_str(), &dest_db);
    if (rc != SQLITE_OK) {
        unlock_write();
        return false;
    }

    // Online Backup API
    sqlite3_backup* backup = sqlite3_backup_init(dest_db, "main",
                                                  src.handle(), "main");
    if (!backup) {
        sqlite3_close(dest_db);
        unlock_write();
        return false;
    }

    // Copy all pages (5 pages per step to yield periodically)
    do {
        rc = sqlite3_backup_step(backup, 5);
    } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);

    bool success = (rc == SQLITE_DONE);
    sqlite3_backup_finish(backup);
    sqlite3_close(dest_db);
    unlock_write();
    return success;
}

} // namespace containercp::storage
