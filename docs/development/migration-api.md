# Migration Engine API

## Purpose

`containercp::storage::MigrationEngine` is a reusable, versioned schema
migration framework for the Storage subsystem.

It manages the lifecycle of database schema changes from initial creation
through successive versions. It is designed for:

- the initial migration from pipe-delimited TXT to SQLite;
- all future SQLite schema changes;
- incremental, versioned, and reversible database evolution.

### Header

`libs/storage/MigrationEngine.h`

---

## Migration definition

```cpp
struct Migration {
    int version;       // Monotonic, >= 1
    std::string name;  // Human-readable unique identifier
    // Returns true on success, false on failure with diagnostics.
    std::function<bool(SQLiteDB& db, std::string& diagnostics)> up;
};
```

- `version` must be unique. Migrations are applied in version order.
- `name` is a human-readable label (e.g. `"create_meta_tables"`).
- `up` is the migration function. It receives the database connection
  and a diagnostics string to fill on error. It must return `true` on
  success.

---

## Engine API

### `void register_migration(Migration m)`

Registers a migration for later execution. Migrations may be registered
in any order — they are sorted by version when `migrate()` is called.

Duplicate versions are not detected at registration time. They are
detected during `migrate()` when the checksum of the second migration
does not match the stored checksum of the first.

### `int current_version(SQLiteDB& db)`

Returns the highest version number among all completed migrations.
Returns `0` if no migrations have been applied (fresh database).

### `bool migrate(SQLiteDB& db)`

Applies all pending migrations in version order.

Process:
1. Creates metadata tables (`schema_migrations`, `storage_meta`) if
   they do not exist.
2. Detects the current schema version.
3. For each registered migration:
   - If version <= current version: verify checksum match and skip.
   - If version > current version: apply the migration.
4. Returns `true` if all migrations completed.
5. Returns `false` on first failure (fail-fast).

### `std::string last_error() const`

Returns the human-readable error message from the last failed
`migrate()` call. Empty string if the last call succeeded.

---

## Migration lifecycle

### States

Each migration transitions through the following states, recorded in the
`schema_migrations` table:

```
pending → running → completed
                   → failed
```

| State | Meaning |
|-------|---------|
| `pending` | Not yet applied (default on INSERT for new migrations) |
| `running` | Currently executing (set before the migration function runs) |
| `completed` | Applied successfully |
| `failed` | Migration function returned false or threw |

### Checksum verification

Each migration has a SHA-256 checksum computed from its version number
and name (`sha256(version + ":" + name)`). The checksum is stored when
the migration is first applied. On subsequent runs, the stored checksum
is compared with the current migration definition:

- **Match:** migration is skipped (already applied, unchanged).
- **Mismatch:** the migration definition changed after it was applied.
  This is an error — `migrate()` returns `false` with a diagnostic.

### Interrupted migration

If the process crashes during a migration that has `status = 'running'`:

- On restart, the engine detects the `running` status.
- The migration is reset to `pending` and retried.
- The migration function must be idempotent for safe retry.

### Failed migration

If a migration returns `false` or throws:

- The migration's schema changes are rolled back via `SAVEPOINT`.
- The migration's status is set to `failed` with diagnostics.
- Subsequent `migrate()` calls refuse to proceed — manual recovery
  is required.

### Idempotency

- Already-completed migrations are never re-applied.
- The checksum prevents accidental re-application with a changed
  definition.
- Interrupted migrations (`running` status) are retried.

---

## Metadata tables

### `schema_migrations`

Records every migration attempt.

```sql
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    checksum    TEXT NOT NULL,       -- SHA-256 of version + name
    started_at  TEXT NOT NULL,       -- ISO-8601 UTC
    completed_at TEXT,               -- ISO-8601 UTC, NULL if not complete
    status      TEXT NOT NULL DEFAULT 'pending',  -- see states above
    diagnostics TEXT                 -- error details for failed migrations
);
```

### `storage_meta`

Key-value store for Storage subsystem metadata.

```sql
CREATE TABLE IF NOT EXISTS storage_meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

Expected keys (populated by the startup migration gate, not by
MigrationEngine itself):

| Key | Value | Description |
|-----|-------|-------------|
| `storage_backend` | `"sqlite"` | Active storage engine |
| `schema_version` | integer | Current schema version |
| `migration_state` | `"completed"` or `"failed"` | Overall migration result |
| `source_version` | `"0.6.0"` | Version migrated from |
| `migration_completed_at` | ISO-8601 | Migration completion timestamp |

---

## Recovery

### After a failed migration

The administrator must:

1. Read the error diagnostics from the log or `schema_migrations` table.
2. Fix the root cause (corrupted source data, migration bug, etc.).
3. If needed, manually update the `schema_migrations` row:
   - `UPDATE schema_migrations SET status = 'pending' WHERE version = N`
4. Restart the daemon. The engine retries the failed migration.

### After checksum mismatch

Checksum mismatch is a safety mechanism. The administrator must:

1. Determine whether the migration definition changed intentionally.
2. If intentional and safe, manually update the stored checksum:
   - `UPDATE schema_migrations SET checksum = '<new>' WHERE version = N`
3. Or restore the original migration definition.

---

## Transaction model

Each migration runs within a `SAVEPOINT`. If the migration fails:

1. `ROLLBACK TO SAVEPOINT` reverts the migration's schema changes.
2. `RELEASE SAVEPOINT` cleans up the savepoint.
3. The migration is marked as `failed`.

If the migration succeeds:

1. `RELEASE SAVEPOINT` commits the changes.
2. The migration is marked as `completed`.

This approach allows multiple migrations in a single session without
requiring an outer transaction. Each migration is independently
atomic.

---

## Future extension rules

1. Every new schema change MUST be a new `Migration` with an
   incremented version number.
2. Migration versions MUST be monotonically increasing.
3. Migration functions MUST be idempotent (safe to retry after
   interruption).
4. Migration functions SHOULD use `CREATE TABLE IF NOT EXISTS`
   and other idempotent DDL where possible.
5. Migration definitions MUST NOT be modified after they have been
   applied to any database. Use a new migration version instead.
6. The `storage_meta` table is available for storing migration
   metadata, but MigrationEngine itself only manages
   `schema_migrations`.
