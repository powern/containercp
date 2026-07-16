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
//   - Read pool uses atomic compare-exchange for lease assignment.
//   - Read connections do not block each other.
//   - A read may block on write during WAL checkpoint (handled
//     by busy_timeout).
class ConnectionPool {
public:
    static constexpr int kReadPoolSize = 3;

    ConnectionPool() = default;
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // Initialize: open the database, create write + read connections.
    // Must be called once before any other method.
    bool initialize(const std::string& db_path);

    // === Write connection (use via WriteGuard) ===

    // Returns reference to the serialized write connection.
    // Caller must hold the write mutex (use WriteGuard).
    SQLiteDB& write_connection();

    // === Read connections ===

    // Lease a read connection.  Blocks if all 3 are in use.
    // Returns nullptr only on catastrophic error or if pool is shut down.
    // Caller must return via return_read().
    SQLiteDB* lease_read();

    // Return a leased read connection.  Safe to call with nullptr.
    void return_read(SQLiteDB* db);

    // === Lifecycle ===

    // Close all connections.  Waits indefinitely for all outstanding
    // leases to be returned.  Connections are never destroyed while
    // a lease is active.  After shutdown, lease_read() returns nullptr.
    void shutdown();

    // Returns true if shutdown() has been called.
    bool is_shutdown() const;

    // Create a consistent snapshot using the SQLite Online Backup API.
    // The write mutex is locked for the duration.
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
