#include "SQLiteStorage.h"
#include "profile/ProfileType.h"

#include <functional>

namespace containercp::storage {

// ============================================================
// TransactionGuard
// ============================================================

TransactionGuard::TransactionGuard(ConnectionPool& pool)
    : pool_(pool) {
    pool_.lock_write();
    // Access the write connection directly through pool internals via
    // the write connection reference.  If the pool has been shut down,
    // write_connection() may crash — catch via is_open() check.
    active_ = false;
    // write_connection() returns a reference; we can check is_open()
    // safely as long as write_conn_ is not null.
    // We use a try-catch for the case where write_conn_ is null.
    try {
        if (pool_.write_connection().is_open()) {
            active_ = pool_.write_connection().exec("BEGIN IMMEDIATE");
        }
    } catch (...) {
        active_ = false;
    }
    if (!active_) {
        pool_.unlock_write();
    }
}

TransactionGuard::~TransactionGuard() {
    if (!active_) return;  // never activated — lock already released
    if (!committed_) {
        pool_.write_connection().exec("ROLLBACK");
    }
    pool_.unlock_write();
}

bool TransactionGuard::is_active() const {
    return active_;
}

void TransactionGuard::suppress_commit() {
    // No-op: destructor always rolls back unless commit() was called.
    // Kept for API compatibility with callers that call suppress_commit
    // after a write failure (harmless).
}

bool TransactionGuard::commit() {
    if (!active_ || committed_) return committed_;
    if (pool_.write_connection().exec("COMMIT")) {
        committed_ = true;
        return true;
    }
    return false;
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

    auto& db = pool.write_connection();
    if (!db.exec(delete_sql)) { txn.suppress_commit(); return false; }

    // The caller's bind_each lambda calls prepare + bind + step for
    // each record.  It returns false on any failure, which triggers
    // rollback.
    if (!bind_each(db)) { txn.suppress_commit(); return false; }

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

} // namespace containercp::storage
