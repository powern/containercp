#include "SQLiteStorage.h"
#include "mail/MailDomain.h"
#include "mail/MailModuleState.h"
#include "profile/ProfileType.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace containercp::storage {

// ============================================================
// TransactionGuard
// ============================================================

TransactionGuard::TransactionGuard(ConnectionPool& pool)
    : pool_(pool) {
    pool_.lock_write();
    // Check shutdown state while holding the mutex.
    if (pool_.is_shutdown()) {
        pool_.unlock_write();
        return;
    }
    db_ = pool_.try_write_connection();
    if (db_ && db_->is_open() && db_->exec("BEGIN IMMEDIATE")) {
        active_ = true;
    }
    if (!active_) {
        pool_.unlock_write();
    }
}

TransactionGuard::~TransactionGuard() {
    if (!active_) return;  // never activated — lock already released
    if (!committed_) {
        db_->exec("ROLLBACK");
    }
    pool_.unlock_write();
}

bool TransactionGuard::is_active() const {
    return active_;
}

bool TransactionGuard::commit() {
    if (!active_ || committed_) return committed_;
    if (db_->exec("COMMIT")) {
        committed_ = true;
        return true;
    }
    return false;
}

SQLiteDB& TransactionGuard::db() const {
    return *db_;
}

// ============================================================
// SQLiteStorage
// ============================================================

SQLiteStorage::SQLiteStorage(ConnectionPool& pool)
    : pool_(pool) {
}

// --- helper: execute a complete-vector-replacement save ---
static bool replace_all(
    ConnectionPool& pool,
    const std::string& delete_sql,
    const std::function<bool(SQLiteDB& db)>& bind_each)
{
    TransactionGuard txn(pool);
    if (!txn.is_active()) return false;

    // The guard holds a stable db_ pointer — use txn.db() for writes.
    if (!txn.db().exec(delete_sql)) return false;

    if (!bind_each(txn.db())) return false;

    return txn.commit();
}

// --- Nodes ---

bool SQLiteStorage::try_save_nodes(const std::vector<node::Node>& nodes) {
    return replace_all(pool_, "DELETE FROM nodes",
        [&](SQLiteDB& db) -> bool {
            for (const auto& n : nodes) {
                if (!db.prepare(
                        "INSERT INTO nodes (id, name, type, created_at, updated_at) "
                        "VALUES (?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                        "strftime('%Y-%m-%dT%H:%M:%SZ','now'))"))
                    return false;
                if (!db.bind_int(1, static_cast<int64_t>(n.id))) return false;
                if (!db.bind_text(2, n.name)) return false;
                if (!db.bind_text(3, n.type)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_nodes(const std::vector<node::Node>& nodes) {
    (void)try_save_nodes(nodes);
}

std::vector<node::Node> SQLiteStorage::load_nodes() {
    std::vector<node::Node> nodes;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return nodes;
    if (!rl->prepare("SELECT id, name, type FROM nodes ORDER BY id")) return nodes;
    while (rl->step()) {
        node::Node n;
        n.id = static_cast<uint64_t>(rl->column_int(0));
        n.name = rl->column_text(1);
        n.type = rl->column_text(2);
        nodes.push_back(std::move(n));
    }
    return nodes;
}

// --- PHP versions ---

bool SQLiteStorage::try_save_php_versions(const std::vector<php::PhpVersion>& versions) {
    return replace_all(pool_, "DELETE FROM php_versions",
        [&](SQLiteDB& db) -> bool {
            for (const auto& pv : versions) {
                if (!db.prepare(
                        "INSERT INTO php_versions (id, version, image, enabled, "
                        "default_version, created_at, updated_at) VALUES "
                        "(?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                        "strftime('%Y-%m-%dT%H:%M:%SZ','now'))"))
                    return false;
                if (!db.bind_int(1, static_cast<int64_t>(pv.id))) return false;
                if (!db.bind_text(2, pv.version)) return false;
                if (!db.bind_text(3, pv.image)) return false;
                if (!db.bind_int(4, pv.enabled ? 1 : 0)) return false;
                if (!db.bind_int(5, pv.default_version ? 1 : 0)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_php_versions(const std::vector<php::PhpVersion>& versions) {
    (void)try_save_php_versions(versions);
}

std::vector<php::PhpVersion> SQLiteStorage::load_php_versions() {
    std::vector<php::PhpVersion> versions;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return versions;
    if (!rl->prepare("SELECT id, version, image, enabled, default_version "
                      "FROM php_versions ORDER BY id")) return versions;
    while (rl->step()) {
        php::PhpVersion pv;
        pv.id = static_cast<uint64_t>(rl->column_int(0));
        pv.version = rl->column_text(1);
        pv.image = rl->column_text(2);
        pv.enabled = (rl->column_int(3) != 0);
        pv.default_version = (rl->column_int(4) != 0);
        pv.name = pv.version;
        versions.push_back(std::move(pv));
    }
    return versions;
}

// --- Profiles ---

bool SQLiteStorage::try_save_profiles(const std::vector<profile::Profile>& profiles) {
    return replace_all(pool_, "DELETE FROM profiles",
        [&](SQLiteDB& db) -> bool {
            for (const auto& p : profiles) {
                if (!db.prepare(
                        "INSERT INTO profiles (id, profile_name, type, web_server, "
                        "runtime, template_path, description, enabled, "
                        "default_profile, created_at, updated_at) VALUES "
                        "(?, ?, ?, ?, ?, ?, ?, ?, ?, "
                        "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                        "strftime('%Y-%m-%dT%H:%M:%SZ','now'))"))
                    return false;
                if (!db.bind_int(1, static_cast<int64_t>(p.id))) return false;
                if (!db.bind_text(2, p.profile_name)) return false;
                if (!db.bind_text(3, profile::profile_type_to_string(p.type))) return false;
                if (!db.bind_text(4, p.web_server)) return false;
                if (!db.bind_text(5, p.runtime)) return false;
                if (!db.bind_text(6, p.template_path)) return false;
                if (!db.bind_text(7, p.description)) return false;
                if (!db.bind_int(8, p.enabled ? 1 : 0)) return false;
                if (!db.bind_int(9, p.default_profile ? 1 : 0)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_profiles(const std::vector<profile::Profile>& profiles) {
    (void)try_save_profiles(profiles);
}

std::vector<profile::Profile> SQLiteStorage::load_profiles() {
    std::vector<profile::Profile> profiles;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return profiles;
    if (!rl->prepare("SELECT id, profile_name, type, web_server, runtime, "
                      "template_path, description, enabled, default_profile "
                      "FROM profiles ORDER BY id")) return profiles;
    while (rl->step()) {
        profile::Profile p;
        p.id = static_cast<uint64_t>(rl->column_int(0));
        p.profile_name = rl->column_text(1);
        p.type = profile::profile_type_from_string(rl->column_text(2));
        p.web_server = rl->column_text(3);
        p.runtime = rl->column_text(4);
        p.template_path = rl->column_text(5);
        p.description = rl->column_text(6);
        p.enabled = (rl->column_int(7) != 0);
        p.default_profile = (rl->column_int(8) != 0);
        p.name = p.profile_name;
        profiles.push_back(std::move(p));
    }
    return profiles;
}

// --- Users ---

bool SQLiteStorage::try_save_users(const std::vector<user::User>& users) {
    return replace_all(pool_, "DELETE FROM users",
        [&](SQLiteDB& db) -> bool {
            const char* sql = "INSERT INTO users (id, username, uid, home_directory, "
                "shell, enabled, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'))";
            for (const auto& u : users) {
                if (!db.prepare(sql)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(u.id))) return false;
                if (!db.bind_text(2, u.username)) return false;
                if (!db.bind_int(3, static_cast<int64_t>(u.uid))) return false;
                if (!db.bind_text(4, u.home_directory)) return false;
                if (!db.bind_text(5, u.shell)) return false;
                if (!db.bind_int(6, u.enabled ? 1 : 0)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_users(const std::vector<user::User>& users) {
    (void)try_save_users(users);
}

std::vector<user::User> SQLiteStorage::load_users() {
    std::vector<user::User> users;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return users;
    if (!rl->prepare("SELECT id, username, uid, home_directory, shell, enabled "
                      "FROM users ORDER BY id")) return users;
    while (rl->step()) {
        user::User u;
        u.id = static_cast<uint64_t>(rl->column_int(0));
        u.username = rl->column_text(1);
        u.uid = static_cast<uint64_t>(rl->column_int(2));
        u.home_directory = rl->column_text(3);
        u.shell = rl->column_text(4);
        u.enabled = (rl->column_int(5) != 0);
        u.name = u.username;
        users.push_back(std::move(u));
    }
    return users;
}

// --- Sites ---

std::vector<site::Site> SQLiteStorage::load_sites() {
    std::vector<site::Site> sites;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return sites;
    if (!rl->prepare("SELECT id, domain, owner, node_id, web_server, php_mail_enabled "
                      "FROM sites ORDER BY id")) return sites;
    while (rl->step()) {
        site::Site s;
        s.id = static_cast<uint64_t>(rl->column_int(0));
        s.domain = rl->column_text(1);
        s.owner = rl->column_text(2);
        s.node_id = static_cast<uint64_t>(rl->column_int(3));
        s.web_server = rl->column_text(4);
        s.php_mail_enabled = (rl->column_int(5) != 0);
        // SQLite rows have an explicit php_mail_enabled column —
        // mark the field as present (equivalent to current 6-field format).
        s.php_mail_enabled_present = true;
        s.name = s.domain;
        sites.push_back(std::move(s));
    }
    return sites;
}

// --- Domains ---

bool SQLiteStorage::try_save_domains(const std::vector<domain::Domain>& domains) {
    return replace_all(pool_, "DELETE FROM domains",
        [&](SQLiteDB& db) -> bool {
            const char* sql = "INSERT INTO domains (id, fqdn, owner_id, site_id, "
                "php_version, ssl_enabled, enabled, type, target, "
                "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'))";
            for (const auto& d : domains) {
                if (!db.prepare(sql)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(d.id))) return false;
                if (!db.bind_text(2, d.fqdn)) return false;
                if (!db.bind_int(3, static_cast<int64_t>(d.owner_id))) return false;
                if (!db.bind_int(4, static_cast<int64_t>(d.site_id))) return false;
                if (!db.bind_text(5, d.php_version)) return false;
                if (!db.bind_int(6, d.ssl_enabled ? 1 : 0)) return false;
                if (!db.bind_int(7, d.enabled ? 1 : 0)) return false;
                if (!db.bind_text(8, d.type)) return false;
                if (!db.bind_text(9, d.target)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_domains(const std::vector<domain::Domain>& domains) {
    (void)try_save_domains(domains);
}

std::vector<domain::Domain> SQLiteStorage::load_domains() {
    std::vector<domain::Domain> domains;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return domains;
    if (!rl->prepare("SELECT id, fqdn, owner_id, site_id, php_version, "
                      "ssl_enabled, enabled, type, target "
                      "FROM domains ORDER BY id")) return domains;
    while (rl->step()) {
        domain::Domain d;
        d.id = static_cast<uint64_t>(rl->column_int(0));
        d.fqdn = rl->column_text(1);
        d.owner_id = static_cast<uint64_t>(rl->column_int(2));
        d.site_id = static_cast<uint64_t>(rl->column_int(3));
        d.php_version = rl->column_text(4);
        d.ssl_enabled = (rl->column_int(5) != 0);
        d.enabled = (rl->column_int(6) != 0);
        d.type = rl->column_text(7);
        d.target = rl->column_text(8);
        d.name = d.fqdn;
        domains.push_back(std::move(d));
    }
    return domains;
}

// --- Databases ---

bool SQLiteStorage::try_save_databases(const std::vector<database::Database>& databases) {
    return replace_all(pool_, "DELETE FROM databases",
        [&](SQLiteDB& db) -> bool {
            const char* sql = "INSERT INTO databases (id, db_name, db_user, db_password, "
                "engine, version, owner_id, site_id, enabled, "
                "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'))";
            for (const auto& d : databases) {
                if (!db.prepare(sql)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(d.id))) return false;
                if (!db.bind_text(2, d.db_name)) return false;
                if (!db.bind_text(3, d.db_user)) return false;
                if (!db.bind_text(4, d.db_password)) return false;
                if (!db.bind_text(5, d.engine)) return false;
                if (!db.bind_text(6, d.version)) return false;
                if (!db.bind_int(7, static_cast<int64_t>(d.owner_id))) return false;
                if (!db.bind_int(8, static_cast<int64_t>(d.site_id))) return false;
                if (!db.bind_int(9, d.enabled ? 1 : 0)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_databases(const std::vector<database::Database>& databases) {
    (void)try_save_databases(databases);
}

std::vector<database::Database> SQLiteStorage::load_databases() {
    std::vector<database::Database> databases;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return databases;
    if (!rl->prepare("SELECT id, db_name, db_user, db_password, engine, version, "
                      "owner_id, site_id, enabled FROM databases ORDER BY id"))
        return databases;
    while (rl->step()) {
        database::Database d;
        d.id = static_cast<uint64_t>(rl->column_int(0));
        d.db_name = rl->column_text(1);
        d.db_user = rl->column_text(2);
        d.db_password = rl->column_text(3);
        d.engine = rl->column_text(4);
        d.version = rl->column_text(5);
        d.owner_id = static_cast<uint64_t>(rl->column_int(6));
        d.site_id = static_cast<uint64_t>(rl->column_int(7));
        d.enabled = (rl->column_int(8) != 0);
        d.name = d.db_name;
        databases.push_back(std::move(d));
    }
    return databases;
}

// --- Reverse proxies ---

bool SQLiteStorage::try_save_reverse_proxies(const std::vector<proxy::ReverseProxy>& proxies) {
    return replace_all(pool_, "DELETE FROM reverse_proxies",
        [&](SQLiteDB& db) -> bool {
            const char* sql = "INSERT INTO reverse_proxies (id, domain, site_id, provider, "
                "config_path, upstream, enabled, status, "
                "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'))";
            for (const auto& p : proxies) {
                if (!db.prepare(sql)) return false;
                if (!db.bind_int(1, static_cast<uint64_t>(p.id))) return false;
                if (!db.bind_text(2, p.domain)) return false;
                if (!db.bind_int(3, static_cast<int64_t>(p.site_id))) return false;
                if (!db.bind_text(4, p.provider)) return false;
                if (!db.bind_text(5, p.config_path)) return false;
                if (!db.bind_text(6, p.upstream)) return false;
                if (!db.bind_int(7, p.enabled ? 1 : 0)) return false;
                if (!db.bind_text(8, p.status)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_reverse_proxies(const std::vector<proxy::ReverseProxy>& proxies) {
    (void)try_save_reverse_proxies(proxies);
}

std::vector<proxy::ReverseProxy> SQLiteStorage::load_reverse_proxies() {
    std::vector<proxy::ReverseProxy> proxies;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return proxies;
    if (!rl->prepare("SELECT id, domain, site_id, provider, config_path, upstream, "
                      "enabled, status FROM reverse_proxies ORDER BY id"))
        return proxies;
    while (rl->step()) {
        proxy::ReverseProxy p;
        p.id = static_cast<uint64_t>(rl->column_int(0));
        p.domain = rl->column_text(1);
        p.site_id = static_cast<uint64_t>(rl->column_int(2));
        p.provider = rl->column_text(3);
        p.config_path = rl->column_text(4);
        p.upstream = rl->column_text(5);
        p.enabled = (rl->column_int(6) != 0);
        p.status = rl->column_text(7);
        p.name = p.domain;
        proxies.push_back(std::move(p));
    }
    return proxies;
}

// -----------------------------------------------------------
// FK-safe parent synchronization helper (UPSERT + strict prune)
// -----------------------------------------------------------
// Enforces strict fail-closed semantics:
//   - UPSERT all supplied rows
//   - Enumerate existing IDs with strict error checking
//   - Prune absent IDs using bound DELETE
//   - Checked commit
//   - Any failure rolls back entire transaction
static bool sync_parent_rows(
    ConnectionPool& pool,
    const std::string& table,
    const std::set<uint64_t>& supplied_ids,
    const std::function<bool(SQLiteDB& db)>& upsert_all)
{
    TransactionGuard txn(pool);
    if (!txn.is_active()) return false;

    // Phase 1: UPSERT all supplied rows
    if (!upsert_all(txn.db())) return false;

    // Phase 2: Enumerate existing IDs strictly
    std::set<uint64_t> existing_ids;
    if (!txn.db().prepare("SELECT id FROM " + table + " ORDER BY id")) return false;

    // step(): true=ROW, false=DONE/ERROR.  Distinguish by error_code.
    // (step() clears error before stepping, so error_code=0 after DONE)
    bool has_row = txn.db().step();
    while (true) {
        if (has_row) {
            existing_ids.insert(static_cast<uint64_t>(txn.db().column_int(0)));
            has_row = txn.db().step();
        } else {
            if (txn.db().error_code() == 0) break;  // DONE — complete
            return false;  // step error → rollback
        }
    }

    // Phase 3: Prune absent IDs using bound DELETE
    const std::string del_sql = "DELETE FROM " + table + " WHERE id = ?";
    for (uint64_t eid : existing_ids) {
        if (supplied_ids.find(eid) == supplied_ids.end()) {
            if (!txn.db().prepare(del_sql)) return false;
            if (!txn.db().bind_int(1, static_cast<int64_t>(eid))) return false;
            if (txn.db().step() == false && txn.db().error_code() != 0) return false;
        }
    }

    // Phase 4: Checked commit
    return txn.commit();
}

// --- Access users (FK-safe parent sync: UPSERT + prune) ---

bool SQLiteStorage::try_save_access_users(const std::vector<access::AccessUser>& users) {
    std::set<uint64_t> supplied_ids;
    for (const auto& u : users) supplied_ids.insert(u.id);

    return sync_parent_rows(pool_, "access_users", supplied_ids,
        [&](SQLiteDB& db) -> bool {
            const char* upsert = "INSERT INTO access_users "
                "(id, username, auth_type, password_hash, enabled, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now')) "
                "ON CONFLICT(id) DO UPDATE SET "
                "username=excluded.username, auth_type=excluded.auth_type, "
                "password_hash=excluded.password_hash, enabled=excluded.enabled, "
                "updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now')";
            for (const auto& u : users) {
                if (!db.prepare(upsert)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(u.id))) return false;
                if (!db.bind_text(2, u.username)) return false;
                if (!db.bind_text(3, u.auth_type)) return false;
                if (!db.bind_text(4, u.password_hash)) return false;
                if (!db.bind_int(5, u.enabled ? 1 : 0)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_access_users(const std::vector<access::AccessUser>& users) {
    (void)try_save_access_users(users);
}

std::vector<access::AccessUser> SQLiteStorage::load_access_users() {
    std::vector<access::AccessUser> users;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return users;
    if (!rl->prepare("SELECT id, username, auth_type, password_hash, enabled "
                      "FROM access_users ORDER BY id")) return users;
    while (rl->step()) {
        access::AccessUser u;
        u.id = static_cast<uint64_t>(rl->column_int(0));
        u.username = rl->column_text(1);
        u.auth_type = rl->column_text(2);
        u.password_hash = rl->column_text(3);
        u.enabled = (rl->column_int(4) != 0);
        u.name = u.username;
        users.push_back(std::move(u));
    }
    return users;
}

// --- Access grants (child table, normal replace_all with FK enforcement) ---

bool SQLiteStorage::try_save_access_grants(const std::vector<access::AccessGrant>& grants) {
    return replace_all(pool_, "DELETE FROM access_grants",
        [&](SQLiteDB& db) -> bool {
            const char* sql = "INSERT INTO access_grants "
                "(id, access_user_id, site_id, permission, "
                "created_at, updated_at) VALUES (?, ?, ?, ?, "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'))";
            for (const auto& g : grants) {
                if (!db.prepare(sql)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(g.id))) return false;
                if (!db.bind_int(2, static_cast<int64_t>(g.access_user_id))) return false;
                if (!db.bind_int(3, static_cast<int64_t>(g.site_id))) return false;
                if (!db.bind_text(4, access::permission_to_string(g.permission))) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_access_grants(const std::vector<access::AccessGrant>& grants) {
    (void)try_save_access_grants(grants);
}

std::vector<access::AccessGrant> SQLiteStorage::load_access_grants() {
    std::vector<access::AccessGrant> grants;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return grants;
    if (!rl->prepare("SELECT id, access_user_id, site_id, permission "
                      "FROM access_grants ORDER BY id")) return grants;
    while (rl->step()) {
        access::AccessGrant g;
        g.id = static_cast<uint64_t>(rl->column_int(0));
        g.access_user_id = static_cast<uint64_t>(rl->column_int(1));
        g.site_id = static_cast<uint64_t>(rl->column_int(2));
        g.permission = access::permission_from_string(rl->column_text(3));
        g.name = std::to_string(g.access_user_id) + "-" + std::to_string(g.site_id);
        grants.push_back(std::move(g));
    }
    return grants;
}

bool SQLiteStorage::try_save_sites(const std::vector<site::Site>& sites) {
    std::set<uint64_t> supplied_ids;
    for (const auto& s : sites) supplied_ids.insert(s.id);

    return sync_parent_rows(pool_, "sites", supplied_ids,
        [&](SQLiteDB& db) -> bool {
            const char* upsert = "INSERT INTO sites "
                "(id, domain, owner, node_id, web_server, php_mail_enabled, "
                "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now')) "
                "ON CONFLICT(id) DO UPDATE SET "
                "domain=excluded.domain, owner=excluded.owner, "
                "node_id=excluded.node_id, web_server=excluded.web_server, "
                "php_mail_enabled=excluded.php_mail_enabled, "
                "updated_at=strftime('%Y-%m-%dT%H:%M:%SZ','now')";
            for (const auto& s : sites) {
                if (!db.prepare(upsert)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(s.id))) return false;
                if (!db.bind_text(2, s.domain)) return false;
                if (!db.bind_text(3, s.owner)) return false;
                if (!db.bind_int(4, static_cast<int64_t>(s.node_id))) return false;
                if (!db.bind_text(5, s.web_server)) return false;
                if (!db.bind_int(6, s.php_mail_enabled ? 1 : 0)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_sites(const std::vector<site::Site>& sites) {
    (void)try_save_sites(sites);
}

// ============================================================
// Phase 7 — Mail and SSL metadata storage
// ============================================================

// --- SSL certificates ---

bool SQLiteStorage::try_save_ssl_certificates(const std::vector<ssl::SslCertificate>& certs) {
    return replace_all(pool_, "DELETE FROM ssl_certificates",
        [&](SQLiteDB& db) -> bool {
            const char* sql = "INSERT INTO ssl_certificates "
                "(id, domain_id, domain, provider, certificate_path, key_path, "
                "chain_path, issued_at, expires_at, renew_after, status, "
                "auto_renew, https_enabled, redirect_enabled, domains, "
                "challenge_type, last_error, last_validation, renew_attempts, "
                "version, created_at, updated_at) VALUES "
                "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'))";
            for (const auto& c : certs) {
                if (!db.prepare(sql)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(c.id))) return false;
                if (!db.bind_int(2, static_cast<int64_t>(c.domain_id))) return false;
                if (!db.bind_text(3, c.domain)) return false;
                if (!db.bind_text(4, c.provider)) return false;
                if (!db.bind_text(5, c.certificate_path)) return false;
                if (!db.bind_text(6, c.key_path)) return false;
                if (!db.bind_text(7, c.chain_path)) return false;
                if (!db.bind_text(8, c.issued_at)) return false;
                if (!db.bind_text(9, c.expires_at)) return false;
                if (!db.bind_text(10, c.renew_after)) return false;
                if (!db.bind_text(11, c.status)) return false;
                if (!db.bind_int(12, c.auto_renew ? 1 : 0)) return false;
                if (!db.bind_int(13, c.https_enabled ? 1 : 0)) return false;
                if (!db.bind_int(14, c.redirect_enabled ? 1 : 0)) return false;
                if (!db.bind_text(15, c.domains)) return false;
                if (!db.bind_text(16, c.challenge_type)) return false;
                if (!db.bind_text(17, c.last_error)) return false;
                if (!db.bind_text(18, c.last_validation)) return false;
                if (!db.bind_int(19, static_cast<int64_t>(c.renew_attempts))) return false;
                if (!db.bind_int(20, static_cast<int64_t>(c.version))) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_ssl_certificates(const std::vector<ssl::SslCertificate>& certs) {
    (void)try_save_ssl_certificates(certs);
}

std::vector<ssl::SslCertificate> SQLiteStorage::load_ssl_certificates() {
    std::vector<ssl::SslCertificate> certs;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return certs;
    const char* sql = "SELECT id, domain_id, domain, provider, certificate_path, "
        "key_path, chain_path, issued_at, expires_at, renew_after, status, "
        "auto_renew, https_enabled, redirect_enabled, domains, "
        "challenge_type, last_error, last_validation, renew_attempts, version "
        "FROM ssl_certificates ORDER BY id";
    if (!rl->prepare(sql)) return certs;
    while (rl->step()) {
        ssl::SslCertificate c;
        c.id = static_cast<uint64_t>(rl->column_int(0));
        c.domain_id = static_cast<uint64_t>(rl->column_int(1));
        c.domain = rl->column_text(2);
        c.provider = rl->column_text(3);
        c.certificate_path = rl->column_text(4);
        c.key_path = rl->column_text(5);
        c.chain_path = rl->column_text(6);
        c.issued_at = rl->column_text(7);
        c.expires_at = rl->column_text(8);
        c.renew_after = rl->column_text(9);
        c.status = rl->column_text(10);
        c.auto_renew = (rl->column_int(11) != 0);
        c.https_enabled = (rl->column_int(12) != 0);
        c.redirect_enabled = (rl->column_int(13) != 0);
        c.domains = rl->column_text(14);
        c.challenge_type = rl->column_text(15);
        c.last_error = rl->column_text(16);
        c.last_validation = rl->column_text(17);
        c.renew_attempts = static_cast<int>(rl->column_int(18));
        c.version = static_cast<int>(rl->column_int(19));
        c.name = c.domain;
        certs.push_back(std::move(c));
    }
    return certs;
}

// --- Mail domains (FK-safe parent sync — referenced by mailboxes/aliases) ---

bool SQLiteStorage::try_save_mail_domains(const std::vector<mail::MailDomain>& domains) {
    std::set<uint64_t> supplied_ids;
    for (const auto& m : domains) supplied_ids.insert(m.id);

    return sync_parent_rows(pool_, "mail_domains", supplied_ids,
        [&](SQLiteDB& db) -> bool {
            const char* sql = "INSERT INTO mail_domains "
                "(id, domain_id, site_id, domain_name, mode, relay_host, "
                "dkim_selector, dkim_private_key_path, dkim_public_key_dns, "
                "max_mailboxes, max_aliases, catch_all, enabled, "
                "created_at, updated_at) VALUES "
                "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(id) DO UPDATE SET "
                "domain_id=excluded.domain_id, site_id=excluded.site_id, "
                "domain_name=excluded.domain_name, mode=excluded.mode, "
                "relay_host=excluded.relay_host, "
                "dkim_selector=excluded.dkim_selector, "
                "dkim_private_key_path=excluded.dkim_private_key_path, "
                "dkim_public_key_dns=excluded.dkim_public_key_dns, "
                "max_mailboxes=excluded.max_mailboxes, "
                "max_aliases=excluded.max_aliases, "
                "catch_all=excluded.catch_all, enabled=excluded.enabled, "
                "created_at=excluded.created_at, "
                "updated_at=excluded.updated_at";
            for (const auto& m : domains) {
                if (!db.prepare(sql)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(m.id))) return false;
                if (!db.bind_int(2, static_cast<int64_t>(m.domain_id))) return false;
                if (!db.bind_int(3, static_cast<int64_t>(m.site_id))) return false;
                if (!db.bind_text(4, m.domain_name)) return false;
                if (!db.bind_text(5, mail::mail_domain_mode_to_string(m.mode))) return false;
                if (!db.bind_text(6, m.relay_host)) return false;
                if (!db.bind_text(7, m.dkim_selector)) return false;
                if (!db.bind_text(8, m.dkim_private_key_path)) return false;
                if (!db.bind_text(9, m.dkim_public_key_dns)) return false;
                if (!db.bind_int(10, static_cast<int64_t>(m.max_mailboxes))) return false;
                if (!db.bind_int(11, static_cast<int64_t>(m.max_aliases))) return false;
                if (!db.bind_text(12, m.catch_all)) return false;
                if (!db.bind_int(13, m.enabled ? 1 : 0)) return false;
                if (!db.bind_text(14, m.created_at)) return false;
                if (!db.bind_text(15, m.updated_at)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_mail_domains(const std::vector<mail::MailDomain>& domains) {
    (void)try_save_mail_domains(domains);
}

std::vector<mail::MailDomain> SQLiteStorage::load_mail_domains() {
    std::vector<mail::MailDomain> domains;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return domains;
    const char* sql = "SELECT id, domain_id, site_id, domain_name, mode, relay_host, "
        "dkim_selector, dkim_private_key_path, dkim_public_key_dns, "
        "max_mailboxes, max_aliases, catch_all, enabled, "
        "created_at, updated_at "
        "FROM mail_domains ORDER BY id";
    if (!rl->prepare(sql)) return domains;
    while (rl->step()) {
        mail::MailDomain m;
        m.id = static_cast<uint64_t>(rl->column_int(0));
        m.domain_id = static_cast<uint64_t>(rl->column_int(1));
        m.site_id = static_cast<uint64_t>(rl->column_int(2));
        m.domain_name = rl->column_text(3);
        m.mode = mail::mail_domain_mode_from_string(rl->column_text(4));
        m.relay_host = rl->column_text(5);
        m.dkim_selector = rl->column_text(6);
        m.dkim_private_key_path = rl->column_text(7);
        m.dkim_public_key_dns = rl->column_text(8);
        m.max_mailboxes = static_cast<uint64_t>(rl->column_int(9));
        m.max_aliases = static_cast<uint64_t>(rl->column_int(10));
        m.catch_all = rl->column_text(11);
        m.enabled = (rl->column_int(12) != 0);
        m.created_at = rl->column_text(13);
        m.updated_at = rl->column_text(14);
        m.name = m.domain_name;
        domains.push_back(std::move(m));
    }
    return domains;
}

// --- Mail mailboxes (child table, FK → mail_domains) ---

bool SQLiteStorage::try_save_mailboxes(const std::vector<mail::Mailbox>& mailboxes) {
    return replace_all(pool_, "DELETE FROM mail_mailboxes",
        [&](SQLiteDB& db) -> bool {
            const char* sql = "INSERT INTO mail_mailboxes "
                "(id, domain_id, local_part, password_hash, quota_bytes, "
                "quota_messages, enabled, display_name, forward_to, "
                "spam_enabled, last_login, created_at, updated_at) VALUES "
                "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            for (const auto& mb : mailboxes) {
                if (!db.prepare(sql)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(mb.id))) return false;
                if (!db.bind_int(2, static_cast<int64_t>(mb.domain_id))) return false;
                if (!db.bind_text(3, mb.local_part)) return false;
                if (!db.bind_text(4, mb.password_hash)) return false;
                if (!db.bind_int(5, static_cast<int64_t>(mb.quota_bytes))) return false;
                if (!db.bind_int(6, static_cast<int64_t>(mb.quota_messages))) return false;
                if (!db.bind_int(7, mb.enabled ? 1 : 0)) return false;
                if (!db.bind_text(8, mb.display_name)) return false;
                if (!db.bind_text(9, mb.forward_to)) return false;
                if (!db.bind_int(10, mb.spam_enabled ? 1 : 0)) return false;
                if (!db.bind_text(11, mb.last_login)) return false;
                if (!db.bind_text(12, mb.created_at)) return false;
                if (!db.bind_text(13, mb.updated_at)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_mailboxes(const std::vector<mail::Mailbox>& mailboxes) {
    (void)try_save_mailboxes(mailboxes);
}

std::vector<mail::Mailbox> SQLiteStorage::load_mailboxes() {
    std::vector<mail::Mailbox> mailboxes;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return mailboxes;
    const char* sql = "SELECT id, domain_id, local_part, password_hash, quota_bytes, "
        "quota_messages, enabled, display_name, forward_to, spam_enabled, "
        "last_login, created_at, updated_at FROM mail_mailboxes ORDER BY id";
    if (!rl->prepare(sql)) return mailboxes;
    while (rl->step()) {
        mail::Mailbox mb;
        mb.id = static_cast<uint64_t>(rl->column_int(0));
        mb.domain_id = static_cast<uint64_t>(rl->column_int(1));
        mb.local_part = rl->column_text(2);
        mb.password_hash = rl->column_text(3);
        mb.quota_bytes = static_cast<uint64_t>(rl->column_int(4));
        mb.quota_messages = static_cast<uint64_t>(rl->column_int(5));
        mb.enabled = (rl->column_int(6) != 0);
        mb.display_name = rl->column_text(7);
        mb.forward_to = rl->column_text(8);
        mb.spam_enabled = (rl->column_int(9) != 0);
        mb.last_login = rl->column_text(10);
        mb.created_at = rl->column_text(11);
        mb.updated_at = rl->column_text(12);
        mb.name = mb.local_part;
        mailboxes.push_back(std::move(mb));
    }
    return mailboxes;
}

// --- Mail aliases (child table, FK → mail_domains) ---

bool SQLiteStorage::try_save_mail_aliases(const std::vector<mail::MailAlias>& aliases) {
    return replace_all(pool_, "DELETE FROM mail_aliases",
        [&](SQLiteDB& db) -> bool {
            const char* sql = "INSERT INTO mail_aliases "
                "(id, domain_id, source_local_part, destination, enabled, "
                "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?)";
            for (const auto& a : aliases) {
                if (!db.prepare(sql)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(a.id))) return false;
                if (!db.bind_int(2, static_cast<int64_t>(a.domain_id))) return false;
                if (!db.bind_text(3, a.source_local_part)) return false;
                if (!db.bind_text(4, a.destination)) return false;
                if (!db.bind_int(5, a.enabled ? 1 : 0)) return false;
                if (!db.bind_text(6, a.created_at)) return false;
                if (!db.bind_text(7, a.updated_at)) return false;
                if (db.step() == false && db.error_code() != 0) return false;
            }
            return true;
        });
}

void SQLiteStorage::save_mail_aliases(const std::vector<mail::MailAlias>& aliases) {
    (void)try_save_mail_aliases(aliases);
}

std::vector<mail::MailAlias> SQLiteStorage::load_mail_aliases() {
    std::vector<mail::MailAlias> aliases;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return aliases;
    const char* sql = "SELECT id, domain_id, source_local_part, destination, enabled, "
        "created_at, updated_at FROM mail_aliases ORDER BY id";
    if (!rl->prepare(sql)) return aliases;
    while (rl->step()) {
        mail::MailAlias a;
        a.id = static_cast<uint64_t>(rl->column_int(0));
        a.domain_id = static_cast<uint64_t>(rl->column_int(1));
        a.source_local_part = rl->column_text(2);
        a.destination = rl->column_text(3);
        a.enabled = (rl->column_int(4) != 0);
        a.created_at = rl->column_text(5);
        a.updated_at = rl->column_text(6);
        a.name = a.source_local_part;
        aliases.push_back(std::move(a));
    }
    return aliases;
}

// --- Mail module state (key in mail_config) ---

bool SQLiteStorage::try_save_mail_module_state(const std::string& state) {
    TransactionGuard txn(pool_);
    if (!txn.is_active()) return false;
    const char* sql = "INSERT OR REPLACE INTO mail_config (key, value) VALUES "
        "('module_state', ?)";
    if (!txn.db().prepare(sql)) return false;
    if (!txn.db().bind_text(1, state)) return false;
    if (txn.db().step() == false && txn.db().error_code() != 0) return false;
    return txn.commit();
}

void SQLiteStorage::save_mail_module_state(const std::string& state) {
    (void)try_save_mail_module_state(state);
}

std::string SQLiteStorage::load_mail_module_state() {
    ReadLease rl(pool_);
    if (!rl.is_valid()) return {};
    if (!rl->prepare("SELECT value FROM mail_config WHERE key = 'module_state'"))
        return {};
    if (rl->step()) return rl->column_text(0);
    return {};
}

// --- Mail smarthost config (key in mail_config) ---

bool SQLiteStorage::try_save_mail_smarthost(const std::string& config) {
    TransactionGuard txn(pool_);
    if (!txn.is_active()) return false;
    const char* sql = "INSERT OR REPLACE INTO mail_config (key, value) VALUES "
        "('smarthost', ?)";
    if (!txn.db().prepare(sql)) return false;
    if (!txn.db().bind_text(1, config)) return false;
    if (txn.db().step() == false && txn.db().error_code() != 0) return false;
    return txn.commit();
}

void SQLiteStorage::save_mail_smarthost(const std::string& config) {
    (void)try_save_mail_smarthost(config);
}

std::string SQLiteStorage::load_mail_smarthost() {
    ReadLease rl(pool_);
    if (!rl.is_valid()) return {};
    if (!rl->prepare("SELECT value FROM mail_config WHERE key = 'smarthost'"))
        return {};
    if (rl->step()) return rl->column_text(0);
    return {};
}

} // namespace containercp::storage
