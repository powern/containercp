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
//   Construction: lock write mutex, check shutdown state,
//     try_write_connection(), BEGIN IMMEDIATE.
//     If all succeed → is_active() = true, db_ is stable.
//   Active: perform writes through db().
//   commit(): COMMIT, mark committed.
//   Destruction: if active and not committed → ROLLBACK.
//     Release write mutex.
//
// Key rules:
//   - Rollback by default on destruction.
//   - Explicit commit() required for persistence.
//   - db_ is stored once during construction — no repeated lookup.
//   - Shutdown cannot destroy write_conn_ while this guard is
//     active because shutdown() waits for write_mutex_.
class TransactionGuard {
public:
    explicit TransactionGuard(ConnectionPool& pool);
    ~TransactionGuard();

    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;

    bool is_active() const;
    bool commit();

    // Access the transaction-scoped write connection.
    // Valid only when is_active() returns true.
    SQLiteDB& db() const;

private:
    ConnectionPool& pool_;
    SQLiteDB* db_ = nullptr;
    bool active_ = false;
    bool committed_ = false;
};

// SQLite-backed storage for a subset of resource types.
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
