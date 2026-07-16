#include "SQLiteStorage.h"
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

void SQLiteStorage::save_nodes(const std::vector<node::Node>& nodes) {
    replace_all(pool_, "DELETE FROM nodes",
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

void SQLiteStorage::save_php_versions(const std::vector<php::PhpVersion>& versions) {
    replace_all(pool_, "DELETE FROM php_versions",
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

void SQLiteStorage::save_profiles(const std::vector<profile::Profile>& profiles) {
    replace_all(pool_, "DELETE FROM profiles",
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

void SQLiteStorage::save_users(const std::vector<user::User>& users) {
    replace_all(pool_, "DELETE FROM users",
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

void SQLiteStorage::save_domains(const std::vector<domain::Domain>& domains) {
    replace_all(pool_, "DELETE FROM domains",
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

void SQLiteStorage::save_databases(const std::vector<database::Database>& databases) {
    replace_all(pool_, "DELETE FROM databases",
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

void SQLiteStorage::save_reverse_proxies(const std::vector<proxy::ReverseProxy>& proxies) {
    replace_all(pool_, "DELETE FROM reverse_proxies",
        [&](SQLiteDB& db) -> bool {
            const char* sql = "INSERT INTO reverse_proxies (id, domain, site_id, provider, "
                "config_path, upstream, enabled, status, "
                "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'))";
            for (const auto& p : proxies) {
                if (!db.prepare(sql)) return false;
                if (!db.bind_int(1, static_cast<int64_t>(p.id))) return false;
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

void SQLiteStorage::save_access_users(const std::vector<access::AccessUser>& users) {
    std::set<uint64_t> supplied_ids;
    for (const auto& u : users) supplied_ids.insert(u.id);

    sync_parent_rows(pool_, "access_users", supplied_ids,
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

void SQLiteStorage::save_access_grants(const std::vector<access::AccessGrant>& grants) {
    replace_all(pool_, "DELETE FROM access_grants",
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

void SQLiteStorage::save_sites(const std::vector<site::Site>& sites) {
    std::set<uint64_t> supplied_ids;
    for (const auto& s : sites) supplied_ids.insert(s.id);

    sync_parent_rows(pool_, "sites", supplied_ids,
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

} // namespace containercp::storage
