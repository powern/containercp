# ConnectionPool Lifecycle Contract

## Purpose

`ConnectionPool` manages a bounded set of SQLite connections (1 write + 3 read) with explicit lease tracking, write-guard mutual exclusion, and safe shutdown semantics.

## State machine

```
 UNINITIALIZED → INITIALIZED → SHUTDOWN
      ↓              ↓              ↓
   No leases     Read/Write    No new leases
   No guards     operations    No new guards
```

- **UNINITIALIZED**: default-constructed. `write_conn_ == nullptr`. All slots inactive.
- **INITIALIZED**: `initialize()` succeeded. `write_conn_ != nullptr`. Read connections open. Slots available.
- **SHUTDOWN**: `shutdown()` completed. `write_conn_ == nullptr`. All slots inactive. Connections closed.

## Invariants

| # | Invariant | Enforcement |
|---|-----------|-------------|
| 1 | Uninitialized pool issues no leases | `lease_read()` returns nullptr if `!write_conn_` |
| 2 | Shut-down pool issues no leases/write guards | `lease_read()` returns nullptr if `shutdown_`; `WriteGuard`/`TransactionGuard` check `is_shutdown()` |
| 3 | Each `ReadLease` maps to exactly one in-use slot | `lease_read()` CAS-sets `read_in_use_[i]` from false→true |
| 4 | Each in-use slot contributes one to `outstanding_leases_` | `fetch_add(1)` on successful lease; `fetch_sub(1)` on return |
| 5 | Returning a lease clears exactly its own slot | `return_read()` matches `read_conns_[i].get() == db` |
| 6 | Double return has no effect | `exchange(false)` on `read_in_use_[i]`; if `was_in_use` was already false, no decrement |
| 7 | Unknown pointer return has no effect | `return_read()` iterates slots; if no match, no-op |
| 8 | `shutdown()` blocks until all leases returned | `while (outstanding_leases_.load() > 0) sleep(...)` |
| 9 | `shutdown()` is idempotent | `shutdown_.store(true)` at entry; second call is a quick no-op path |
| 10 | Destructor safe for all states | `~ConnectionPool()` calls `shutdown()`; handles nullptr connections |
| 11 | Write connection not destroyed while WriteGuard active | `shutdown()` acquires `write_mutex_` before destroying |
| 12 | Read connections not destroyed while ReadLease active | `shutdown()` waits for `outstanding_leases_ == 0` before closing |

## Methods

### initialize(path)

- Sets `db_path_`, clears `shutdown_` flag, opens write + 3 read connections.
- Returns `false` on failure; all opened connections are cleaned up.
- Callable only from UNINITIALIZED or SHUTDOWN state.
- After failed `initialize()`, pool returns to UNINITIALIZED state.

### lease_read()

- Returns `nullptr` if `shutdown_` flag set OR `write_conn_` null.
- Otherwise CAS-acquires a free read slot, increments `outstanding_leases_`.
- If all slots busy, retries with exponential backoff and eventually yields.

### return_read(db)

- If `db` is null: no-op (no counter change).
- Matches `db` against pool's read connections. If found, atomically clears the in-use flag.
- Only decrements `outstanding_leases_` if the slot was actually in use (`exchange(false)` returned `true`).
- Unknown `db` pointers are silently ignored.

### shutdown()

1. Sets `shutdown_` flag (prevents new leases/guards).
2. Blocks until `outstanding_leases_` reaches zero.
3. Notifies test observer (if configured).
4. Acquires `write_mutex_` (waits for active WriteGuard/TransactionGuard).
5. Closes and destroys write connection.
6. Releases write mutex.
7. Closes and destroys all read connections.
8. Idempotent: second call is a fast path.

### Destructor

Calls `shutdown()`. Safe for:
- Never-initialized pools (no connections to close)
- Initialization-failed pools (partial cleanup handled)
- Already-shutdown pools (idempotent)
- Pools with active operations (blocks until complete)

### write_connection()

- Returns reference to write connection.
- **Precondition:** caller holds an active WriteGuard or TransactionGuard.
- Dereferencing on uninitialized/shutdown pool is undefined behavior.
- Callers must use `try_write_connection()` for nullable access.

## Thread safety

- `shutdown_`: atomic, checked before any acquisition.
- `outstanding_leases_`: atomic, incremented on lease, decremented on return.
- `read_in_use_[i]`: atomic, CAS for acquisition, `exchange(false)` for return.
- `write_mutex_`: non-recursive mutex, acquired by WriteGuard/TransactionGuard/shutdown.
- Read connections: slot-based CAS ensures mutual exclusion per connection.
- `write_conn_`: pointer stability guaranteed by write_mutex_ ownership during modification.

## See also

- `docs/development/legacy-archive-api.md` — Phase 10 archive behavior
- `docs/development/storage-api.md` — Storage backend selection
- `docs/ADR/` — Architecture Decision Records
