# ARCH-008 — SQLite Storage Foundation Implementation Plan

> **Based on:** `planning/proposals/ARCH-008-SQLite-Storage-Foundation.md`
>
> **Status:** Phase 1 complete. Phases 2–14 pending.
> **Total phases:** 15 (Phase 0 through Phase 14)
> **Definition of Done:** see Phase 14
> **Migration test data:**
>   1. Small synthetic edge-case fixtures (`tests/fixtures/v0.6.0/normal/`,
>      `legacy/`, `sentinels/`, `malformed/`)
>   2. Anonymized production-derived fixtures
>      (`tests/fixtures/v0.6.0/production_derived/`)
>   3. Temporary raw production snapshot (local only, never committed)

---

## Phase Dependencies Overview

```
Phase 0 (fixtures)
  └──> Phase 1 (SQLite wrapper)
        └──> Phase 2 (connection pool)
              └──> Phase 3 (migration engine)
                    └──> Phase 4 (schema)
                          ├──> Phase 5 (core subset storage)
                          │     └──> Phase 6 (remaining core)
                          │           └──> Phase 7 (mail + SSL)
                          └──> Phase 8 (TXT importer)
                                └──> Phase 9 (verification)
                                      └──> Phase 10 (backup archive)
                                            └──> Phase 11 (startup gate)
                                                  └──> Phase 12 (backup/restore)
                                                        └──> Phase 13 (cleanup)
                                                              └──> Phase 14 (validation)
```

---

## Phase 0 — Baseline and Safety Fixtures

**Objective:** Confirm the current test baseline; capture representative v0.6.0 legacy TXT fixtures; inventory every storage file and serialization version. No SQLite production behavior.

### Files expected to change

| File | Change |
|------|--------|
| `tests/test_fixtures/` | NEW directory |
| `tests/test_fixtures/v0.6.0/nodes.db` | NEW — 1 local node |
| `tests/test_fixtures/v0.6.0/sites.db` | NEW — 3 sites (2 normal + 1 with php_mail_enabled) |
| `tests/test_fixtures/v0.6.0/sites_legacy_5field.db` | NEW — legacy 5-field format |
| `tests/test_fixtures/v0.6.0/domains.db` | NEW — domains with site_id=0 sentinel |
| `tests/test_fixtures/v0.6.0/users.db` | NEW — admin user |
| `tests/test_fixtures/v0.6.0/ssl_certificates.db` | NEW — both legacy 4-field and current format |
| `tests/test_fixtures/v0.6.0/ssl_certificates_legacy.db` | NEW — old format for migration tests |
| `tests/test_fixtures/v0.6.0/mail_domains.db` | NEW — current 12-field format |
| `tests/test_fixtures/v0.6.0/mail_domains_legacy.db` | NEW — old 10-field format |
| `tests/test_fixtures/v0.6.0/mail_mailboxes.db` | NEW — 2 mailboxes with password hashes |
| `tests/test_fixtures/v0.6.0/mail_aliases.db` | NEW — 1 alias |
| `tests/test_fixtures/v0.6.0/access_users.db` | NEW — 1 access user with password hash |
| `tests/test_fixtures/v0.6.0/access_grants.db` | NEW — 1 grant |
| `tests/test_fixtures/v0.6.0/reverse_proxies.db` | NEW — 3 proxies (2 site + 1 admin panel site_id=0) |
| `tests/test_fixtures/v0.6.0/profiles.db` | NEW — 5 profiles |
| `tests/test_fixtures/v0.6.0/php_versions.db` | NEW — 3 PHP versions |
| `tests/test_fixtures/v0.6.0/databases.db` | NEW — 2 databases with plaintext passwords |
| `tests/test_fixtures/v0.6.0/backups.db` | NEW — 2 backups |
| `tests/test_fixtures/v0.6.0/auth_users.db` | NEW — 1 admin user with password hash |
| `tests/test_fixtures/v0.6.0/mail_state.db` | NEW — "inactive" |
| `tests/test_fixtures/v0.6.0/mail_smarthost.db` | NEW — smarthost config with password |
| `tests/test_fixtures/v0.6.0/malformed/` | NEW directory |
| `tests/test_fixtures/v0.6.0/malformed/wrong_field_count.db` | NEW |
| `tests/test_fixtures/v0.6.0/malformed/empty_file.db` | NEW |
| `tests/test_fixtures/v0.6.0/malformed/duplicate_id.db` | NEW |
| `tests/test_fixtures/v0.6.0/malformed/invalid_int.db` | NEW |
| `tests/test_fixtures/v0.6.0/malformed/multiline_corruption.db` | NEW |
| `tests/test_fixtures/v0.6.0/empty/` | NEW — empty directories |
| `tests/test_fixtures/README.md` | NEW — documents fixture conventions |
| `tests/test_storage.cpp` | Verify current baseline, add fixture loader helpers |

### Interfaces added or modified

- None. This phase adds only test fixtures and test infrastructure.

### Data and schema impact

- None. No production code changes.

### Unit tests

- [ ] Document current test baseline: `./build-release/containercp_tests` count.
- [ ] Add `load_fixture<T>(path)` helper that uses the current `Storage` TXT parser to load fixture files.
- [ ] Verify every fixture file is parseable by the current `Storage::load_*()` methods.
- [ ] Verify malformed fixtures are rejected by the current parser.
- [ ] Verify empty fixture directories produce empty vectors.

### Integration tests

- [ ] No integration tests in this phase.

### Manual validation

- [ ] `xxd` or `od` inspection of fixture files confirms no hidden characters.
- [ ] Fixture files with password hashes use placeholder non-production values.

### Rollback method

- N/A. Only test fixtures added.

### Known risks

- None.

### Non-goals

- No SQLite code.
- No production code changes.
- No CMake changes.

### Completion checklist

- [ ] All 19 fixture types created (normal + legacy variants).
- [ ] 5 malformed fixture variants created.
- [ ] Baseline test count documented.
- [ ] Fixture loader works.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase0-baseline-fixtures`

---

## Phase 1 — SQLite Dependency and Low-Level Wrapper

**Objective:** Add SQLite3 build dependency; introduce a small RAII SQLite wrapper; implement connection opening, closing, prepared statements, binding, stepping, and error mapping; configure approved PRAGMAs. No existing resource uses SQLite yet.

### Files expected to change

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add `find_package(SQLite3 REQUIRED)`, `target_link_libraries(... SQLite::SQLite3)` for both `containercpd` and `containercp_tests` |
| `scripts/install.sh` | Add `libsqlite3-dev` to `apt-get install` line |
| `libs/storage/SQLiteWrapper.h` | NEW — RAII wrapper |
| `libs/storage/SQLiteWrapper.cpp` | NEW — implementation |
| `tests/test_storage.cpp` | Add SQLite wrapper tests |

### SQLiteWrapper API

```cpp
namespace containercp::storage {

class SQLiteDB {
public:
    SQLiteDB() = default;
    ~SQLiteDB();

    SQLiteDB(SQLiteDB&&) noexcept;
    SQLiteDB& operator=(SQLiteDB&&) noexcept;

    // Open/close
    bool open(const std::string& path);
    bool close();
    bool is_open() const;

    // Statement helpers
    bool exec(const std::string& sql);
    bool prepare(const std::string& sql);

    // Binding
    bool bind_int(int index, int64_t value);
    bool bind_text(int index, const std::string& value);
    bool bind_null(int index);

    // Stepping
    bool step();              // returns true if ROW, false if DONE
    bool step_row();          // same as step but throws on error
    int column_count();
    int64_t column_int(int index);
    std::string column_text(int index);
    bool column_is_null(int index);

    // Transaction
    bool begin_immediate();
    bool commit();
    bool rollback();

    // PRAGMAs
    bool apply_pragmas();

    // Error
    std::string error_message() const;
    int error_code() const;

    // Access to raw handle for MigrationEngine
    sqlite3* handle() const;

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

} // namespace containercp::storage
```

### PRAGMAs applied on every open

```cpp
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;
PRAGMA synchronous = FULL;
PRAGMA busy_timeout = 5000;
PRAGMA wal_autocheckpoint = 1000;
PRAGMA journal_size_limit = 67108864;
PRAGMA cache_size = -8000;
```

### Expected tests

- [ ] Open and close (valid path).
- [ ] Open with invalid path → error.
- [ ] Double open → idempotent.
- [ ] Double close → idempotent.
- [ ] `exec()` with CREATE TABLE.
- [ ] Prepare + bind_int + bind_text + bind_null.
- [ ] Step: INSERT then SELECT.
- [ ] Step: no more rows → false.
- [ ] Column access by index.
- [ ] `begin_immediate()` + `commit()`.
- [ ] `rollback()` reverts uncommitted INSERT.
- [ ] `PRAGMA foreign_keys = ON` verified by failing FK violation.
- [ ] `PRAGMA journal_mode` returns `wal`.
- [ ] `busy_timeout` verified by concurrent access test.
- [ ] Error message contains meaningful text on invalid SQL.
- [ ] Move constructor leaves source empty.
- [ ] Destructor closes handle.

### Integration tests

- [ ] None in this phase (no cross-module integration yet).

### Manual validation

- [ ] Build succeeds with SQLite3 linked.
- [ ] `ldd build-release/containercpd` shows `libsqlite3.so`.

### Rollback method

- `git revert` the CMakeLists.txt + wrapper changes.

### Known risks

- SQLite3 version on Debian 13 (Trixie) must provide `sqlite3_prepare_v2`,
  `sqlite3_bind_*`, `sqlite3_step`, `sqlite3_column_*`. Confirmed: Debian 13
  ships SQLite 3.44+ which includes all required APIs.

### Non-goals

- No connection pool.
- No resource-specific SQL.
- No migration engine.
- No business logic.

### Completion checklist

- [ ] CMakeLists.txt updated and builds.
- [ ] install.sh updated.
- [ ] RAII wrapper passes all unit tests.
- [ ] PRAGMAs verified.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase1-sqlite-wrapper`

---

## Phase 2 — Connection and Transaction Infrastructure

**Objective:** Implement one serialized write connection; implement exactly three bounded read connections; add connection leasing and deterministic shutdown; add the minimum transaction API required by Storage and Migration Engine. No resource migration yet.

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/Storage.h` | Add `begin_transaction()`, `commit_transaction()`, `rollback_transaction()`, `backup()` |
| `libs/storage/ConnectionPool.h` | NEW |
| `libs/storage/ConnectionPool.cpp` | NEW |
| `libs/storage/SQLiteWrapper.h` | No change (reused from Phase 1) |
| `tests/test_storage.cpp` | Add connection pool tests |

### ConnectionPool API

```cpp
namespace containercp::storage {

class ConnectionPool {
public:
    // kReadPoolSize is a named constant = 3.
    static constexpr int kReadPoolSize = 3;

    ConnectionPool();
    ~ConnectionPool();

    // Initialize: open write conn + 3 read conns.
    bool initialize(const std::string& db_path);

    // Write connection (serialized by mutex).
    SQLiteDB& write_connection();
    void lock_write();
    void unlock_write();

    // Read connection leasing.
    SQLiteDB* lease_read();       // blocks if all busy
    void return_read(SQLiteDB* db);

    // Shutdown: close all connections.
    void shutdown();

    // Backup: online backup API.
    bool backup(const std::string& dest_path);

private:
    std::unique_ptr<SQLiteDB> write_conn_;
    std::mutex write_mutex_;
    std::array<std::unique_ptr<SQLiteDB>, kReadPoolSize> read_conns_;
    std::atomic<int> read_next_{0};
    std::array<std::atomic<bool>, kReadPoolSize> read_in_use_;
};

} // namespace containercp::storage
```

### Storage transaction API (additions)

```cpp
// In Storage.h:
bool begin_transaction();     // BEGIN IMMEDIATE on write connection
bool commit_transaction();    // COMMIT
bool rollback_transaction();  // ROLLBACK
bool backup(const std::string& dest_path);  // Online Backup API
```

### Expected tests

- [ ] Initialize pool: opens 1 write + 3 read connections.
- [ ] Write mutex: concurrent write attempts block correctly.
- [ ] Lease 3 reads simultaneously → all succeed.
- [ ] Lease 4th read when 3 are busy → blocks or times out (verify timeout, not hang).
- [ ] Return read → connection becomes available for next lease.
- [ ] Shutdown with all connections returned → clean close.
- [ ] Shutdown with outstanding leases → force close (log warning).
- [ ] `begin_transaction()` + multiple writes + `commit()`.
- [ ] `begin_transaction()` + write + `rollback()` → no change on read-back.
- [ ] Nested `begin_transaction()` → no-op (idempotent).
- [ ] Every connection has correct PRAGMAs (verify `journal_mode`, `foreign_keys`).
- [ ] `busy_timeout` fires on deliberate lock contention.
- [ ] Backup creates valid SQLite file (verified by `PRAGMA integrity_check` on copy).
- [ ] Backup during concurrent reads succeeds.
- [ ] Backup during write succeeds (write waits).

### Integration tests

- [ ] None in this phase.

### Manual validation

- [ ] Review valgrind or ASAN for connection leaks.

### Rollback method

- `git revert` phase 2 commit. Phase 1 wrapper remains.

### Known risks

- Connection pool starvation under high load: mitigated by `busy_timeout=5000`.
- Mutex deadlock: use `std::lock_guard` consistently, never recursive.

### Non-goals

- No resource-specific storage methods.
- No migration engine.
- No dynamic pool sizing.

### Completion checklist

- [ ] Pool opens correct number of connections.
- [ ] Transaction API methods work.
- [ ] Backup method works.
- [ ] Shutdown is clean.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase2-connection-pool`

---

## Phase 3 — Migration Engine and Metadata Schema

**Objective:** Implement versioned schema migrations; create `storage_meta` and `schema_migrations` tables; implement migration ordering, checksums, statuses, retry semantics, and fail-fast errors. Do not create business tables yet.

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/MigrationEngine.h` | NEW |
| `libs/storage/MigrationEngine.cpp` | NEW |
| `libs/storage/Storage.h` | May add migration-related accessors |
| `tests/test_migration.cpp` | NEW — migration engine tests |

### MigrationEngine API

```cpp
namespace containercp::storage {

struct Migration {
    int version;       // Monotonic, >= 1
    std::string name;  // Human-readable, unique
    // Returns true on success, false on failure with diagnostics set.
    std::function<bool(SQLiteDB& db, std::string& diagnostics)> up;
};

class MigrationEngine {
public:
    MigrationEngine();

    void register_migration(Migration m);
    int current_version(SQLiteDB& db);
    bool migrate(SQLiteDB& db);

    // Diagnostics after failed migration.
    std::string last_error() const;

private:
    bool ensure_meta_tables(SQLiteDB& db);
    bool apply_one(SQLiteDB& db, const Migration& m, int version);
    bool verify_checksum(SQLiteDB& db, const Migration& m, int version);
    std::string compute_checksum(const Migration& m);

    std::vector<Migration> migrations_;
    std::string last_error_;
};

} // namespace containercp::storage
```

### Metadata tables

```sql
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    checksum    TEXT NOT NULL,
    started_at  TEXT NOT NULL,
    completed_at TEXT,
    status      TEXT NOT NULL DEFAULT 'pending',
    diagnostics TEXT
);

CREATE TABLE IF NOT EXISTS storage_meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

### Expected tests

- [ ] `ensure_meta_tables()` creates both tables.
- [ ] First migration from version 0 to version 1 succeeds.
- [ ] Second migration from 1 to 2 runs after first.
- [ ] Repeated `migrate()` after all completed → no-op.
- [ ] Migration with changed checksum → detection and refusal.
- [ ] Interrupted migration (`status = running` on restart) → retry if safe.
- [ ] Failed migration → status set to `failed`, diagnostics populated.
- [ ] Failed migration → subsequent `migrate()` refuses to proceed.
- [ ] Transaction rollback on migration failure.
- [ ] `storage_meta` keys written and read correctly.
- [ ] Duplicate migration version → rejected.
- [ ] Unknown migration status → handled gracefully.

### Integration tests

- [ ] None (no business schema yet).

### Manual validation

- [ ] Inspection of `schema_migrations` table after test run.

### Rollback method

- `git revert` phase 3 commit.

### Known risks

- Checksum algorithm choice: SHA-256 via OpenSSL (already a dependency).
- Time-based `started_at` may collide on rapid startup: mitigated by version uniqueness.

### Non-goals

- No business tables.
- No TXT data import.
- No startup integration.

### Completion checklist

- [ ] Migration engine works for empty → v1 → v2.
- [ ] Checksum verification works.
- [ ] All 4 status transitions tested.
- [ ] Fail-fast on checksum mismatch.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase3-migration-engine`

---

## Phase 4 — Approved SQLite Schema

**Objective:** Introduce the approved business tables and constraints; implement the exact foreign-key matrix; preserve valid sentinel and historical relationships; add approved indices. No jobs table. No PEM/private-key content columns.

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/SchemaMigrations.h` | NEW — all migration definitions |
| `libs/storage/SchemaMigrations.cpp` | NEW — register all migrations |
| `libs/storage/MigrationEngine.h` | No change |
| `tests/test_migration.cpp` | Add schema creation tests |

### Migration v1: Initial Schema

Creates all 17 business tables + `mail_config` table.

Tables (see proposal section 7.2 for full DDL):

1. `nodes`
2. `sites` (FK → nodes, index on node_id)
3. `users`
4. `domains` (FK → users, sites; indices on site_id, owner_id)
5. `php_versions`
6. `databases` (FK → users, sites; index on site_id)
7. `backups` (FK → sites, users; index on site_id)
8. `ssl_certificates` (FK → domains; index on domain_id)
9. `mail_domains` (NO FK on domain_id/site_id — sentinel 0)
10. `mail_mailboxes` (FK → mail_domains; index on domain_id)
11. `mail_aliases` (FK → mail_domains; index on domain_id)
12. `access_users`
13. `access_grants` (FK → access_users, sites; indices on user_id, site_id)
14. `reverse_proxies` (NO FK on site_id — sentinel 0; index on site_id)
15. `profiles`
16. `auth_users`
17. `mail_config` (key-value)

### Migration v1: Seed data

- `storage_meta`: backend='sqlite', schema_version=1, migration_state='completed'
- No seed business data (that happens in startup, not in schema migration)

### Expected tests

- [ ] All 18 tables exist after migration.
- [ ] All indices exist (check via `PRAGMA index_list`).
- [ ] FK violations are rejected (INSERT with invalid site_id → error).
- [ ] FK violations with sentinel 0 are allowed (site_id=0, domain_id=0).
- [ ] `ON DELETE RESTRICT` prevents deletion of referenced row.
- [ ] No `jobs` table exists.
- [ ] No BLOB/PEM columns exist.
- [ ] `mail_config` table accepts arbitrary key-value pairs.
- [ ] Re-running migration v1 is a no-op (idempotent).
- [ ] Schema can be verified via `PRAGMA schema_version`.

### Integration tests

- [ ] Chain migration v1 (schema) from a fresh database.
- [ ] Verify no business logic runs during schema migration.

### Manual validation

- [ ] SQLite `.schema` output matches intended DDL.
- [ ] `PRAGMA foreign_key_check` returns no rows on empty database.

### Rollback method

- Drop and recreate. Migration engine ensures idempotent re-apply.

### Known risks

- FK constraints with `ON DELETE RESTRICT` may cause unexpected failures
  in existing business logic that deleted referenced rows first.
  Mitigation: current business logic already handles lifecycle order
  (e.g., `SiteRemoveOperation` removes domains, databases, backups,
  access_grants, reverse_proxies before removing the site).

### Non-goals

- No jobs table (reserved for ARCH-010).
- No data import.
- No startup integration.

### Completion checklist

- [ ] All 18 tables created with correct columns, types, defaults.
- [ ] FK matrix matches proposal section 8.
- [ ] Indices match proposal section 9.
- [ ] Sentinel values accepted.
- [ ] No jobs table.
- [ ] No PEM columns.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase4-sqlite-schema`

---

## Phase 5 — SQLite Storage Backend for Non-Sensitive Core Resources

**Objective:** Implement SQLite persistence first for a small low-risk
resource subset (nodes, PHP versions, profiles). Preserve existing
Storage-facing behavior. Keep legacy TXT backend active for migration
fixtures and compatibility tests.

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/Storage.h` | Add SQLiteStorage member alongside TXT (dual for now) |
| `libs/storage/Storage.cpp` | Replace `save_nodes`, `load_nodes`, `save_php_versions`, `load_php_versions`, `save_profiles`, `load_profiles` with SQLite implementation |
| `libs/storage/SQLiteStorage.h` | NEW — SQLite helper methods |
| `libs/storage/SQLiteStorage.cpp` | NEW — implementations |
| `libs/storage/ConnectionPool.h` | No change |
| `tests/test_storage.cpp` | Add SQLite save/load round-trip tests |

### Chosen resource subset

- **Nodes:** simple, 3 fields, no FKs
- **PHP Versions:** simple, 5 fields, no FKs
- **Profiles:** simple, 9 fields, no FKs

### Expected tests

- [ ] `save_nodes` + `load_nodes` round trip.
- [ ] Empty nodes → empty vector.
- [ ] Update node → save + load reflects change.
- [ ] Delete node → not present after save.
- [ ] Multiple nodes preserved.
- [ ] PHP versions: same round trip.
- [ ] Profiles: same round trip.
- [ ] Special characters in profile names (quotes, backslashes).
- [ ] Transaction rollback reverts save batch.
- [ ] Auto-increment IDs work across saves.
- [ ] Existing TXT backend still works for other resource types (sites, users).

### Integration tests

- [ ] No integration tests in this phase (subset too small).

### Manual validation

- [ ] Run existing test suite: all 257 tests must pass (TXT-backed resources unchanged).
- [ ] Verify SQLite database after test run with `.dump`.

### Rollback method

- `git revert` phase 5 commit. SQLite wrapper and schema remain for future phases.

### Known risks

- Dual-backend complexity: mitigate by keeping the TXT backend untouched for
  all non-migrated resources. Only 3 resource types switch to SQLite.

### Non-goals

- No migration importer.
- No startup gate changes.
- No legacy TXT removal.

### Completion checklist

- [ ] Nodes, PHP versions, profiles read/write from SQLite.
- [ ] All other resources still read/write from TXT.
- [ ] Existing test suite passes.
- [ ] No SQLite C API calls outside Storage implementation.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase5-core-subset-storage`

---

## Phase 6 — Remaining Core Resource Storage

**Objective:** Migrate the next logical set of related resource types
to SQLite: users, sites, domains, databases, reverse proxies, access
users, access grants.

### Subphases

If the diff becomes too large, split into:

- **Phase 6a:** users, sites, domains (depends on each other)
- **Phase 6b:** databases, reverse proxies
- **Phase 6c:** access users, access grants

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/Storage.cpp` | Replace `save_users/load_users`, `save_sites/load_sites`, `save_domains/load_domains`, `save_databases/load_databases`, `save_reverse_proxies/load_reverse_proxies`, `save_access_users/load_access_users`, `save_access_grants/load_access_grants` |
| `libs/storage/SQLiteStorage.h` | Add helper methods for these types |
| `libs/storage/SQLiteStorage.cpp` | Implementations |
| `tests/test_storage.cpp` | Add round-trip tests |

### FK considerations

- Sites FK to nodes.
- Domains FK to sites and users (sentinel 0 allowed).
- Databases FK to sites and users (sentinel 0 allowed).
- Reverse proxies: site_id=0 sentinel, no FK on site_id column.
- Access grants FK to access_users and sites.

### Expected tests

- [ ] Round trips for each type.
- [ ] Empty datasets return empty vectors.
- [ ] Updates and deletions persist.
- [ ] FK enforcement: INSERT with invalid site_id on domains → error.
- [ ] Sentinels: site_id=0, owner_id=0, node_id=0 accepted.
- [ ] Sensitive fields (db_password) stored but not exposed in test output.
- [ ] Existing manager and API regression tests pass.
- [ ] Cross-resource deletion restrictions enforced.

### Integration tests

- [ ] Full `ServiceRegistry` startup loading from SQLite (in test harness).

### Manual validation

- [ ] All 257 existing tests pass.

### Rollback method

- `git revert` subphase commit. Earlier subphases remain.

### Known risks

- `site_id = 0` in reverse_proxies: no FK constraint, must be documented.

### Non-goals

- No mail/SSL resources.
- No migration importer.

### Completion checklist

- [ ] All core resources read/write from SQLite.
- [ ] FK constraints verified.
- [ ] Sentinel values preserved.

### 🔴 STOP — review and commit after each subphase

**Expected commits:** `phase6a-core-sites-domains`, `phase6b-core-databases-proxies`, `phase6c-core-access`

---

## Phase 7 — Mail and SSL Metadata Storage

**Objective:** Migrate SSL certificate metadata, mail domains, mailboxes,
mail aliases, mail module state, and smarthost configuration to SQLite.

### Requirements

- SSL PEM and private-key files stay on disk (section 20 of proposal).
- DKIM private keys stay on disk.
- DKIM path and public DNS value may be stored in SQLite.
- Password hashes may be stored in SQLite.
- Smarthost password remains sensitive and redacted from output.
- Missing cryptographic files are detectable without storing their content.

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/Storage.cpp` | Replace `save_ssl_certificates/load_ssl_certificates`, `save_mail_domains/load_mail_domains`, `save_mailboxes/load_mailboxes`, `save_mail_aliases/load_mail_aliases`, `save_mail_module_state/load_mail_module_state`, `save_mail_smarthost/load_mail_smarthost` |
| `libs/storage/SQLiteStorage.h` | Add helper methods |
| `libs/storage/SQLiteStorage.cpp` | Implementations |
| `tests/test_storage.cpp` | Add round-trip tests |
| `tests/test_cert_store.cpp` | Add metadata-to-file consistency tests |

### Legacy format handling

- SSL certs: detect legacy 4-field vs new 14+ field format (pipe count).
- Mail domains: detect legacy 10-field vs current 12-field format (pipe count).

### Expected tests

- [ ] SSL cert round trip (current format).
- [ ] SSL cert legacy format import.
- [ ] Mail domain round trip (current format).
- [ ] Mail domain legacy format import.
- [ ] Mailbox round trip (including password hash).
- [ ] Mail alias round trip.
- [ ] Mail module state round trip.
- [ ] Smarthost config round trip.
- [ ] No PEM content in SQLite (verify via column inspection).
- [ ] No private key content in SQLite.
- [ ] Password hash not exposed through `JsonFormatter` or API.
- [ ] Smarthost password not exposed through API or logs.
- [ ] Missing certificate file → diagnostic without file content.
- [ ] Missing DKIM key file → diagnostic without file content.

### Integration tests

- [ ] Full mail module startup loading from SQLite.
- [ ] SSL CertificateStore loading from SQLite.

### Manual validation

- [ ] Run `SELECT * FROM ssl_certificates` — no PEM content visible.
- [ ] API responses for mail/ssl redact sensitive fields.

### Rollback method

- `git revert` phase 7 commit.

### Known risks

- Legacy format detection: existing pipe-counting logic preserved in migration parser.

### Non-goals

- No encryption of sensitive fields (deferred to Secrets epic).
- No migration importer for these types (Phase 8 covers that).

### Completion checklist

- [ ] All mail and SSL resources read/write from SQLite.
- [ ] No PEM/private-key blobs in SQLite.
- [ ] Sensitive fields redacted in API and logs.
- [ ] Legacy format detection works.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase7-mail-ssl-storage`

---

## Phase 8 — Legacy TXT Importer

**Objective:** Implement strict import of all supported v0.6.0 TXT
formats; reuse or isolate existing parsers where safe; reject malformed
and ambiguous data; preserve original TXT files.

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/LegacyImporter.h` | NEW |
| `libs/storage/LegacyImporter.cpp` | NEW |
| `libs/storage/Storage.h` | May add migration accessor |
| `tests/test_migration.cpp` | Add import tests |

### LegacyImporter API

```cpp
namespace containercp::storage {

struct ImportResult {
    bool success = false;
    std::string resource_type;
    std::string source_file;
    int record_count = 0;
    std::string error;       // empty on success
    std::string diagnostics; // detailed on failure
};

class LegacyImporter {
public:
    LegacyImporter(const std::string& db_path, ConnectionPool& pool);

    // Import a single resource type.
    ImportResult import_nodes();
    ImportResult import_sites();
    ImportResult import_users();
    ImportResult import_domains();
    ImportResult import_php_versions();
    ImportResult import_profiles();
    ImportResult import_backups();
    ImportResult import_databases();
    ImportResult import_access_users();
    ImportResult import_auth_users();
    ImportResult import_reverse_proxies();
    ImportResult import_ssl_certificates();
    ImportResult import_mail_domains();
    ImportResult import_mail_mailboxes();
    ImportResult import_mail_aliases();
    ImportResult import_mail_config();

    // Import all types in dependency order.
    // Returns the first failure, or success if all pass.
    ImportResult import_all();

private:
    // Uses existing Storage TXT parsers for reading.
    storage::Storage txt_backend_;
    ConnectionPool& pool_;
    std::string db_path_;
};

} // namespace containercp::storage
```

### Expected tests

- [ ] Import nodes from synthetic fixtures.
- [ ] Import sites from synthetic fixtures (including legacy 5-field).
- [ ] Import SSL certs from synthetic fixtures (including legacy 4-field).
- [ ] Import mail domains from synthetic fixtures (including legacy 10-field).
- [ ] Import all types from **anonymized production-derived fixture** (`production_derived/`)
      and verify record counts match the production snapshot.
- [ ] Malformed field count → fail with line number.
- [ ] Duplicate ID → fail.
- [ ] Empty file → skip.
- [ ] Missing optional file → skip.
- [ ] Sentinel values (site_id=0, domain_id=0) preserved.
- [ ] Password hashes preserved correctly.
- [ ] Database passwords preserved correctly.
- [ ] Smarthost config preserved correctly.
- [ ] Partial import: import first 5 types, then fail on 6th. First 5 committed.

### Integration tests

- [ ] Import all 19 fixture files → verify SQLite contents match.

### Manual validation

- [ ] Run importer on a real v0.6.0 database directory (backup first!).

### Rollback method

- TXT files are never modified by the importer (read-only).
- SQLite can be deleted to revert.

### Known risks

- TXT file format may have undocumented variants not covered by fixtures.
  Mitigation: extensive fixture set covering all known historical formats.

### Non-goals

- No startup integration (Phase 11).
- No verification pipeline (Phase 9).
- No archive creation (Phase 10).

### Completion checklist

- [ ] All 19 resource types importable.
- [ ] Legacy format variants handled.
- [ ] Malformed data rejected with clear diagnostics.
- [ ] Import preserves sentinel values.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase8-legacy-importer`

---

## Phase 9 — Migration Verification

**Objective:** Implement the complete approved verification pipeline:
parse success, record counts, canonical field-by-field comparison,
per-resource SHA-256 checksums, `PRAGMA foreign_key_check`,
`PRAGMA integrity_check`, close and production reopen, reload through
the production Storage API.

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/Verification.h` | NEW |
| `libs/storage/Verification.cpp` | NEW |
| `libs/storage/MigrationEngine.h` | May call verification after import |
| `tests/test_migration.cpp` | Add verification tests |

### Verification API

```cpp
namespace containercp::storage {

struct VerificationResult {
    bool success = false;
    std::string resource_type;
    int txt_record_count = 0;
    int sqlite_row_count = 0;
    std::string txt_checksum;
    std::string sqlite_checksum;
    std::string canonical_example;  // for debugging
    std::vector<std::string> mismatches;
    std::string integrity_check_result;
    std::string fk_check_result;
};

class Verification {
public:
    Verification(ConnectionPool& pool);

    // Verify a single resource type.
    VerificationResult verify_type(
        const std::string& type_name,
        const std::vector<std::string>& txt_records_canonical,
        const std::string& txt_checksum,
        int txt_count
    );

    // Run full verification suite after all types imported.
    bool verify_all(
        const std::map<std::string, std::string>& txt_checksums,
        const std::map<std::string, int>& txt_counts
    );

    // SHA-256 helper.
    static std::string sha256(const std::string& data);

    // Canonical JSON representation of a resource.
    static std::string to_canonical_json(const site::Site& s);
    static std::string to_canonical_json(const domain::Domain& d);
    // ... one per resource type.
};

} // namespace containercp::storage
```

### Expected tests

- [ ] Successful equivalence on synthetic fixtures.
- [ ] Successful equivalence on **anonymized production-derived fixtures** (full 16-step pipeline).
- [ ] Count mismatch → detected and reported.
- [ ] Field mismatch → detected with field path.
- [ ] Checksum mismatch → verification fails.
- [ ] `PRAGMA foreign_key_check` failure → detected.
- [ ] `PRAGMA integrity_check` failure → detected.
- [ ] Production reopen: close pool, reopen, reload all types.
- [ ] Production reopen: re-verify integrity passes.
- [ ] Canonical JSON is stable (same input → same output).
- [ ] Canonical JSON for same data from TXT vs SQLite is identical.
- [ ] Sensitive fields (password_hash, db_password) included in canonical
  comparison (they must match, but SHA-256 covers them).

### Integration tests

- [ ] Full migration pipeline: import all fixtures → verify all → reopen.

### Manual validation

- [ ] Run on real v0.6.0 backup; verify checksums match.

### Rollback method

- SQLite can be deleted; TXT files unchanged.

### Known risks

- Canonical JSON comparison may be fragile for fields with
  insignificant formatting differences. Mitigation: parser-level
  normalization (trim whitespace, default empty string vs null).

### Non-goals

- No startup integration.
- No archive creation.

### Completion checklist

- [ ] All 16 verification steps implemented.
- [ ] Verification passes on all fixture datasets.
- [ ] Verification fails on deliberately corrupted datasets.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase9-verification`

---

## Phase 10 — Legacy Backup Archive

**Objective:** Create the versioned migration archive; create manifest
and SHA-256 files; verify copied data before activation; apply approved
ownership and permissions; prevent automatic deletion.

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/LegacyArchive.h` | NEW |
| `libs/storage/LegacyArchive.cpp` | NEW |
| `tests/test_migration.cpp` | Add archive tests |

### LegacyArchive API

```cpp
namespace containercp::storage {

struct ArchiveManifest {
    std::string migration_id;
    std::string source_version;
    std::string target_version;
    std::string migration_timestamp;
    std::vector<ArchiveFileEntry> files;
    std::string result;  // "success" or "failed"
    bool checksum_match;
    std::string integrity_check;
    std::string foreign_key_check;
};

struct ArchiveFileEntry {
    std::string filename;
    uint64_t size;
    std::string sha256;
    int record_count;
};

class LegacyArchive {
public:
    LegacyArchive(const std::string& db_path, logger::Logger& logger);

    // Create archive directory and copy all .db files.
    // Returns the archive path on success, empty on failure.
    std::string create_archive(const std::string& migration_id);

    // Generate manifest and write to archive.
    bool write_manifest(const ArchiveManifest& manifest);

    // Verify all files in archive match pre-computed checksums.
    bool verify_archive(const std::string& archive_path);

    // Remove .db files from active directory (after successful archive).
    bool remove_active_files();

    // Set archive permissions to 0700, files to read-only.
    bool set_permissions(const std::string& archive_path);

private:
    std::string db_path_;
    logger::Logger& logger_;
};

} // namespace containercp::storage
```

### Expected tests

- [ ] Archive creation: copies all `.db` files.
- [ ] Archive directory: named correctly with timestamp.
- [ ] Manifest: contains all expected fields.
- [ ] Manifest checksums match actual file checksums.
- [ ] `verify_archive()` passes on intact archive.
- [ ] `verify_archive()` fails on modified file.
- [ ] `remove_active_files()` removes only `.db` files, not `.db-wal` or other.
- [ ] Permissions: directory 0700, files read-only (0440).
- [ ] Archive collision: existing archive path → error, not overwrite.
- [ ] Insufficient disk space → error before partial copy.
- [ ] Source deletion blocked until successful archive + verification.
- [ ] Migration ID is a valid UUID.

### Integration tests

- [ ] Full pipeline: import → verify → archive → remove → reopen.

### Manual validation

- [ ] `ls -la` on archive directory confirms permissions.
- [ ] `sha256sum` on archived files matches manifest.

### Rollback method

- Files remain in active directory until `remove_active_files()`.
- Before that point: archive is redundant but harmless.

### Known risks

- Disk space: migration may fail if `/srv/containercp/database/` partition
  is full. Mitigation: check available space before starting archive.

### Non-goals

- No startup integration.
- No automatic deletion of archives.

### Completion checklist

- [ ] Archive created, verified, permissions set.
- [ ] Active `.db` files removed only after successful archive.
- [ ] Manifest complete and correct.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase10-legacy-archive`

---

## Phase 11 — Startup Migration Gate

**Objective:** Integrate the migration flow into daemon startup before
normal services initialize. No manager, API, UI, job, runtime action,
SSL renewal, or mail synchronization may start before successful storage
activation.

### Files expected to change

| File | Change |
|------|--------|
| `app/containercpd/main.cpp` | Add migration gate before `ServiceRegistry` initialization |
| `libs/core/StartupManager.h` | Add storage state detection methods |
| `libs/core/StartupManager.cpp` | Add migration gate logic |
| `libs/storage/MigrationGate.h` | NEW |
| `libs/storage/MigrationGate.cpp` | NEW |
| `tests/test_migration.cpp` | Add startup state tests |

### States handled

| State | Detection | Action |
|-------|-----------|--------|
| Fresh install | No `containercp.db`, no `.db` files | Create DB, apply schema, seed data |
| v0.6.0 upgrade | No `containercp.db`, `.db` files exist | Create DB, apply schema, import, verify, archive |
| Already migrated | `containercp.db` exists, no `.db` files, `storage_meta` valid | Normal startup |
| Corrupt migration | `containercp.db` exists, no `.db` files, `storage_meta` invalid | Fail-fast |
| Mixed, validated | Both exist, `storage_meta` valid, checksums match | Warn, archive `.db`, proceed |
| Mixed, failed | Both exist, `storage_meta` invalid | Fail-fast |
| Unsupported newer schema | Schema version > known max | Fail-fast with message |
| Archived TXT returned | `.db` files after migration complete | Log warning, ignore |

### Startup order (updated)

```
1. Acquire PID lock
2. Load minimal config (paths)
3. Detect storage state (above)
4. If migration needed:
   a. Initialize ConnectionPool (write + 3 reads)
   b. Open containercp.db or create it
   c. Apply schema migrations
   d. If legacy .db files exist: create archive → import → verify → activate
   e. Update storage_meta
   f. Close connection pool
5. Reopen connection pool in production mode
6. Perform production reopen validation
7. Initialize ServiceRegistry (managers, services)
8. Start jobs, runtime reconciliation, SSL renewal, mail sync
9. Start REST API and Web UI listeners
```

### Fail-fast rules

- If step 4 fails at any point: log diagnostic, exit.
- If step 6 fails: log diagnostic, exit.
- Steps 7–9 never execute before successful storage activation.

### Expected tests

- [ ] Fresh install → creates DB, seeds data, starts normally.
- [ ] v0.6.0 upgrade with fixtures → migrates, starts normally.
- [ ] Already migrated → skips migration, starts normally.
- [ ] Corrupted TXT fixture → fail-fast, no listeners start.
- [ ] `storage_meta` missing → fail-fast.
- [ ] Unsupported future schema version → fail-fast.
- [ ] Archived TXT files present → logs warning, ignores.
- [ ] After fixing source data, retry succeeds.
- [ ] No repeat import on second startup.
- [ ] Migration error message includes: migration ID, resource type,
  source filename, record/line, SQLite error, recovery direction.

### Integration tests

- [ ] Full daemon startup from each state in a test harness.

### Manual validation

- [ ] Run `containercpd` on a real v0.6.0 data directory (backup first!).
- [ ] Verify startup log shows successful migration.
- [ ] Verify `curl http://127.0.0.1:8080/api/health` returns 200.

### Rollback method

- Stop daemon, remove `containercp.db`, restore `.db` from archive.
- Start daemon → detects v0.6.0 state.

### Known risks

- Startup time may increase due to migration. Mitigation: benchmark
  and optimize import if needed.

### Non-goals

- No removing the TXT backend yet (Phase 13).
- No backup/restore commands (Phase 12).

### Completion checklist

- [ ] All 8 startup states handled.
- [ ] Fail-fast is enforced.
- [ ] No listeners start before storage is ready.
- [ ] Migration diagnostic output is actionable.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase11-startup-gate`

---

## Phase 12 — SQLite-Aware Backup and Restore

**Objective:** Implement or integrate the selected SQLite-aware backup
mechanism (Online Backup API); correctly handle WAL and SHM; verify
restored snapshots; preserve required filesystem artifacts separately.

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/Storage.cpp` | Ensure `backup()` method is wired to ConnectionPool |
| `libs/storage/ConnectionPool.cpp` | `backup()` implementation using `sqlite3_backup_*` |
| `libs/daemon/DaemonApp.cpp` | Add `storage backup <path>` and `storage restore <path>` CLI commands |
| `libs/api/ApiServer.cpp` | Add backup/restore API endpoints if needed |
| `tests/test_storage.cpp` | Add backup/restore tests |

### Backup mechanism

Uses SQLite Online Backup API:

```cpp
bool ConnectionPool::backup(const std::string& dest_path) {
    // 1. Lock write mutex
    // 2. sqlite3_backup_init(dest_db, "main", src_db, "main")
    // 3. sqlite3_backup_step(page_count)  -- copy all pages
    // 4. sqlite3_backup_finish(dest_db)
    // 5. Verify destination with PRAGMA integrity_check
    // 6. Unlock write mutex
}
```

### WAL/SHM handling

- Online Backup API reads from WAL consistently — no manual checkpoint needed.
- After backup, destination is a clean, self-contained SQLite file.
- WAL and SHM files of the source are not copied.
- On restore: just copy the backup file over `containercp.db`.
- WAL and SHM files from the old database are deleted on daemon restart.

### CLI commands

```
containercp storage backup /path/to/backup.db
containercp storage restore /path/to/backup.db
```

### Expected tests

- [ ] Backup creates valid SQLite file.
- [ ] Backup file passes `PRAGMA integrity_check`.
- [ ] Backup file contains all data (compare record counts).
- [ ] Backup during concurrent reads succeeds.
- [ ] Backup during write succeeds (write completes first or waits).
- [ ] Restore: file replaced, daemon restart picks up new data.
- [ ] Corrupted backup file → restore refused.
- [ ] WAL/SHM files not copied by backup.
- [ ] `PRAGMA wal_checkpoint(TRUNCATE)` on daemon shutdown.

### Integration tests

- [ ] Backup + restore + restart cycle.

### Manual validation

- [ ] `sqlite3 /path/to/backup.db "SELECT count(*) FROM sites"` matches original.

### Rollback method

- Restore from the backup file.

### Known risks

- Backup locks the write connection. On a busy system, this may briefly
  block writes. Mitigation: backup is an explicit operator action, not
  automatic.

### Non-goals

- No scheduled backup (ARCH-010).
- No incremental backup.
- No encryption of backup files.

### Completion checklist

- [ ] Backup produces consistent snapshot.
- [ ] Restore recovers all data.
- [ ] CLI commands work.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase12-backup-restore`

---

## Phase 13 — Remove Permanent TXT Production Storage

**Objective:** Ensure SQLite is the only active production backend;
retain legacy TXT parsing only for migration, recovery tooling, and
tests; remove obsolete permanent dual-storage code paths; update
documentation and operational tooling.

### Files expected to change

| File | Change |
|------|--------|
| `libs/storage/Storage.cpp` | Remove all TXT file I/O code; keep only SQLite implementation |
| `libs/storage/Storage.h` | Remove TXT file helper declarations |
| `libs/storage/LegacyTXTParser.h` | NEW — extracted from old Storage for migration/tests only |
| `libs/storage/LegacyTXTParser.cpp` | NEW — parsers isolated from production path |
| `libs/core/ServiceRegistry.cpp` | Remove conditional TXT vs SQLite loading |
| `CMakeLists.txt` | Add LegacyTXTParser sources |
| `tests/test_storage.cpp` | Rewrite to test only SQLite backend |
| `tests/test_migration.cpp` | Legacy parser tested via LegacyTXTParser |

### What is removed from production Storage

- `nodes_file()`, `sites_file()` ... all 19 path helpers.
- `save_nodes()` TXT version, `load_nodes()` TXT version — all 17 pairs.
- `save_mail_module_state()`, `load_mail_module_state()` TXT.
- `save_mail_smarthost()`, `load_mail_smarthost()` TXT.
- `migrate_template_profiles()` — handled by migration, not Storage.
- Format detection (pipe counting) — only in `LegacyTXTParser`.

### What is preserved (in LegacyTXTParser)

- All `load_*()` TXT parsers, renamed to `LegacyTXTParser::parse_*(file)`.
- Format detection (pipe counting).
- Only used by:
  - Migration importer (Phase 8).
  - Test fixtures (Phase 0).
  - Recovery tooling.

### Expected tests

- [ ] All 257 existing tests pass with SQLite-only backend.
- [ ] LegacyTXTParser correctly parses all fixture formats.
- [ ] No TXT file I/O in production Storage code path.
- [ ] `ServiceRegistry` loads directly from SQLite.

### Integration tests

- [ ] Full daemon startup without any `.db` files present (fresh install).
- [ ] Full daemon startup after migration from v0.6.0.
- [ ] Archived TXT files are not read.

### Manual validation

- [ ] `strace -e openat containercpd 2>&1 | grep "\.db"` — only `containercp.db` opened.

### Rollback method

- Revert phase 13 commit. Phases 1–12 remain; Storage reverts to dual-backend.

### Known risks

- Some code paths may still reference TXT helpers. Mitigation: exhaustive grep
  for `.db` file access patterns before marking complete.

### Non-goals

- No deleting migration or archive code.
- No removing test fixtures.

### Completion checklist

- [ ] No TXT I/O in production Storage.
- [ ] Legacy parsers moved to dedicated file.
- [ ] All tests pass.
- [ ] `ServiceRegistry` loads from SQLite unconditionally.

### 🔴 STOP — review and commit before proceeding

**Expected commit:** `phase13-remove-txt-production`

---

## Phase 14 — Clean Debian 13 Validation and Release Readiness

**Objective:** Validate build, install, fresh installation, and real
v0.6.0 upgrade on clean Debian 13; verify permissions, WAL behavior,
migration diagnostics, backup, restore, and service restart; update
relevant documentation. Do not release or bump the project version
unless separately approved.

### Files expected to change

| File | Change |
|------|--------|
| `README.md` | SQLite dependency documented |
| `INSTALL.md` | Update build prerequisites |
| `CHANGELOG.md` | Add ARCH-008 entry |
| `docs/development/coding-rules.md` | Update storage rules |
| `planning/proposals/ARCH-008-SQLite-Storage-Foundation.md` | Final status update |
| `planning/project-status.md` | Mark ARCH-008 complete |

### Validation checklist

#### Build validation

- [ ] Clean build from source on Debian 13.
- [ ] Zero compiler warnings.
- [ ] All tests pass (257+).

#### Fresh installation

- [ ] Run `scripts/install.sh` on clean Debian 13.
- [ ] `containercpd` starts and creates `containercp.db`.
- [ ] `curl http://127.0.0.1:8080/api/health` returns 200.
- [ ] `sqlite3 /srv/containercp/database/containercp.db ".tables"` shows all tables.
- [ ] Database file permissions are `0600`.

#### v0.6.0 upgrade

- [ ] Install v0.6.0, create a site with SSL + mail + backup.
- [ ] Stop daemon.
- [ ] Replace binary with ARCH-008 build.
- [ ] Start daemon — migration runs.
- [ ] `/var/log/containercp/storage.log` shows successful migration.
- [ ] All sites, domains, SSL, mail, backups visible in Web UI.
- [ ] `containercp site list` works via CLI.
- [ ] Legacy `.db` files archived.
- [ ] Rollback by removing `containercp.db` and restoring `.db` files → works.
- [ ] Upgrade from anonymized production-derived data produces identical
      record counts and relationship integrity.
- [ ] Migration of production-scale data (8 sites, 2 mail domains,
      1 mailbox, 9 reverse proxies) completes within 1 second.

#### WAL behavior

- [ ] `containercp.db-wal` and `containercp.db-shm` exist during operation.
- [ ] `PRAGMA journal_mode` returns `wal`.
- [ ] `PRAGMA wal_autocheckpoint` returns `1000`.

#### Migration diagnostics

- [ ] Corrupted fixture → clear error message.
- [ ] After fixing source → retry succeeds.

#### Backup and restore

- [ ] `containercp storage backup /tmp/test.db` succeeds.
- [ ] `sqlite3 /tmp/test.db "PRAGMA integrity_check"` returns `ok`.
- [ ] `containercp storage restore /tmp/test.db` → daemon restarts.

#### Service restart

- [ ] `systemctl restart containercpd` — no migration re-run.
- [ ] `systemctl status containercpd` — active.

### Definition of Done

- [ ] All checklist items in all 14 phases are complete.
- [ ] All 257+ existing tests pass.
- [ ] Fresh install and v0.6.0 upgrade validated on clean Debian 13.
- [ ] Storage backend is SQLite-only.
- [ ] Legacy parsers exist only in migration/tests.
- [ ] No production code depends on TXT files.
- [ ] Backup and restore verified.
- [ ] Architecture proposal updated to final status.

### 🔴 STOP — final review, commit, and notify architect

**Expected commit:** `phase14-validation`

---

## Appendix: Sensitive Field Inventory

The following fields contain sensitive data and must never be exposed
through API responses, Web UI output, logs, diagnostics, or audit events:

| Table | Column | Type | Sensitivity |
|-------|--------|------|-------------|
| `databases` | `db_password` | Plaintext | Recoverable secret (tech debt) |
| `mail_mailboxes` | `password_hash` | SHA-512-CRYPT | Credential verifier |
| `access_users` | `password_hash` | SHA-256 (or similar) | Credential verifier |
| `auth_users` | `password_hash` | SHA-256 | Credential verifier |
| `mail_config` | `value` (smarthost) | Plaintext | Recoverable secret (tech debt) |

**Rules:**
- Password hashes may be stored in SQLite (product-owner approved).
- Plaintext secrets (db_password, smarthost password) are migrated for
  backward compatibility and marked as technical debt.
- No sensitive field is ever written to logs, API output, or Web UI.
- The `JsonFormatter` and API handlers must explicitly exclude or
  redact these fields.

## Appendix: Resource Type Summary

| # | Resource | Table | Fields | FKs | Sensitive | Legacy variants |
|---|----------|-------|--------|-----|-----------|-----------------|
| 1 | nodes | `nodes` | 3 | — | No | None |
| 2 | sites | `sites` | 6 | nodes | No | 5-field |
| 3 | users | `users` | 6 | — | No | None |
| 4 | domains | `domains` | 9 | users, sites | No | None |
| 5 | php_versions | `php_versions` | 5 | — | No | None |
| 6 | databases | `databases` | 9 | users, sites | **db_password** | None |
| 7 | backups | `backups` | 10 | sites, users | No | None |
| 8 | ssl_certificates | `ssl_certificates` | 20 | domains | No | 4-field |
| 9 | mail_domains | `mail_domains` | 16 | — (sentinel 0) | No | 10-field |
| 10 | mail_mailboxes | `mail_mailboxes` | 13 | mail_domains | **password_hash** | None |
| 11 | mail_aliases | `mail_aliases` | 7 | mail_domains | No | None |
| 12 | access_users | `access_users` | 5 | — | **password_hash** | None |
| 13 | access_grants | `access_grants` | 5 | access_users, sites | No | None |
| 14 | reverse_proxies | `reverse_proxies` | 8 | — (sentinel 0) | No | None |
| 15 | profiles | `profiles` | 9 | — | No | None |
| 16 | auth_users | `auth_users` | 6 | — | **password_hash** | None |
| 17 | mail_config | `mail_config` | 2 (KV) | — | **smarthost password** | None |

## Appendix: Commit Sequence Summary

| Order | Commit | Phase | Description |
|-------|--------|-------|-------------|
| 1 | `phase0-baseline-fixtures` | 0 | Test fixtures |
| 2 | `phase1-sqlite-wrapper` | 1 | RAII wrapper + CMake |
| 3 | `phase2-connection-pool` | 2 | Pool + transactions |
| 4 | `phase3-migration-engine` | 3 | Schema migrations |
| 5 | `phase4-sqlite-schema` | 4 | Business tables |
| 6 | `phase5-core-subset-storage` | 5 | Nodes, PHP, profiles |
| 7 | `phase6a-core-sites-domains` | 6a | Sites, domains, users |
| 8 | `phase6b-core-databases-proxies` | 6b | Databases, proxies |
| 9 | `phase6c-core-access` | 6c | Access users, grants |
| 10 | `phase7-mail-ssl-storage` | 7 | Mail + SSL metadata |
| 11 | `phase8-legacy-importer` | 8 | TXT import |
| 12 | `phase9-verification` | 9 | Equivalence checks |
| 13 | `phase10-legacy-archive` | 10 | Backup archive |
| 14 | `phase11-startup-gate` | 11 | Migration gate |
| 15 | `phase12-backup-restore` | 12 | Backup/restore |
| 16 | `phase13-remove-txt-production` | 13 | Cleanup |
| 17 | `phase14-validation` | 14 | Debian 13 validation |
