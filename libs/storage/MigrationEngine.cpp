#include "MigrationEngine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <openssl/sha.h>
#include <sstream>

namespace containercp::storage {

namespace {

std::string timestamp_utc() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
    return ts.str();
}

std::string sha256_hex(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, input.data(), input.size());
    SHA256_Final(hash, &ctx);

    std::ostringstream hex;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        hex << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(hash[i]);
    }
    return hex.str();
}

std::string escape_sql_string(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    return out;
}

} // anonymous namespace

void MigrationEngine::register_migration(Migration m) {
    migrations_.push_back(std::move(m));
}

int MigrationEngine::current_version(SQLiteDB& db) {
    if (!db.prepare("SELECT COALESCE(MAX(version), 0) FROM schema_migrations "
                     "WHERE status = 'completed'")) {
        return 0;
    }
    if (db.step()) {
        return static_cast<int>(db.column_int(0));
    }
    return 0;
}

bool MigrationEngine::migrate(SQLiteDB& db) {
    last_error_.clear();

    std::sort(migrations_.begin(), migrations_.end(),
              [](const Migration& a, const Migration& b) {
                  return a.version < b.version;
              });

    // Reject duplicate versions with different descriptors.
    // Two migrations with the same version and same descriptor
    // are allowed (idempotent registration) but the second is
    // silently skipped via checksum match.
    for (size_t i = 0; i + 1 < migrations_.size(); ++i) {
        if (migrations_[i].version == migrations_[i + 1].version &&
            migrations_[i].descriptor != migrations_[i + 1].descriptor) {
            last_error_ = "Duplicate migration version "
                + std::to_string(migrations_[i].version)
                + " with different descriptors: '"
                + migrations_[i].descriptor + "' vs '"
                + migrations_[i + 1].descriptor + "'";
            return false;
        }
    }

    if (!ensure_meta_tables(db)) {
        return false;
    }

    int current_ver = current_version(db);

    for (const auto& m : migrations_) {
        if (m.version <= current_ver) {
            if (!checksum_matches(db, m)) {
                last_error_ = "Migration " + std::to_string(m.version)
                    + " (" + m.name + ") checksum mismatch. "
                    + "The migration definition has changed since it was applied.";
                return false;
            }
            continue;
        }

        // Check for previously failed or interrupted status
        {
            std::string check_sql = "SELECT status FROM schema_migrations "
                                     "WHERE version = " + std::to_string(m.version);
            if (db.prepare(check_sql) && db.step()) {
                std::string status = db.column_text(0);
                if (status == "failed") {
                    last_error_ = "Migration " + std::to_string(m.version)
                        + " (" + m.name + ") previously failed. "
                        + "Manual recovery required.";
                    return false;
                }
                if (status == "running") {
                    // Reset to pending — apply_one will UPDATE to running
                    db.exec("UPDATE schema_migrations SET status = 'pending' "
                            "WHERE version = " + std::to_string(m.version));
                }
            }
        }

        if (!apply_one(db, m)) {
            return false;
        }
    }

    return true;
}

bool MigrationEngine::apply_one(SQLiteDB& db, const Migration& m) {
    std::string checksum = compute_checksum(m);
    std::string started_at = timestamp_utc();

    // Check if a record already exists (for interrupted migration retry)
    bool record_exists = false;
    if (db.prepare("SELECT 1 FROM schema_migrations WHERE version = ?") &&
        db.bind_int(1, m.version) && db.step()) {
        record_exists = true;
    }

    if (record_exists) {
        std::string upd = "UPDATE schema_migrations SET status = 'running', "
            "started_at = '" + started_at + "', "
            "checksum = '" + checksum + "', "
            "completed_at = NULL, diagnostics = NULL "
            "WHERE version = " + std::to_string(m.version);
        if (!db.exec(upd)) {
            last_error_ = "Failed to update migration record: " + db.error_message();
            return false;
        }
    } else {
        std::string ins = "INSERT INTO schema_migrations "
            "(version, name, checksum, started_at, status) VALUES ("
            + std::to_string(m.version) + ", '"
            + escape_sql_string(m.name) + "', '"
            + checksum + "', '"
            + started_at + "', 'running')";
        if (!db.exec(ins)) {
            last_error_ = "Failed to insert migration record: " + db.error_message();
            return false;
        }
    }

    // Savepoint for rollback on failure
    std::string sp = "mig_" + std::to_string(m.version);
    if (!db.exec("SAVEPOINT " + sp)) {
        last_error_ = "Failed to create savepoint: " + db.error_message();
        return false;
    }

    std::string diagnostics;
    bool ok = false;
    try {
        ok = m.up(db, diagnostics);
    } catch (const std::exception& e) {
        diagnostics = e.what();
        ok = false;
    }

    if (!ok) {
        db.exec("ROLLBACK TO " + sp);
        db.exec("RELEASE " + sp);
        std::string fail = "UPDATE schema_migrations SET status = 'failed', "
            "completed_at = '" + timestamp_utc() + "', "
            "diagnostics = '" + escape_sql_string(diagnostics) + "' "
            "WHERE version = " + std::to_string(m.version);
        db.exec(fail);
        last_error_ = "Migration " + std::to_string(m.version)
            + " (" + m.name + ") failed: " + diagnostics;
        return false;
    }

    db.exec("RELEASE " + sp);

    std::string complete = "UPDATE schema_migrations SET status = 'completed', "
        "completed_at = '" + timestamp_utc() + "' "
        "WHERE version = " + std::to_string(m.version);
    if (!db.exec(complete)) {
        last_error_ = "Failed to mark migration completed: " + db.error_message();
        return false;
    }

    return true;
}

bool MigrationEngine::ensure_meta_tables(SQLiteDB& db) {
    const char* create_schema_migrations = R"(
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version     INTEGER PRIMARY KEY,
            name        TEXT NOT NULL,
            checksum    TEXT NOT NULL,
            started_at  TEXT NOT NULL,
            completed_at TEXT,
            status      TEXT NOT NULL DEFAULT 'pending',
            diagnostics TEXT
        )
    )";

    if (!db.exec(create_schema_migrations)) {
        last_error_ = "Failed to create schema_migrations table: "
            + db.error_message();
        return false;
    }

    const char* create_storage_meta = R"(
        CREATE TABLE IF NOT EXISTS storage_meta (
            key   TEXT PRIMARY KEY,
            value TEXT NOT NULL
        )
    )";

    if (!db.exec(create_storage_meta)) {
        last_error_ = "Failed to create storage_meta table: "
            + db.error_message();
        return false;
    }

    return true;
}

bool MigrationEngine::checksum_matches(SQLiteDB& db, const Migration& m) {
    std::string expected = compute_checksum(m);

    if (!db.prepare("SELECT checksum FROM schema_migrations "
                     "WHERE version = ? AND status = 'completed'")) {
        return false;
    }
    if (!db.bind_int(1, m.version)) return false;
    if (!db.step()) return false;

    std::string stored = db.column_text(0);
    return stored == expected;
}

std::string MigrationEngine::compute_checksum(const Migration& m) {
    // The checksum uniquely identifies the migration definition:
    // version + ":" + descriptor.
    // The developer MUST change the descriptor whenever the migration
    // logic (up function) changes.  This is a deterministic content
    // fingerprint that can be computed without hashing the function
    // itself (which is not safely hashable in C++).
    //
    // If no descriptor is provided, the name is used as fallback
    // (for backward compatibility).
    std::string input = std::to_string(m.version) + ":"
        + (m.descriptor.empty() ? m.name : m.descriptor);
    return sha256_hex(input);
}

std::string MigrationEngine::last_error() const {
    return last_error_;
}

} // namespace containercp::storage
