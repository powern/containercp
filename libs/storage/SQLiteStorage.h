#ifndef CONTAINERCP_STORAGE_SQLITE_STORAGE_H
#define CONTAINERCP_STORAGE_SQLITE_STORAGE_H

#include "access/AccessGrant.h"
#include "access/AccessUser.h"
#include "ConnectionPool.h"
#include "database/Database.h"
#include "domain/Domain.h"
#include "node/Node.h"
#include "php/PhpVersion.h"
#include "profile/Profile.h"
#include "proxy/ReverseProxy.h"
#include "site/Site.h"
#include "user/User.h"

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

    // Users
    void save_users(const std::vector<user::User>& users);
    std::vector<user::User> load_users();

    // Sites
    void save_sites(const std::vector<site::Site>& sites);
    std::vector<site::Site> load_sites();

    // Domains
    void save_domains(const std::vector<domain::Domain>& domains);
    std::vector<domain::Domain> load_domains();

    // Databases
    void save_databases(const std::vector<database::Database>& databases);
    std::vector<database::Database> load_databases();

    // Reverse proxies
    void save_reverse_proxies(const std::vector<proxy::ReverseProxy>& proxies);
    std::vector<proxy::ReverseProxy> load_reverse_proxies();

    // Access users (FK-safe parent sync)
    void save_access_users(const std::vector<access::AccessUser>& users);
    std::vector<access::AccessUser> load_access_users();

    // Access grants (child table, FK-dependent)
    void save_access_grants(const std::vector<access::AccessGrant>& grants);
    std::vector<access::AccessGrant> load_access_grants();

private:
    ConnectionPool& pool_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_SQLITE_STORAGE_H
