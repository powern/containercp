#ifndef CONTAINERCP_STORAGE_CONNECTION_POOL_H
#define CONTAINERCP_STORAGE_CONNECTION_POOL_H

#include "SQLiteWrapper.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace containercp::storage {

class ConnectionPool;

// RAII write guard.  Locks the write mutex on construction, unlocks
// on destruction.  Provides access to the serialized write connection.
//
// Usage:
//   {
//       WriteGuard wg(pool);
//       wg.db().exec("INSERT ...");
//   }  // mutex released automatically
class WriteGuard {
public:
    explicit WriteGuard(ConnectionPool& pool);
    ~WriteGuard();

    WriteGuard(const WriteGuard&) = delete;
    WriteGuard& operator=(const WriteGuard&) = delete;

    SQLiteDB& db();

private:
    ConnectionPool& pool_;
};

// RAII read lease.  Acquires a read connection on construction,
// returns it on destruction.  Move-constructible (transfers the
// connection to the new lease), but NOT move-assignable (prevents
// cross-pool ownership bugs since the pool reference cannot be
// reseated).
//
// Usage:
//   {
//       ReadLease rl(pool);
//       if (rl.is_valid()) {
//           rl->exec("SELECT ...");
//       }
//   }  // connection returned automatically on destruction
class ReadLease {
public:
    explicit ReadLease(ConnectionPool& pool);
    ~ReadLease();

    ReadLease(const ReadLease&) = delete;
    ReadLease& operator=(const ReadLease&) = delete;

    ReadLease(ReadLease&& other) noexcept;
    ReadLease& operator=(ReadLease&& other) noexcept = delete;

    SQLiteDB& db() const;
    SQLiteDB* operator->() const;
    bool is_valid() const;

private:
    ConnectionPool& pool_;
    SQLiteDB* db_ = nullptr;
};

// Bounded connection pool for the Storage subsystem.
//
// Architecture:
//   - One serialized write connection protected by a std::mutex.
//   - Exactly kReadPoolSize (3) read connections for concurrent reads.
//
// Ownership:
//   - The pool owns all connections.
//   - Callers lease read connections temporarily and must return them.
//   - The write connection is accessed exclusively through WriteGuard.
//
// Lease lifetime:
//   - A read lease is valid until return_read() is called.
//   - shutdown() waits indefinitely for all outstanding leases to
//     be returned.  Connections are never destroyed while leased.
//   - After shutdown(), lease_read() returns nullptr.
//
// Thread-safety:
//   - Write mutex serializes all write operations.
//   - Lease acquisition increments the outstanding count BEFORE
//     checking shutdown state, ensuring shutdown can never complete
//     while a lease is being acquired.
//   - Read pool uses atomic compare-exchange for lease assignment.
//   - Read connections do not block each other.
class ConnectionPool {
public:
    static constexpr int kReadPoolSize = 3;

    ConnectionPool() = default;
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    bool initialize(const std::string& db_path);

    // === Write connection (use via WriteGuard) ===
    SQLiteDB& write_connection();

    // === Read connections ===
    SQLiteDB* lease_read();
    void return_read(SQLiteDB* db);

    // === Lifecycle ===
    void shutdown();

    bool is_shutdown() const;

    bool backup(const std::string& dest_path);

private:
    friend class WriteGuard;

    void lock_write();
    void unlock_write();

    bool open_connection(SQLiteDB& conn, const std::string& path);

    std::unique_ptr<SQLiteDB> write_conn_;
    std::mutex write_mutex_;
    std::array<std::unique_ptr<SQLiteDB>, kReadPoolSize> read_conns_;
    std::atomic<int> read_next_{0};
    std::array<std::atomic<bool>, kReadPoolSize> read_in_use_ = {};
    std::string db_path_;
    std::atomic<bool> shutdown_{false};
    std::atomic<int> outstanding_leases_{0};
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_CONNECTION_POOL_H
