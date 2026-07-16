# Storage API

## SQLiteDB — Low-Level SQLite Wrapper

### Purpose and ownership

`containercp::storage::SQLiteDB` is a minimal RAII wrapper around a
single SQLite connection and at most one prepared statement.

It is:

- a **low-level building block** owned by the Storage subsystem;
- **move-only** and **non-copyable**;
- designed for use by:
  - `Storage` class (production persistence);
  - `MigrationEngine` (schema and data migration);
- **not** for direct use by managers, API handlers, providers, or UI code.

### Header

`libs/storage/SQLiteWrapper.h`

---

## Lifecycle API

### `bool open(const std::string& path)`

Opens a SQLite database at the given filesystem path.

- Creates the database file if it does not exist.
- Applies all standard PRAGMAs (see PRAGMA contract below).
- Returns `true` on success.
- Returns `false` on error (check `error_message()` and `error_code()`).
- Calling `open()` on an already-open database is a no-op (returns `true`).

### `bool close()`

Closes the database and finalizes any active prepared statement.

- Idempotent — safe to call multiple times.
- Returns `true` on success.
- Returns `false` on error (unusual — indicates a SQLite issue).

### `bool is_open()`

Returns `true` if `open()` has been called successfully and `close()`
has not been called (or failed).

### Destructor

Calls `close()`. Does not throw.

### Move construction and move assignment

Transfer ownership of the underlying `sqlite3*` and `sqlite3_stmt*`
handles. The moved-from object becomes equivalent to a default-constructed
state (`is_open()` returns `false`).

---

## Statement API

### `bool exec(const std::string& sql)`

Executes one or more SQL statements that return no result rows.

Suitable for:

- `CREATE TABLE`
- `INSERT` without `RETURNING`
- `UPDATE`
- `DELETE`
- `PRAGMA`

Returns `true` on success. Returns `false` on error.

### `bool prepare(const std::string& sql)`

Prepares a single SQL statement for parameterized execution.

- Finalizes any previously prepared statement automatically.
- Use `bind_*()` to set parameters, then `step()` to execute.
- Returns `true` on success. Returns `false` on error.

### Parameter binding

Parameters are indexed starting at **1** (SQLite convention).

```cpp
bool bind_int(int index, int64_t value);
bool bind_text(int index, const std::string& value);
bool bind_null(int index);
```

- `bind_text()` copies the string content using `SQLITE_TRANSIENT`.
- Returns `true` on success, `false` on error (e.g., no prepared statement).

---

## Execution semantics

### `bool step()`

Advances the prepared statement to the next result row.

| Return value | Meaning |
|-------------|---------|
| `true` | A row is available (`SQLITE_ROW`). Use `column_*()` to read columns. |
| `false` | Execution is complete (`SQLITE_DONE`) **or** an error occurred. |

**Important:** `false` alone does not mean success. Callers that need
to distinguish completion from failure must inspect `error_code()` or
`error_message()` after `step()` returns `false`.

### Column access

Columns are indexed starting at **0** (zero-based, following SQLite
`sqlite3_column_*` convention).

```cpp
int column_count();
int64_t column_int(int index);
std::string column_text(int index);
bool column_is_null(int index);
```

- `column_count()` returns 0 if no statement is prepared.
- `column_int()` returns 0 if the column is `NULL` or no statement.
- `column_text()` returns an empty string if the column is `NULL`.
- `column_is_null()` returns `true` if the column is `NULL` or no statement.

---

## Transactions

```cpp
bool begin_immediate();
bool commit();
bool rollback();
```

- `begin_immediate()` executes `BEGIN IMMEDIATE`, which acquires a
  write lock immediately rather than waiting until the first write.
- `commit()` and `rollback()` execute `COMMIT` and `ROLLBACK`.
- The wrapper does **not** provide nested transactions (SQLite does
  not support them natively, and savepoints are not exposed here).
- Transaction ownership and multi-statement transaction management
  are handled at the Storage infrastructure level (Phase 2).

---

## PRAGMA contract

The following PRAGMAs are applied by `apply_pragmas()`, which is
called automatically by `open()`:

| PRAGMA | Value | Purpose |
|--------|-------|---------|
| `journal_mode` | `WAL` | Write-Ahead Logging — enables concurrent reads during writes |
| `synchronous` | `FULL` | Full fsync on every transaction — maximum durability |
| `foreign_keys` | `ON` | Enforce foreign key constraints |
| `busy_timeout` | `5000` | Wait 5 seconds before returning `SQLITE_BUSY` |
| `wal_autocheckpoint` | `1000` | Trigger automatic WAL checkpoint every 1000 pages |
| `journal_size_limit` | `67108864` | Retained WAL size limit (~64 MiB) |

**Notes:**

- The `journal_size_limit` of 64 MiB is a **retained WAL size** limit,
  not a database size limit. The WAL file may temporarily grow to this
  size before the next checkpoint.
- These PRAGMAs are applied to every connection when it opens.
- Every connection in the Storage subsystem must use identical settings.

---

## Error contract

```cpp
std::string error_message() const;
int error_code() const;
```

- Errors are propagated through Boolean return values plus stored
  diagnostic state.
- No exceptions are currently used by `SQLiteDB`.
- Raw SQLite error codes may be exposed through `error_code()`.
- Stale error state is cleared at the start of every mutating public
  method. After a successful call, `error_code()` returns 0 and
  `error_message()` returns an empty string.
- On failure, `error_code()` returns the SQLite error code and
  `error_message()` returns a human-readable description.

---

## Raw handle access

```cpp
sqlite3* handle() const;
```

- Restricted to Storage and Migration Engine infrastructure.
- Returns the raw `sqlite3*` pointer, or `nullptr` if not open.
- Returning the handle does **not** transfer ownership.
- External modules must **not** close, modify, or bypass the handle.
- Managers, API handlers, providers, and UI code must never use this
  method.

---

## ConnectionPool — Connection and Transaction Infrastructure

### Purpose and ownership

`containercp::storage::ConnectionPool` owns one serialized write
connection and exactly three bounded read connections to the SQLite
database.

It is:

- owned by the Storage subsystem;
- responsible for all SQLite connections in the process;
- a bounded pool — no dynamic scaling;
- designed for the current single-daemon architecture.

### Header

`libs/storage/ConnectionPool.h`

### Connection architecture

```
ConnectionPool
  ├── Write connection (serialized by std::mutex)
  │     All save_*(), DDL, transactions
  └── Read connections (3, round-robin lease)
        All load_*(), SELECT, read-only PRAGMAs
```

### Write connection

- One `SQLiteDB` instance, accessed under `std::mutex`.
- All mutating operations (INSERT, UPDATE, DELETE, DDL) go through it.
- Transaction primitives (`begin_immediate`, `commit`, `rollback`) use
  this connection.
- The write mutex ensures only one thread writes at a time.

```cpp
pool.lock_write();
pool.write_connection().exec("INSERT ...");
pool.unlock_write();
```

### Read connection pool

- Exactly `kReadPoolSize = 3` read connections.
- Leased via round-robin with compare-exchange for contention-free
  acquisition.
- If all connections are busy, the lease attempt spins briefly
  (100µs–1ms) and retries.

```cpp
SQLiteDB* conn = pool.lease_read();
// ... use conn ...
pool.return_read(conn);
```

### Connection lifecycle

- All connections are created during `initialize(db_path)`.
- PRAGMAs are applied automatically by `SQLiteDB::open()`.
- `shutdown()` closes all connections and marks read leases as
  released.
- `~ConnectionPool()` calls `shutdown()`.

### Thread-safety guarantees

- Write connection: serialized by `std::mutex`. Must lock/unlock
  explicitly.
- Read connections: concurrent access is safe. Multiple threads can
  hold different read connections simultaneously.
- A read may briefly block during a WAL checkpoint (handled by
  `busy_timeout = 5000`).
- No recursive mutex — do not nest `lock_write()` calls.

### Transaction ownership

- Transactions operate on the write connection.
- `ConnectionPool` does not track transaction state.
- Transaction begin/commit/rollback must be paired correctly by the
  caller (Storage or MigrationEngine).
- Nested transactions are not supported (SQLite limitation).

### Shutdown behavior

1. Read connections are force-released (outstanding leases lose their
   connection).
2. Write connection is closed.
3. Read connections are closed.

Double shutdown is safe.

### Public API

```cpp
bool initialize(const std::string& db_path);

SQLiteDB& write_connection();
void lock_write();
void unlock_write();

SQLiteDB* lease_read();
void return_read(SQLiteDB* db);

void shutdown();

bool backup(const std::string& dest_path);
```

### Backup

Uses the SQLite Online Backup API (`sqlite3_backup_init/step/finish`).
The write mutex is held for the duration to ensure a consistent
snapshot.

---

## Storage — Transaction Stubs (TXT Backend)

The `Storage` class declares transaction and backup methods for
forward compatibility with the SQLite backend:

```cpp
bool begin_transaction();
bool commit_transaction();
bool rollback_transaction();
bool backup(const std::string& dest_path);
```

With the current TXT backend, these all return `false`.
They will delegate to `ConnectionPool` when Storage is switched
to SQLite in Phase 5+.
