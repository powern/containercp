# SQLite Storage API

## Phase 5 status

Phase 5 introduces the `SQLiteStorage` implementation and makes it
available through an **explicit opt-in mode**. SQLite is NOT active
in the default runtime Storage backend.

| Mode | Backend for core resources | Activation |
|------|---------------------------|------------|
| Default (`CoreStorageBackend::Txt`) | TXT for all resources | Default — no action needed |
| Explicit (`CoreStorageBackend::SqlitePhase5`) | nodes, php_versions, profiles via SQLite | Pass `StorageOptions{ .core_backend = CoreStorageBackend::SqlitePhase5 }` to `Storage` |

**Default runtime behavior:** All resources, including nodes, PHP versions,
and profiles, are TXT-backed. Existing TXT data remains visible.

**Explicit SQLite mode:** Creates `containercp.db`, runs the schema
migration, and delegates the three Phase 5 resources to SQLiteStorage.
Other resources remain TXT-backed. If initialization or migration fails,
`sqlite_ready()` returns false and core resource operations are no-ops
(no silent TXT fallback).

---

## WriteGuard

RAII write guard. Locks write mutex on construction, unlocks on destruction.

```cpp
class WriteGuard {
public:
    explicit WriteGuard(ConnectionPool& pool);
    ~WriteGuard();

    bool is_valid() const;   // false if shutdown started before lock acquired
    SQLiteDB& db();          // valid only when is_valid() returns true
};
```

### Lifecycle

1. **Construction:** Lock write mutex. Check `shutdown_` flag.
   - If shutdown has not started: `is_valid()` = true, connection accessible.
   - If shutdown has started: release mutex, `is_valid()` = false.
2. **Active:** Use `db()` for writes. Valid only while `is_valid()` is true.
3. **Destruction:** Unlock write mutex.

### Rules

- Always check `is_valid()` before calling `db()`.
- `db()` returns a reference to the write connection, which is stable for
  the guard's lifetime because `shutdown()` cannot destroy the connection
  while the guard holds the write mutex.
- Non-copyable, non-movable.

---

## ReadLease

Scoped RAII read lease. See `docs/development/storage-api.md`.

---

## TransactionGuard

RAII transaction guard with fail-closed semantics.

```cpp
class TransactionGuard {
public:
    explicit TransactionGuard(ConnectionPool& pool);
    ~TransactionGuard();

    bool is_active() const;
    bool commit();
    SQLiteDB& db() const;    // stable connection; valid only when active
};
```

### Lifecycle

```
Construction:
  lock write mutex
  check shutdown flag — if set: unlock, is_active() = false
  try_write_connection() — if null: unlock, is_active() = false
  BEGIN IMMEDIATE — if fails: unlock, is_active() = false
  → is_active() = true, db_ stored

Active:
  use db() for writes
  every bind/prepare/step must check return value

commit():
  COMMIT
  success → committed, true
  failure → false (destructor will ROLLBACK)

Destruction:
  if active and not committed → ROLLBACK
  unlock write mutex
```

### Rules

1. **Rollback by default.** Destructor rolls back if `commit()` was not
   called. No auto-commit.
2. **Check `is_active()`** before any write operation.
3. **Stable `db_` pointer.** The connection pointer is captured once
   during construction and used for all operations within the transaction.
   `shutdown()` cannot destroy the connection while the guard holds the
   write mutex.
4. **Explicit `commit()` required** for persistence.
5. **Failed `COMMIT`** leaves the transaction eligible for rollback on
   destruction.
6. **Failed `BEGIN`** leaves the guard inactive — no lock held, no
   transaction started.

---

## ConnectionPool write lifecycle

### Shutdown sequence

```
shutdown():
  1. Set shutdown_ flag (atomic)
     → prevents new WriteGuard, TransactionGuard, backup, and read leases
  2. Wait for outstanding read leases (all returned)
  3. Notify test observer (if configured)
  4. Acquire write_mutex_
     → waits for any active WriteGuard, TransactionGuard, or backup
  5. Close and reset write_conn_
  6. Release write_mutex_
  7. Close and reset read connections
```

### Lock order

1. `shutdown_` flag (atomic, no mutex)
2. Read lease counter (atomic, drain before write mutex)
3. `write_mutex_` (std::mutex, acquired by WriteGuard, TransactionGuard,
   and backup)

This ordering guarantees:
- No new write guard can acquire the mutex after shutdown_ is set.
- Any guard that already holds the mutex keeps the connection alive until
  it releases.
- shutdown() waits for the last guard to release before destroying the
  write connection.
- No dangling pointer or use-after-free is possible.

### Write connection lifecycle

- Created during `initialize()`.
- Destroyed during `shutdown()` while holding `write_mutex_`.
- Guards (WriteGuard, TransactionGuard, backup) hold `write_mutex_`
  for their entire lifetime.
- `try_write_connection()` is private — only friends (WriteGuard,
  TransactionGuard) can call it. It returns a raw pointer valid only
  while the caller owns `write_mutex_`.

### Backup synchronization

`backup()` uses `WriteGuard` internally:

```cpp
bool ConnectionPool::backup(const std::string& dest_path) {
    WriteGuard wg(*this);
    if (!wg.is_valid()) return false;
    // ... backup using wg.db() ...
}
```

This means:
- backup acquires the write mutex during the entire operation.
- shutdown() waits for backup to complete (via write_mutex_).
- backup cannot begin after shutdown() has started (WriteGuard checks
  shutdown_ flag).
- The write connection remains alive for the complete backup.

### Reinitialization

After `shutdown()`, the pool may be reinitialized with another call to
`initialize()`. This creates new connections and resets all state.
Subsequent guards operate on the new connections.

---

## Save/load semantics

### Complete-vector replacement

Every `save_*` method uses `replace_all()`:

1. `TransactionGuard` → BEGIN IMMEDIATE
2. `DELETE FROM <table>` (all rows)
3. `INSERT` every record (bound parameters, every return checked)
4. `COMMIT`
5. On any error: rollback, no partial state

---

## Checked save methods (try_save_*)

In addition to the existing void `save_*` methods, `SQLiteStorage` provides
`bool`-returning checked variants for all 16 resource types:

```cpp
bool try_save_nodes(const std::vector<node::Node>& nodes);
bool try_save_php_versions(const std::vector<php::PhpVersion>& versions);
// ... all 16 resource types
```

### Contract

- Returns `true` only when the transaction committed successfully:
  - BEGIN IMMEDIATE started
  - All DELETE / UPSERT / INSERT operations succeeded (every bind, step, error check)
  - COMMIT succeeded
- Returns `false` if any step failed (FK violation, connection issue, SQL error,
  pool unavailable, shutdown in progress)
- On `false`, no partial state is committed (transaction rolled back or
  never started)
- The existing void `save_*` methods delegate to `try_save_*` and discard
  the result, preserving backward compatibility.

### When to use

- LegacyImporter uses try_save_* exclusively (must confirm commit).
- Runtime code (CLI, API, Web UI) uses the existing void save_* methods.

### Callers

| Checked method | Internal primitive |
|----------------|-------------------|
| `try_save_nodes`, `try_save_php_versions`, `try_save_profiles`, `try_save_users`, `try_save_domains`, `try_save_databases`, `try_save_reverse_proxies`, `try_save_access_grants`, `try_save_ssl_certificates`, `try_save_mailboxes`, `try_save_mail_aliases` | `replace_all()` (DELETE + INSERT + commit) |
| `try_save_sites`, `try_save_access_users`, `try_save_mail_domains` | `sync_parent_rows()` (UPSERT + prune) |
| `try_save_mail_module_state`, `try_save_mail_smarthost` | Direct `TransactionGuard` |

---

## Dual-backend boundary

With explicit SQLite mode:

| Backend | Resources |
|---------|-----------|
| SQLite | nodes, php_versions, profiles, users, sites, domains, databases, reverse_proxies, access_users, access_grants, ssl_certificates, mail_domains, mail_mailboxes, mail_aliases, mail_config |
| TXT | backups, auth_users, all other resources |

Without explicit SQLite mode (default):

| Backend | Resources |
|---------|-----------|
| TXT | ALL resources |

---

## FK-safe parent synchronization (UPSERT + prune)

### Problem

`access_grants` has two enforced FKs:

- `access_user_id → access_users(id) ON DELETE RESTRICT`
- `site_id → sites(id) ON DELETE RESTRICT`

The standard `DELETE-all` + `INSERT` algorithm used by `replace_all()`
would fail for `access_users` and `sites` if any access_grants row
references them, because the `DELETE` would trigger a FK violation
before the re-INSERT.

### Solution

`sync_parent_rows()` is a shared helper used by both `save_sites()` and
`save_access_users()`. It uses a strict four-phase algorithm:

1. **UPSERT** every supplied row using `INSERT ... ON CONFLICT(id) DO UPDATE SET ...`
2. **Enumerate** existing IDs using strict error-checked SELECT:
   - `prepare()` failure → rollback
   - any `step()` error (`error_code() != 0`) → rollback
   - `error_code() == 0` after `step()` returns false = DONE (success)
   - empty table correctly produces an empty set (DONE on first step)
3. **Prune:** DELETE absent IDs using **bound parameters**
   (`DELETE FROM table WHERE id = ?`), not SQL concatenation
4. **Check commit** — if `commit()` fails, the transaction rolls back

If any step fails (prepare, bind, step, commit, FK RESTRICT), the
entire transaction rolls back. No partial state is visible.

**Effects:**
- Updating a referenced parent succeeds.
- Adding a new parent succeeds.
- Removing an unreferenced parent succeeds.
- Removing a referenced parent fails with FK violation → entire save
  rolls back → no partial update.
- Empty vector = "remove all rows." Succeeds if no grants reference
  them; fails with rollback if any grant exists.

### Update to `save_sites()`

In Phase 6c, `save_sites()` was updated from `replace_all` to the
UPSERT + prune algorithm to support FK-safe coexistence with
`access_grants`. API and behavior are unchanged for the common case
(no grants referencing the removed site).

### Dependency order

Callers must create resources in this order:

1. `save_sites()` — creates sites that grants will reference
2. `save_access_users()` — creates users that grants will reference
3. `save_access_grants()` — creates grants referencing both

For deletion, reverse the order:

1. Remove grants (empty vector or reduced set)
2. Remove users/sites

---

## TestObserver (test-only lifecycle hooks)

`ConnectionPool::TestObserver` provides callbacks for deterministic
internal lifecycle tests. **NOT for production use.**

```cpp
struct TestObserver {
    // Called from shutdown() after setting shutdown_ flag and draining
    // read leases, immediately before attempting to lock write_mutex_.
    // May block (e.g., on a condition_variable) for test coordination.
    std::function<void()> on_shutdown_awaiting_write_mutex;

    // Called from backup() after successfully acquiring WriteGuard
    // (and therefore owning write_mutex_).  May block to pause backup
    // at a known point for deterministic testing.
    std::function<void()> on_backup_guard_acquired;
};
```

### Contract

- All callbacks are **empty by default** — zero production overhead.
- Callbacks execute **synchronously** in the calling thread's context.
- Callbacks **may block** (e.g., `cv.wait()`) for test coordination.
- Callbacks **must not throw** exceptions.
- Configure observers **before** starting worker threads.
- Do **not** modify observers concurrently while the observed operation
  is in progress.
- This interface must **never** be used by production logic, REST API,
  CLI, configuration, GUI, managers, or business code.

### Test cleanup rules

Tests using `TestObserver` must follow these rules to prevent
use-after-free:

1. **Always release blocked callbacks** before destroying the
   `ConnectionPool` or synchronization state. Set the release flag and
   call `cv.notify_all()`.
2. **Join every worker thread**. Never `detach()` a thread that
   references local test state.
3. **Clear observer callbacks** (`= nullptr`) after threads are joined.
4. **Timeouts are deadlock safeguards**, not valid completion paths. If
   a `cv.wait_for` timeout fires, the test must fail after safe cleanup.
5. **RAII cleanup objects** must release callbacks and join threads in
   their destructor, and must be declared after the `ConnectionPool` and
   synchronization state they reference (so they are destroyed first).

### `on_shutdown_awaiting_write_mutex`

Called at a precise point in `shutdown()`:

1. `shutdown_` flag is set (no new guards, leases, backup).
2. Read leases have been drained.
3. **Observer fires.**
4. `lock_write()` is called (waits for active guards/backup).

The test uses this to verify that shutdown has reached the point where
it is about to wait for a guard, and has not yet destroyed any connection.

### `on_backup_guard_acquired`

Called at a precise point in `backup()`:

1. `WriteGuard` is acquired (write_mutex_ held).
2. WriteGuard validity is confirmed.
3. **Observer fires.**
4. SQLite backup API calls begin.

The test uses this to pause backup while it holds the write mutex,
verifying that shutdown cannot proceed and the connection remains valid.

---

## Phase 6a field mappings

### User → `users`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved exactly |
| `username` | `username` | |
| `uid` | `uid` | |
| `home_directory` | `home_directory` | |
| `shell` | `shell` | |
| `enabled` | `enabled` | |
| `name` | — | Set from `username` on load (same as TXT) |

### Site → `sites`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved exactly |
| `domain` | `domain` | |
| `owner` | `owner` | |
| `node_id` | `node_id` | No FK — sentinel 0 = default/local node |
| `web_server` | `web_server` | |
| `php_mail_enabled` | `php_mail_enabled` | |
| `php_mail_enabled_present` | — | Set to `true` for SQLite rows (field always present) |
| `name` | — | Set from `domain` on load |

### Database → `databases`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved exactly |
| `db_name` | `db_name` | |
| `db_user` | `db_user` | |
| `db_password` | `db_password` | **Sensitive** — backward-compat plaintext |
| `engine` | `engine` | |
| `version` | `version` | |
| `owner_id` | `owner_id` | No FK — sentinel 0 = system owner |
| `site_id` | `site_id` | No FK — sentinel 0 = orphan |
| `enabled` | `enabled` | |
| `name` | — | Set from `db_name` on load |

### AccessUser → `access_users`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved exactly |
| `username` | `username` | |
| `auth_type` | `auth_type` | |
| `password_hash` | `password_hash` | **Sensitive** — credential verifier |
| `enabled` | `enabled` | |
| `name` | — | Set from `username` on load |

**Synchronization:** Uses FK-safe UPSERT + prune algorithm (see below).

### AccessGrant → `access_grants`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved exactly |
| `access_user_id` | `access_user_id` | FK → `access_users(id)` ON DELETE RESTRICT |
| `site_id` | `site_id` | FK → `sites(id)` ON DELETE RESTRICT |
| `permission` | `permission` | Serialized via `permission_to_string` / `permission_from_string` |
| `name` | — | Set from `"access_user_id-site_id"` on load |

Sentinel 0 is NOT valid for `access_user_id` or `site_id` — both FKs
enforce existence.

### SslCertificate → `ssl_certificates`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved |
| `domain_id` | `domain_id` | No FK — sentinel 0 valid |
| `domain` | `domain` | |
| `provider` | `provider` | |
| `certificate_path` | `certificate_path` | Filesystem path only — no PEM content |
| `key_path` | `key_path` | Filesystem path only — no key content |
| `chain_path` | `chain_path` | Filesystem path only |
| `issued_at` | `issued_at` | |
| `expires_at` | `expires_at` | |
| `renew_after` | `renew_after` | |
| `status` | `status` | |
| `auto_renew` | `auto_renew` | |
| `https_enabled` | `https_enabled` | |
| `redirect_enabled` | `redirect_enabled` | |
| `domains` | `domains` | SAN domain list |
| `challenge_type` | `challenge_type` | |
| `last_error` | `last_error` | |
| `last_validation` | `last_validation` | |
| `renew_attempts` | `renew_attempts` | |
| `version` | `version` | Metadata format version |
| `name` | — | Set from `domain` on load |

### MailDomain → `mail_domains`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved |
| `domain_id` | `domain_id` | No FK — 0 = external domain |
| `site_id` | `site_id` | No FK — 0 = unlinked |
| `domain_name` | `domain_name` | |
| `mode` | `mode` | Via `mail_domain_mode_to/from_string` |
| `relay_host` | `relay_host` | |
| `dkim_selector` | `dkim_selector` | |
| `dkim_private_key_path` | `dkim_private_key_path` | Path only — no key content |
| `dkim_public_key_dns` | `dkim_public_key_dns` | Public DNS value |
| `max_mailboxes` | `max_mailboxes` | |
| `max_aliases` | `max_aliases` | |
| `catch_all` | `catch_all` | |
| `enabled` | `enabled` | |
| `created_at` | `created_at` | Preserved exactly — model-supplied, not generated |
| `updated_at` | `updated_at` | Preserved exactly — model-supplied, not generated |
| `name` | — | Set from `domain_name` on load |

Mail domains are referenced by mailboxes and aliases with FK RESTRICT.
Uses `sync_parent_rows` (UPSERT + prune) to preserve referenced domains.

### Mailbox → `mail_mailboxes`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved |
| `domain_id` | `domain_id` | FK → `mail_domains(id)` RESTRICT |
| `local_part` | `local_part` | |
| `password_hash` | `password_hash` | **Sensitive** |
| `quota_bytes` | `quota_bytes` | |
| `quota_messages` | `quota_messages` | |
| `enabled` | `enabled` | |
| `display_name` | `display_name` | |
| `forward_to` | `forward_to` | |
| `spam_enabled` | `spam_enabled` | |
| `last_login` | `last_login` | |
| `created_at` | `created_at` | Preserved exactly — model-supplied |
| `updated_at` | `updated_at` | Preserved exactly — model-supplied |
| `name` | — | Set from `local_part` on load |

### MailAlias → `mail_aliases`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved |
| `domain_id` | `domain_id` | FK → `mail_domains(id)` RESTRICT |
| `source_local_part` | `source_local_part` | |
| `destination` | `destination` | Full email address |
| `enabled` | `enabled` | |
| `created_at` | `created_at` | Preserved exactly — model-supplied |
| `updated_at` | `updated_at` | Preserved exactly — model-supplied |
| `name` | — | Set from `source_local_part` on load |

### SSL certificate note

The `SslCertificate` C++ model has lifecycle timestamp fields
(`issued_at`, `expires_at`, `renew_after`, `last_validation`). These are
preserved exactly.

The schema-level `created_at` and `updated_at` columns in
`ssl_certificates` are **not** represented by the current C++ model.
They are generated internally during INSERT and are not round-trip
fields. This is a schema bookkeeping detail, not a model contract.

### Mail config (module state + smarthost)

Both stored in `mail_config` key-value table with persistent keys.
Each write uses `TransactionGuard` with checked commit — if `COMMIT`
fails, the transaction rolls back and the change is not persisted.

| Key | Value | Persistence method |
|-----|-------|-------------------|
| `module_state` | `"active"` or `"inactive"` | `INSERT OR REPLACE` per key |
| `smarthost` | Pipe-delimited config string | `INSERT OR REPLACE` per key |

Smarthost format: `enabled|host|port|username|password`

Each key is updated independently — saving one does not erase the other.

### ReverseProxy → `reverse_proxies`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved exactly |
| `domain` | `domain` | |
| `site_id` | `site_id` | No FK — sentinel 0 = admin panel |
| `provider` | `provider` | |
| `config_path` | `config_path` | Metadata only — no filesystem validation |
| `upstream` | `upstream` | |
| `enabled` | `enabled` | |
| `status` | `status` | |
| `name` | — | Set from `domain` on load |

### Domain → `domains`

| Field | Column | Notes |
|-------|--------|-------|
| `id` | `id` | Preserved exactly |
| `fqdn` | `fqdn` | |
| `owner_id` | `owner_id` | No FK — sentinel 0 = system owner |
| `site_id` | `site_id` | No FK — sentinel 0 = orphan/admin panel |
| `php_version` | `php_version` | |
| `ssl_enabled` | `ssl_enabled` | |
| `enabled` | `enabled` | |
| `type` | `type` | All domain types preserved |
| `target` | `target` | Redirect/alias target URL |
| `name` | — | Set from `fqdn` on load |

---

## Non-goals

- SQLite is NOT active in production runtime by default.
- No automatic TXT→SQLite data import.
- No startup migration gate.
- No TXT file deletion.
