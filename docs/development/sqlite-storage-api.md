# SQLite Storage API

## Phase 5 status

Phase 5 introduces the `SQLiteStorage` implementation and makes it
available through an **explicit opt-in mode**. SQLite is NOT active
in the default runtime Storage backend.

| Mode | Backend for core resources | Activation |
|------|---------------------------|------------|
| Default (`CoreStorageBackend::Txt`) | TXT for all resources | Default — no action needed |
| Explicit (`CoreStorageBackend::SqlitePhase5`) | nodes, php_versions, profiles via SQLite | Pass `StorageOptions{ .core_backend = CoreStorageBackend::SqlitePhase5 }` to Storage |

**Default runtime behavior:** All resources, including nodes, PHP versions,
and profiles, are TXT-backed. Existing TXT data remains visible.

**Explicit SQLite mode:** For testing and development. Creates
`containercp.db`, runs the schema migration, and delegates the three
Phase 5 resources to SQLiteStorage. Other resources remain TXT-backed.

---

## SQLiteStorage

### Responsibility

`containercp::storage::SQLiteStorage` provides SQLite persistence for
a fixed subset of resource types. Used internally by `Storage` when
explicit SQLite mode is active.

### Initialization and ownership

`SQLiteStorage` receives a `ConnectionPool&` owned by `Storage`.
The pool is initialized and the schema is migrated only when
`SqlitePhase5` mode is selected.

If initialization or migration fails, `use_sqlite()` returns false
and all resources remain TXT-backed.

---

## TransactionGuard

### Lifecycle

```
TransactionGuard(pool)
  ├── BEGIN IMMEDIATE succeeds → is_active() = true
  │     ├── perform writes
  │     ├── commit() → persists, guard becomes inactive
  │     ├── suppress_commit() + ~guard → ROLLBACK
  │     └── ~guard (no commit) → ROLLBACK (default)
  └── BEGIN IMMEDIATE fails → is_active() = false
        └── ~guard → only releases write lock
```

### API

```cpp
class TransactionGuard {
public:
    explicit TransactionGuard(ConnectionPool& pool);
    ~TransactionGuard();

    bool is_active() const;       // false if BEGIN failed
    void suppress_commit();       // mark for rollback on destruction
    bool commit();                // explicit commit; false on failure
};
```

### Rules

1. **Rollback by default.** If `commit()` is never called, the
   destructor rolls back. No auto-commit.
2. **Check `is_active()`** before any write. If `BEGIN IMMEDIATE`
   failed, no writes should proceed.
3. **Check every bind/prepare/step return.** On any failure, call
   `suppress_commit()` to ensure rollback.
4. **Explicit `commit()` required** for data to persist.
5. **After `commit()` succeeds**, the guard is inactive — destructor
   does nothing.
6. **Write lock** is acquired on construction and released on
   destruction (or immediately if `BEGIN` fails).

---

## Save/load semantics

### Complete-vector replacement

Every `save_*` method performs atomic replacement via `replace_all()`:

1. `BEGIN IMMEDIATE`
2. `DELETE FROM <table>` (all rows)
3. `INSERT` every record (bound parameters)
4. `COMMIT`
5. On any error: `ROLLBACK`, no partial state

Every bind/prepare/step return value is checked. On failure, the
transaction is marked for rollback and the method returns without
persisting any change.

---

## Dual-backend boundary

After Phase 5, with explicit SQLite mode:

| Backend | Resources |
|---------|-----------|
| SQLite | nodes, php_versions, profiles |
| TXT | all other resources |

Without explicit SQLite mode (default):

| Backend | Resources |
|---------|-----------|
| TXT | ALL resources |

---

## Extension notes

To add another resource type to SQLiteStorage:
1. Add `save_<type>` / `load_<type>` to `SQLiteStorage`.
2. Delegate from `Storage` within the `use_sqlite()` check.
3. The table must exist in the schema (add a migration if needed).
4. Add tests.
5. Update this document.

---

## Non-goals

- SQLite is NOT active in production runtime by default.
- No automatic TXT→SQLite data import in Phase 5.
- No startup migration gate.
- No TXT file deletion.
