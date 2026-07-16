# Migration Engine API

## Purpose

`containercp::storage::MigrationEngine` is a reusable, versioned schema
migration framework for the Storage subsystem.

It manages the lifecycle of database schema changes from initial creation
through successive versions. It is designed for:

- the initial migration from pipe-delimited TXT to SQLite;
- all future SQLite schema changes;
- incremental, versioned, and deterministic database evolution.

### Header

`libs/storage/MigrationEngine.h`

---

## Migration definition

```cpp
struct Migration {
    int version;       // >= 1, must be unique.
    std::string name;  // Required — non-empty human-readable label.
    std::string descriptor;  // Required — content fingerprint (see below).
    std::function<bool(SQLiteDB& db, std::string& diagnostics)> up;
};
```

**Registration requirements (all mandatory):**

| Field | Requirement | Error if |
|-------|------------|----------|
| `version` | >= 1 | version < 1 |
| `name` | non-empty | empty string |
| `descriptor` | non-empty | empty string |
| `up` | callable | empty `std::function` |

If any requirement is violated, `register_migration()` sets the engine
to a **permanently invalid state**. The engine never recovers:

- subsequent `register_migration()` calls are silently **ignored** —
  the migration is not registered and the original error is preserved;
- subsequent `migrate()` calls return `false` and preserve the original
  error in `last_error()`.

The caller must discard the engine and construct a new
`MigrationEngine`.

- `version` must be unique across all registered migrations. Each
  version may appear at most once regardless of descriptor. Duplicate
  versions are detected at `migrate()` time.
- `descriptor` uniquely identifies the migration's implementation. It
  MUST change when the migration logic changes. The recommended
  approach is to use the SQL content as the descriptor. The checksum
  is `SHA-256(version + ":" + descriptor)`.

---

## Checksum algorithm

```
checksum = SHA-256(version + ":" + descriptor)
```

The checksum is computed from the deterministic `descriptor` field,
not from the `up` function itself (which cannot be safely hashed in
C++).

**Rule:** The developer MUST change `descriptor` whenever the migration
logic changes. Using the SQL content as the descriptor is the
recommended approach — any SQL change automatically produces a
different checksum.

---

## Duplicate version policy

Duplicate versions are **always rejected** and put the engine into a
**permanently invalid state** (same as a registration validation
failure). Each migration version may appear at most once, regardless
of descriptor.

This is enforced before any migration is applied. The error message
identifies both conflicting migration names:

```
Duplicate migration version N (second_name) — version N is already
registered by 'first_name'. Each version must appear exactly once.
```

If a migration needs to be changed, create a new migration with an
incremented version number. Do not attempt to register the same version
twice.

---

## Engine API

### `void register_migration(Migration m)`

Registers a migration for later execution. Migrations may be registered
in any order — they are sorted by version when `migrate()` is called.

**Validation:** If the migration definition fails validation (version < 1,
empty name, empty descriptor, or missing callback), the engine enters a
**permanently invalid state**. Subsequent `register_migration()` calls
are silently **ignored** (the migration is not registered; the original
error is preserved). Subsequent `migrate()` calls return `false`.
The caller must discard the engine and construct a new
`MigrationEngine`.

### `int current_version(SQLiteDB& db)`

Returns the highest version number among all completed migrations.
Returns `0` if no migrations have been applied (fresh database).

### `bool migrate(SQLiteDB& db)`

Applies all pending migrations in version order.

Process:
1. Sort migrations by version.
2. Reject duplicate versions with different descriptors.
3. Create metadata tables (`schema_migrations`, `storage_meta`) if
   they do not exist.
4. Detect the current schema version.
5. For each registered migration:
   - If version <= current version: verify checksum match and skip.
   - If version > current version: apply the migration.
6. Returns `true` if all migrations completed.
7. Returns `false` on first failure (fail-fast).

### `std::string last_error() const`

Returns the human-readable error message from the last failed
`migrate()` call. Empty string if the last call succeeded.

---

## Migration lifecycle

### States

```
pending → running → completed
                   → failed
```

| State | Meaning |
|-------|---------|
| `pending` | Not yet applied. Default on INSERT for new migrations. |
| `running` | Currently executing. Set before the migration function runs. |
| `completed` | Applied successfully. |
| `failed` | Migration function returned false or threw. Manual recovery required. |

### Checksum verification

On each `migrate()` call, every registered migration with
`version <= current_version` is checked against the stored checksum:

- **Checksum match:** migration was already applied and the definition
  has not changed. Skipped silently.
- **Checksum mismatch:** the migration definition changed after it was
  applied. This is an error — `migrate()` returns `false`.
  The administrator must investigate whether the change was intentional.
  If it was, the correct fix is to add a NEW migration with an
  incremented version, NOT to modify the existing one.

### Interrupted migration (status = `running`)

If the process crashes during a migration (status `running`):

1. On restart, `migrate()` detects the `running` status.
2. The migration is reset to `pending`.
3. `apply_one()` treats the existing record as a retry: it UPDATEs
   the record back to `running` and re-executes the migration.
4. The migration function MUST be idempotent for safe retry.

This is the ONLY automatic retry case.

### Failed migration (status = `failed`)

If a migration returns `false` or throws:

1. The migration's schema changes are rolled back via SAVEPOINT.
2. The migration is marked as `failed` with diagnostics.
3. Subsequent `migrate()` calls refuse to proceed until the
   `schema_migrations` row is manually reset to `pending` by an
   administrator (emergency recovery only — see Recovery below).

### Checksum mismatch

A checksum mismatch means the migration definition was changed after
it was already applied. This is NOT a retry scenario. The engine
refuses to start. The correct resolution is to add a new migration
with an incremented version.

---

## Recovery policy

### Standard recovery (after operational failure)

1. Read the error diagnostics from the log or `schema_migrations.diagnostics`.
2. Fix the root cause (source data issue, environment problem, etc.).
3. If the migration failed with `status = 'failed'`, manually reset it:
   ```sql
   UPDATE schema_migrations SET status = 'pending', diagnostics = NULL
   WHERE version = <N>;
   ```
4. Restart the daemon. The engine retries the migration.

This manual SQL is an **emergency administrative procedure**, not a
standard workflow. It should only be performed after understanding why
the migration failed and confirming the fix.

### Emergency recovery (after checksum mismatch)

A checksum mismatch indicates that the migration definition was
modified after it was applied. This should never happen in normal
operation. If it does:

1. Determine whether the migration definition should have changed.
2. If a new migration is appropriate, add it with an incremented
   version — do not modify the existing migration.
3. If the checksum must be updated (exceptional circumstances only),
   this is an emergency database operation:
   ```sql
   UPDATE schema_migrations SET checksum = '<new_checksum>' WHERE version = <N>;
   ```
   This is NOT a standard procedure — it bypasses a safety mechanism.

### Automatic retry is limited to interrupted migrations

| Scenario | Automatic retry? | Behaviour |
|----------|-----------------|-----------|
| `running` status (crash) | Yes | Reset to `pending`, re-apply |
| `failed` status | No | Refuse until manually reset |
| Checksum mismatch | No | Refuse — requires investigation |
| `completed` (normal) | N/A | Skipped |

---

## Metadata tables

### `schema_migrations`

Records every migration attempt.

```sql
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    checksum    TEXT NOT NULL,       -- SHA-256 of version + descriptor
    started_at  TEXT NOT NULL,       -- ISO-8601 UTC
    completed_at TEXT,               -- ISO-8601 UTC, NULL if not complete
    status      TEXT NOT NULL DEFAULT 'pending',
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

## Transaction model

Each migration runs within a SAVEPOINT. If the migration fails:

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
3. The `descriptor` MUST change when the migration logic changes.
4. Migration functions SHOULD be idempotent (safe to retry after
   interruption).
5. Migration functions SHOULD use `CREATE TABLE IF NOT EXISTS`
   where appropriate.
6. Migration definitions MUST NOT be modified after they have been
   applied to any database. Use a new migration version instead.
