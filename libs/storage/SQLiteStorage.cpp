#include "SQLiteStorage.h"
#include "profile/ProfileType.h"

namespace containercp::storage {

// ============================================================
// TransactionGuard
// ============================================================

TransactionGuard::TransactionGuard(ConnectionPool& pool)
    : pool_(pool) {
    pool_.lock_write();
    pool_.write_connection().exec("BEGIN IMMEDIATE");
}

TransactionGuard::~TransactionGuard() {
    if (!committed_) {
        if (!suppress_) {
            pool_.write_connection().exec("COMMIT");
        } else {
            pool_.write_connection().exec("ROLLBACK");
        }
    }
    pool_.unlock_write();
}

void TransactionGuard::suppress_commit() {
    suppress_ = true;
}

bool TransactionGuard::commit() {
    if (committed_) return true;
    if (pool_.write_connection().exec("COMMIT")) {
        committed_ = true;
        return true;
    }
    suppress_ = true;
    return false;
}

// ============================================================
// SQLiteStorage
// ============================================================

SQLiteStorage::SQLiteStorage(ConnectionPool& pool)
    : pool_(pool) {
}

// --- Nodes ---

void SQLiteStorage::save_nodes(const std::vector<node::Node>& nodes) {
    TransactionGuard txn(pool_);
    auto& db = pool_.write_connection();

    // Clear existing rows
    if (!db.exec("DELETE FROM nodes")) {
        txn.suppress_commit();
        return;
    }

    // Prepare INSERT
    if (!db.prepare("INSERT INTO nodes (id, name, type, created_at, updated_at) "
                     "VALUES (?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                     "strftime('%Y-%m-%dT%H:%M:%SZ','now'))")) {
        txn.suppress_commit();
        return;
    }

    for (const auto& n : nodes) {
        db.bind_int(1, static_cast<int64_t>(n.id));
        db.bind_text(2, n.name);
        db.bind_text(3, n.type);

        if (!db.step() && db.error_code() != 0) {
            txn.suppress_commit();
            return;
        }
        // Re-prepare for next row (SQLite resets after step to DONE)
        if (!db.prepare("INSERT INTO nodes (id, name, type, created_at, updated_at) "
                         "VALUES (?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                         "strftime('%Y-%m-%dT%H:%M:%SZ','now'))")) {
            txn.suppress_commit();
            return;
        }
    }
}

std::vector<node::Node> SQLiteStorage::load_nodes() {
    std::vector<node::Node> nodes;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return nodes;

    if (!rl->prepare("SELECT id, name, type FROM nodes ORDER BY id")) {
        return nodes;
    }

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
    TransactionGuard txn(pool_);
    auto& db = pool_.write_connection();

    if (!db.exec("DELETE FROM php_versions")) {
        txn.suppress_commit();
        return;
    }

    const char* sql = "INSERT INTO php_versions "
        "(id, version, image, enabled, default_version, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
        "strftime('%Y-%m-%dT%H:%M:%SZ','now'))";

    for (const auto& pv : versions) {
        if (!db.prepare(sql)) { txn.suppress_commit(); return; }
        db.bind_int(1, static_cast<int64_t>(pv.id));
        db.bind_text(2, pv.version);
        db.bind_text(3, pv.image);
        db.bind_int(4, pv.enabled ? 1 : 0);
        db.bind_int(5, pv.default_version ? 1 : 0);
        if (!db.step() && db.error_code() != 0) { txn.suppress_commit(); return; }
    }
}

std::vector<php::PhpVersion> SQLiteStorage::load_php_versions() {
    std::vector<php::PhpVersion> versions;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return versions;

    if (!rl->prepare("SELECT id, version, image, enabled, default_version "
                      "FROM php_versions ORDER BY id")) {
        return versions;
    }

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
    TransactionGuard txn(pool_);
    auto& db = pool_.write_connection();

    if (!db.exec("DELETE FROM profiles")) {
        txn.suppress_commit();
        return;
    }

    const char* sql = "INSERT INTO profiles "
        "(id, profile_name, type, web_server, runtime, template_path, "
        "description, enabled, default_profile, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
        "strftime('%Y-%m-%dT%H:%M:%SZ','now'))";

    for (const auto& p : profiles) {
        if (!db.prepare(sql)) { txn.suppress_commit(); return; }
        db.bind_int(1, static_cast<int64_t>(p.id));
        db.bind_text(2, p.profile_name);
        db.bind_text(3, profile::profile_type_to_string(p.type));
        db.bind_text(4, p.web_server);
        db.bind_text(5, p.runtime);
        db.bind_text(6, p.template_path);
        db.bind_text(7, p.description);
        db.bind_int(8, p.enabled ? 1 : 0);
        db.bind_int(9, p.default_profile ? 1 : 0);
        if (!db.step() && db.error_code() != 0) { txn.suppress_commit(); return; }
    }
}

std::vector<profile::Profile> SQLiteStorage::load_profiles() {
    std::vector<profile::Profile> profiles;
    ReadLease rl(pool_);
    if (!rl.is_valid()) return profiles;

    if (!rl->prepare("SELECT id, profile_name, type, web_server, runtime, "
                      "template_path, description, enabled, default_profile "
                      "FROM profiles ORDER BY id")) {
        return profiles;
    }

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
