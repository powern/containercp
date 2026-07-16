#ifndef CONTAINERCP_STORAGE_SQLITE_STORAGE_H
#define CONTAINERCP_STORAGE_SQLITE_STORAGE_H

#include "ConnectionPool.h"
#include "node/Node.h"
#include "php/PhpVersion.h"
#include "profile/Profile.h"

#include <string>
#include <vector>

namespace containercp::storage {

// RAII transaction guard for the serialized write connection.
// Begins a transaction on construction, commits on destruction
// if no error occurred, rolls back on destruction if an error
// was set via suppress_commit().
//
// Usage:
//   {
//       TransactionGuard txn(pool);
//       // ... write operations ...
//       txn.commit();  // optional — auto-commits on destruction
//   }
class TransactionGuard {
public:
    explicit TransactionGuard(ConnectionPool& pool);
    ~TransactionGuard();

    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;

    // Mark the transaction for rollback on destruction.
    void suppress_commit();

    // Commit explicitly.  Safe to call multiple times.
    bool commit();

private:
    ConnectionPool& pool_;
    bool committed_ = false;
    bool suppress_ = false;
};

// SQLite-backed storage for a subset of resource types.
//
// Used internally by Storage to delegate nodes, PHP versions,
// and profiles to SQLite while other types remain TXT-backed.
//
// Uses ConnectionPool (write via WriteGuard + TransactionGuard,
// reads via ReadLease).  No direct SQLite C API calls.
class SQLiteStorage {
public:
    explicit SQLiteStorage(ConnectionPool& pool);

    // Nodes
    void save_nodes(const std::vector<node::Node>& nodes);
    std::vector<node::Node> load_nodes();

    // PHP versions
    void save_php_versions(const std::vector<php::PhpVersion>& versions);
    std::vector<php::PhpVersion> load_php_versions();

    // Profiles
    void save_profiles(const std::vector<profile::Profile>& profiles);
    std::vector<profile::Profile> load_profiles();

private:
    ConnectionPool& pool_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_SQLITE_STORAGE_H
