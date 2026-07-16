#include "ConnectionPool.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace containercp::storage {

// ============================================================
// WriteGuard
// ============================================================

WriteGuard::WriteGuard(ConnectionPool& pool)
    : pool_(pool) {
    pool_.lock_write();
}

WriteGuard::~WriteGuard() {
    pool_.unlock_write();
}

SQLiteDB& WriteGuard::db() {
    return pool_.write_connection();
}

// ============================================================
// ConnectionPool
// ============================================================

ConnectionPool::~ConnectionPool() {
    shutdown();
}

bool ConnectionPool::initialize(const std::string& db_path) {
    db_path_ = db_path;
    shutdown_.store(false);

    auto write_conn = std::make_unique<SQLiteDB>();
    if (!open_connection(*write_conn, db_path)) {
        return false;
    }
    write_conn_ = std::move(write_conn);

    for (int i = 0; i < kReadPoolSize; ++i) {
        auto conn = std::make_unique<SQLiteDB>();
        if (!open_connection(*conn, db_path)) {
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
    return conn.open(path);
}

SQLiteDB& ConnectionPool::write_connection() {
    return *write_conn_;
}

SQLiteDB* ConnectionPool::try_write_connection() {
    if (write_conn_) return write_conn_.get();
    return nullptr;
}

void ConnectionPool::lock_write() {
    write_mutex_.lock();
}

void ConnectionPool::unlock_write() {
    write_mutex_.unlock();
}

SQLiteDB* ConnectionPool::lease_read() {
    // Increment outstanding count FIRST, before checking shutdown.
    // This ensures shutdown() can never see zero leases while a new
    // lease is being acquired.  If we detect shutdown after incrementing,
    // we decrement and return nullptr.
    outstanding_leases_.fetch_add(1);

    if (shutdown_.load()) {
        outstanding_leases_.fetch_sub(1);
        return nullptr;
    }

    // Round-robin with compare-exchange
    for (int attempt = 0; attempt < kReadPoolSize * 2; ++attempt) {
        int idx = read_next_.fetch_add(1) % kReadPoolSize;
        bool expected = false;
        if (read_in_use_[idx].compare_exchange_strong(expected, true)) {
            return read_conns_[idx].get();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // Linear scan fallback
    for (int i = 0; i < kReadPoolSize * 50; ++i) {
        if (shutdown_.load()) {
            outstanding_leases_.fetch_sub(1);
            return nullptr;
        }
        for (int idx = 0; idx < kReadPoolSize; ++idx) {
            bool expected = false;
            if (read_in_use_[idx].compare_exchange_strong(expected, true)) {
                return read_conns_[idx].get();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    outstanding_leases_.fetch_sub(1);
    return nullptr;
}

// ============================================================
// ReadLease
// ============================================================

ReadLease::ReadLease(ConnectionPool& pool)
    : pool_(pool)
    , db_(pool_.lease_read()) {
}

ReadLease::~ReadLease() {
    if (db_) {
        pool_.return_read(db_);
    }
}

ReadLease::ReadLease(ReadLease&& other) noexcept
    : pool_(other.pool_)
    , db_(other.db_) {
    other.db_ = nullptr;
}

SQLiteDB& ReadLease::db() const {
    return *db_;
}

SQLiteDB* ReadLease::operator->() const {
    return db_;
}

bool ReadLease::is_valid() const {
    return db_ != nullptr;
}

void ConnectionPool::return_read(SQLiteDB* db) {
    if (!db) return;
    for (int i = 0; i < kReadPoolSize; ++i) {
        if (read_conns_[i].get() == db) {
            read_in_use_[i].store(false);
            outstanding_leases_.fetch_sub(1);
            return;
        }
    }
}

void ConnectionPool::shutdown() {
    shutdown_.store(true);

    // Wait for ALL outstanding leases to be returned.
    // Never destroy connections while a caller holds a lease.
    // Deterministic shutdown is more important than fast shutdown.
    while (outstanding_leases_.load() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // All leases are returned. Safe to close connections now.
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

bool ConnectionPool::is_shutdown() const {
    return shutdown_.load();
}

bool ConnectionPool::backup(const std::string& dest_path) {
    WriteGuard wg(*this);
    SQLiteDB& src = wg.db();

    sqlite3* dest_db = nullptr;
    int rc = sqlite3_open(dest_path.c_str(), &dest_db);
    if (rc != SQLITE_OK) {
        return false;
    }

    sqlite3_backup* backup = sqlite3_backup_init(dest_db, "main",
                                                  src.handle(), "main");
    if (!backup) {
        sqlite3_close(dest_db);
        return false;
    }

    do {
        rc = sqlite3_backup_step(backup, 5);
    } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);

    bool success = (rc == SQLITE_DONE);
    sqlite3_backup_finish(backup);
    sqlite3_close(dest_db);
    return success;
}

} // namespace containercp::storage
