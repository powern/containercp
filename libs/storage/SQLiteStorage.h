#ifndef CONTAINERCP_STORAGE_SQLITE_STORAGE_H
#define CONTAINERCP_STORAGE_SQLITE_STORAGE_H

#include "ConnectionPool.h"
#include "node/Node.h"
#include "php/PhpVersion.h"
#include "profile/Profile.h"

#include <string>
#include <vector>

namespace containercp::storage {

// RAII transaction guard with fail-closed semantics.
//
// Lifecycle:
//   1. Construction: acquires write lock, executes BEGIN IMMEDIATE.
//      Check is_active() before proceeding.
//   2. If active: perform writes.  Every bind/prepare/step return
//      value must be checked — on failure the guard is marked for
//      rollback via suppress_commit().
//   3. Destruction: if active and not committed → ROLLBACK.
//      If active and committed → no-op.
//      If never activated → only releases the lock.
//
// Key rules:
//   - NEVER auto-commits.  Always rollback by default.
//   - Explicit commit() required for persistence.
//   - suppress_commit() marks for rollback on destruction.
//   - is_active() returns false if BEGIN IMMEDIATE failed.
//   - After commit(), the guard is inactive.
class TransactionGuard {
public:
    explicit TransactionGuard(ConnectionPool& pool);
    ~TransactionGuard();

    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;

    // Returns true if BEGIN IMMEDIATE succeeded and a transaction
    // is active.  No writes should proceed if this returns false.
    bool is_active() const;

    // Mark the transaction for rollback on destruction (call when
    // a write operation fails and you need to abort).
    void suppress_commit();

    // Commit explicitly.  Returns true on success.
    // On failure, marks for rollback and returns false.
    // Safe to call multiple times (idempotent after success).
    bool commit();

private:
    ConnectionPool& pool_;
    bool active_ = false;
    bool committed_ = false;
};

// SQLite-backed storage for a subset of resource types.
//
// Used internally by Storage in explicit SQLite mode
// (CoreStorageBackend::SqlitePhase5).  Not active in default TXT mode.
//
// Uses ConnectionPool (write via WriteGuard + TransactionGuard,
// reads via ReadLease).  No direct SQLite C API calls.
class SQLiteStorage {
public:
    explicit SQLiteStorage(ConnectionPool& pool);

    void save_nodes(const std::vector<node::Node>& nodes);
    std::vector<node::Node> load_nodes();

    void save_php_versions(const std::vector<php::PhpVersion>& versions);
    std::vector<php::PhpVersion> load_php_versions();

    void save_profiles(const std::vector<profile::Profile>& profiles);
    std::vector<profile::Profile> load_profiles();

private:
    ConnectionPool& pool_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_SQLITE_STORAGE_H
