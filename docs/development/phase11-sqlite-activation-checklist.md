# Phase 11 — SQLite Activation Plan and Checklist

## Architecture Analysis Summary

| Aspect | Current State |
|--------|--------------|
| Storage backend in production | Always `CoreStorageBackend::Txt` (default) |
| SqlitePhase5 | Fully implemented in `Storage`, never activated in production |
| ServiceRegistry | Constructs `Storage(config_.database_dir())` — no options |
| Managers | Storage-agnostic, pure in-memory containers |
| Migration command | Does NOT exist |
| LegacyImporter | `import_all()` complete, invocation-only |
| Verification | Complete, invocation-only |
| LegacyArchive | Complete, invocation-only |
| Config | No storage backend option |
| StartupManager | Setup wizard only, not storage |

## Key Architecture

```
ServiceRegistry
  └─ Storage(config.database_dir(), options)  ← needs backend config
       ├─ TXT save/load  (CoreStorageBackend::Txt)
       └─ SQLite save/load (CoreStorageBackend::SqlitePhase5)
            └─ ConnectionPool
                 └─ containercp.db
```

## P11-01 — Current Runtime Storage Analysis

### Findings

1. `ServiceRegistry` at `libs/core/ServiceRegistry.cpp:23` constructs `storage_(config_.database_dir())` with no `StorageOptions` — always TXT.
2. `CoreStorageBackend::SqlitePhase5` is never used in production code (only tests + Verification).
3. No migration command exists — no CLI, daemon, or REST endpoint for TXT-to-SQLite.
4. All 17 resources have SQLite save/load methods in `Storage.cpp`.
5. Managers (`SiteManager`, `UserManager`, etc.) are pure in-memory — storage-agnostic.
6. `StartupManager` handles setup wizard only — not storage initialization.
7. `Config` has no storage backend option.
8. 2 resources (backups, auth_users) are TXT-only in SqlitePhase5 — need SQLite support added.

### Dependency Map

```
API handler → ServiceRegistry → Managers (in-memory)
                          ↓
                      Storage (save/load)
                    ↙         ↘
              TXT files     SQLite (containercp.db)
              (pipe-delimited)  (ConnectionPool)
```

### Completion Criteria

[x] Analysis complete — documented above.

- [ ] P11-01 — Complete

## P11-02 — Backend Selection Contract

### Problem
No mechanism to select SQLite as runtime backend. `Storage` defaults to `Txt`.

### Affected files
- `libs/config/Config.h/cpp` — add storage backend config
- `libs/storage/Storage.h` — `CoreStorageBackend` enum already exists
- `libs/core/ServiceRegistry.h/cpp` — accept backend selection

### Approved contract
```ini
# /etc/containercp/containercp.conf (or equivalent)
storage.backend = legacy    # default, never changes
storage.backend = sqlite    # only after successful P11 migration
```

Default: `legacy`. Unknown value: startup failure. Empty: same as legacy.
No auto-detection. No silent fallback.

### Tests required
- Explicit `legacy` accepted
- Explicit `sqlite` accepted (if DB+state valid)
- Unknown value → startup failure
- Missing value → defaults to legacy
- SQLite selected but DB missing → startup failure
- SQLite selected but no activation state → startup failure

### Implementation evidence

Commit SHA: `97dbd1d`

Focused test result:
```
test cases:  605 |  605 passed | 0 failed | 0 skipped
assertions: 3526 | 3526 passed | 0 failed |
```

- [x] P11-02 — Complete

## P11-03 — Explicit Migration Command

### Problem
No way for an operator to trigger TXT-to-SQLite migration.

### Approved contract
```bash
containercp storage migrate-to-sqlite \
  --source /srv/containercp/database \
  --database /srv/containercp/database/containercp.db \
  --archive-root /srv/containercp/archive \
  --source-version v0.6.0 \
  --target-version v0.7.0 \
  --confirm
```

### Implementation
- Add daemon command `migrate-to-sqlite` to `DaemonApp`
- Add CLI command to `CommandDispatcher`
- Command does NOT start HTTP service
- Command exits non-zero on failure
- Requires `--confirm` flag
- Returns migration ID (UUID v4)
- Prints stage-level results

### Implementation evidence

Commit SHA: `97dbd1d` (P11-02), `6a1a48e` (P11-03)

Focused test result:
```
test cases:  607 |  607 passed | 0 failed | 0 skipped
assertions: 3541 | 3541 passed | 0 failed |
```

- [x] P11-03 — Complete

## P11-04 — Migration Orchestrator

### Problem
No single component ties together import + verification + archive.

### Approved contract
```cpp
struct MigrationOrchestrator {
    MigrationResult migrate_to_sqlite(
        const std::string& source_dir,
        const std::string& database_path,
        const std::string& archive_root,
        const std::string& source_version,
        const std::string& target_version);
};
```

Ordered workflow (fail-closed):
1. Validate inputs + paths
2. Generate migration UUID (v4, lowercase)
3. Create staged SQLite DB (temp path)
4. Apply schema migrations
5. Run `LegacyImporter::import_all()`
6. Run `Verification::verify_all()`
7. Close and reopen DB
8. Run reopened verification
9. Create immutable archive via `LegacyArchive::create_archive()`
10. Verify archive via `archive.verify_archive()`
11. Publish final SQLite DB (atomic rename)
12. Write activation state file
13. Return migration result

### Implementation evidence

Commit SHA: `6e3e009`

Focused test result:
```
test cases:  612 |  612 passed | 0 failed | 0 skipped
assertions: 3562 | 3562 passed | 0 failed |
```

- [x] P11-04 — Complete

## P11-05 — Phase 9 Verification Integration

### Problem
Migration must require complete Phase 9 verification.

### Contract
Reuse `Verification::verify_all()` public API. Require all checks:
- 17 initial resources, 17 reopened, all success
- integrity_check = "ok"
- foreign_key_violations empty
- site_id=0 survives

### Implementation evidence

Integrated in MigrationOrchestrator (commit `6e3e009`), steps 6–8.

- [x] P11-05 — Complete

## P11-06 — Phase 10 Archive Integration

### Problem
Migration must produce immutable archive.

### Contract
Reuse `LegacyArchive::create_archive()`. Require:
- archive success
- 19 file entries
- public verify_archive passes
- source TXT unchanged

### Implementation evidence

Integrated in MigrationOrchestrator (commit `6e3e009`), steps 9–10.

- [x] P11-06 — Complete

## P11-07 — Activation State

### Problem
No durable record that SQLite is the active backend.

### Contract
```json
// /srv/containercp/database/storage-state.json
{
    "state_version": 1,
    "active_backend": "sqlite",
    "migration_id": "<uuid>",
    "database_path": "/srv/containercp/database/containercp.db",
    "archive_path": "/srv/containercp/archive/legacy-...",
    "source_version": "v0.6.0",
    "target_version": "v0.7.0",
    "activation_timestamp": "20260718T120000Z",
    "schema_version": 1,
    "verification_result": "success"
}
```

Atomic write: temp file → fsync → rename → fsync parent.

### Implementation evidence

Integrated in MigrationOrchestrator (commit `6e3e009`), step 12. Test `MigrationOrchestrator activation state content` validates content.

- [x] P11-07 — Complete

## P11-08 — SQLite Startup Path

### Problem
ServiceRegistry always initializes TXT storage.

### Contract
When `config.backend == "sqlite"`:
1. Read and validate activation state
2. Require `active_backend == sqlite`
3. Validate database path, existence, non-symlink
4. Open ConnectionPool
5. Verify foreign_keys ON
6. Verify WAL mode + synchronous FULL
7. Run integrity check
8. Run FK check
9. Initialize runtime repositories
10. Only then allow HTTP service

### Implementation evidence

Commit SHA: _____________

Focused test result: _____________

- [ ] P11-08 — Complete

## P11-09 — No Silent Fallback

### Problem
Must never switch to TXT if SQLite is configured.

### Contract
If SQLite selected and any check fails → daemon exits with error.
No TXT fallback. No HTTP listener. Safe error logged.

### Implementation evidence

Commit SHA: _____________

Focused test result: _____________

- [ ] P11-09 — Complete

## P11-10 — Runtime Repository Wiring

### Problem
All 17 resources must work through SQLite Storage.

### Contract
Verify every resource uses Storage abstraction and reaches SQLite when backend=sqlite.
Currently 2 resources (backups, auth_users) are TXT-only in SqlitePhase5.

### Implementation
- Add SQLite save/load for backups and auth_users in Storage
- Verify all 17 resources work

### Implementation evidence

Commit SHA: _____________

Focused test result: _____________

- [ ] P11-10 — Complete

## P11-11 through P11-20

(Write-path validation, read-path validation, restart persistence, failure handling, observability, operator workflow, security, site_id=0, integration tests, production runbook)

### Implementation evidence

Commit SHA: _____________

### P11-21 — Clean Build and Final Validation

Commit SHA: _____________

## Traceability

| ID | Production files | Test cases | Commit SHA | Result |
|----|------------------|------------|------------|--------|
| P11-01 | Analysis only | — | — | Complete |
| P11-02 | Config, ServiceRegistry | backend selection tests | 97dbd1d | Complete |
| P11-03 | DaemonApp, CommandDispatcher | migration command tests | 6a1a48e | Complete |
| P11-04 | MigrationOrchestrator | migration orchestrator tests | 6e3e009 | Complete |
| P11-05 | Verification integration (in orchestrator) | — | 6e3e009 | Complete |
| P11-06 | Archive integration (in orchestrator) | — | 6e3e009 | Complete |
| P11-07 | Activation state (in orchestrator) | activation state test | 6e3e009 | Complete |

## Final validation

__Build environment:__ ________

__Full suite result:__ ________

__HEAD SHA:__ ________

__git status:__ ________
