#ifndef CONTAINERCP_STORAGE_CONNECTION_POOL_H
#define CONTAINERCP_STORAGE_CONNECTION_POOL_H

#include "SQLiteWrapper.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace containercp::storage {

// Bounded connection pool for the Storage subsystem.
//
// Architecture:
//   - One serialized write connection protected by a mutex.
//   - Exactly kReadPoolSize (3) read connections for concurrent reads.
//
// All connections use the same database file and identical PRAGMA settings.
// Connections are created during initialize() and destroyed during shutdown().
//
// Ownership:
//   - The pool owns all connections. Callers lease read connections
//     temporarily and must return them.
//   - The write connection is accessed under a mutex — callers lock,
//     use, unlock.
//
// Thread-safety:
//   - Write mutex serializes all write operations.
//   - Read pool uses atomic round-robin for lease assignment.
//   - Read connections do not block each other.
//   - A read may block on write during WAL checkpoint (handled by
//     busy_timeout).
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

    // === Write connection ===

    // Returns reference to the serialized write connection.
    // Caller must hold the write mutex via lock_write()/unlock_write().
    SQLiteDB& write_connection();
    void lock_write();
    void unlock_write();

    // === Read connections ===

    // Lease a read connection.  Blocks if all 3 are in use.
    // Returns nullptr only on catastrophic error.
    // Caller must return the connection via return_read().
    SQLiteDB* lease_read();

    // Return a leased read connection to the pool.
    void return_read(SQLiteDB* db);

    // === Lifecycle ===

    // Close all connections.  Safe to call multiple times.
    // Outstanding leases are force-closed (log warning).
    void shutdown();

    // Create a consistent snapshot using the SQLite Online Backup API.
    // The write mutex is locked for the duration.
    bool backup(const std::string& dest_path);

private:
    // Open a single connection and apply PRAGMAs.
    bool open_connection(SQLiteDB& conn, const std::string& path);

    std::unique_ptr<SQLiteDB> write_conn_;
    std::mutex write_mutex_;
    std::array<std::unique_ptr<SQLiteDB>, kReadPoolSize> read_conns_;
    std::atomic<int> read_next_{0};
    std::array<std::atomic<bool>, kReadPoolSize> read_in_use_ = {};
    std::string db_path_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_CONNECTION_POOL_H
