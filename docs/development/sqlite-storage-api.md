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

## Dual-backend boundary

With explicit SQLite mode:

| Backend | Resources |
|---------|-----------|
| SQLite | nodes, php_versions, profiles, users, sites, domains |
| TXT | all other resources |

Without explicit SQLite mode (default):

| Backend | Resources |
|---------|-----------|
| TXT | ALL resources |

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
