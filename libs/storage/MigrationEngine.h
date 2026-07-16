#ifndef CONTAINERCP_STORAGE_MIGRATION_ENGINE_H
#define CONTAINERCP_STORAGE_MIGRATION_ENGINE_H

#include "SQLiteWrapper.h"

#include <functional>
#include <string>
#include <vector>

namespace containercp::storage {

// A single schema or data migration.
//
// version: monotonic integer, >= 1.  Migrations are applied in
//   version order.  There is no gap-filling — every version must
//   be registered.
// name: human-readable unique identifier (e.g. "create_meta_tables").
// up: function that performs the migration.  Receives the database
//   connection and a diagnostics string to fill on error.
//   Returns true on success, false on failure.
struct Migration {
    int version = 0;
    std::string name;
    std::function<bool(SQLiteDB& db, std::string& diagnostics)> up;
};

// Versioned schema migration engine.
//
// Lifecycle:
//   1. Create MigrationEngine.
//   2. Register all migrations in any order (sorted by version).
//   3. Call migrate() on an open database connection.
//   4. migrate() detects current version from schema_migrations table,
//      applies pending migrations in order, and records results.
//
// The engine is reusable — can be called on different databases or
// after schema reset.
class MigrationEngine {
public:
    MigrationEngine() = default;

    // Register a migration.  Duplicate versions are rejected.
    void register_migration(Migration m);

    // Detect the current schema version by reading the
    // schema_migrations table.  Returns 0 if the table does
    // not exist or has no completed migrations.
    int current_version(SQLiteDB& db);

    // Apply all pending migrations in version order.
    // Returns true if all pending migrations completed.
    // Returns false on first failure (fail-fast).
    // After failure, inspect last_error() for diagnostics.
    bool migrate(SQLiteDB& db);

    // Human-readable error from the last failed migrate() call.
    std::string last_error() const;

private:
    bool ensure_meta_tables(SQLiteDB& db);
    bool apply_one(SQLiteDB& db, const Migration& m);
    bool checksum_matches(SQLiteDB& db, const Migration& m);
    std::string compute_checksum(const Migration& m);

    std::vector<Migration> migrations_;
    std::string last_error_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_MIGRATION_ENGINE_H
