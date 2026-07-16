#ifndef CONTAINERCP_STORAGE_CONNECTION_POOL_H
#define CONTAINERCP_STORAGE_CONNECTION_POOL_H

#include "SQLiteWrapper.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace containercp::storage {

class ConnectionPool;

// RAII write guard.  Locks the write mutex on construction, unlocks
// on destruction.  After shutdown has started, is_valid() returns
// false and db() must not be used.
//
// Usage:
//   {
//       WriteGuard wg(pool);
//       if (wg.is_valid()) {
//           wg.db().exec("INSERT ...");
//       }
//   }
class WriteGuard {
public:
    explicit WriteGuard(ConnectionPool& pool);
    ~WriteGuard();

    WriteGuard(const WriteGuard&) = delete;
    WriteGuard& operator=(const WriteGuard&) = delete;

    // Returns true if the lock was acquired and shutdown had not
    // yet started.  If false, no write operation may proceed.
    bool is_valid() const;

    // Access the write connection.  Only valid when is_valid()
    // returns true.
    SQLiteDB& db();

private:
    ConnectionPool& pool_;
    SQLiteDB* db_ = nullptr;
    bool locked_ = false;
};

// RAII read lease.  Acquires a read connection on construction,
// returns it on destruction.  Move-constructible only.
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

class TransactionGuard;

// Bounded connection pool.
//
// Write connection lifecycle (lock order):
//   1. shutdown_ flag is set atomically (prevent new acquisitions).
//   2. Read lease count is drained (existing leases complete).
//   3. write_mutex_ is locked (waits for active WriteGuard,
//      TransactionGuard, or backup).
//   4. write_conn_ is closed and reset.
//   5. write_mutex_ is unlocked.
//   6. Read connections are closed.
//
// Any WriteGuard or TransactionGuard that acquires write_mutex_ after
// shutdown_ is set will detect the flag, release the mutex, and become
// inactive.  This guarantees: once shutdown() acquires write_mutex_,
// no new write operation can interleave.
class ConnectionPool {
public:
    static constexpr int kReadPoolSize = 3;

    ConnectionPool() = default;
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    bool initialize(const std::string& db_path);

    // === Write connection ===
    // write_connection() requires an active WriteGuard or
    // TransactionGuard to be valid.  Undefined behaviour if
    // called without one.
    SQLiteDB& write_connection();

    // === Read connections ===
    SQLiteDB* lease_read();
    void return_read(SQLiteDB* db);

    // === Lifecycle ===
    void shutdown();

    bool is_shutdown() const;

    bool backup(const std::string& dest_path);

    // Test-only observer for shutdown synchronization.
    // In production all callbacks are empty — no overhead.
    struct TestObserver {
        std::function<void()> on_shutdown_awaiting_write_mutex;
    };
    TestObserver test_obs_;

private:
    friend class WriteGuard;
    friend class TransactionGuard;

    void lock_write();
    void unlock_write();

    // Returns nullptr if write_conn_ is null (uninitialized,
    // init failed, or shut down).  Requires write_mutex_ to be
    // held by the caller (ensures pointer stability).
    SQLiteDB* try_write_connection();

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
