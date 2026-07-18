# Phase 11 Production Review Fixes

Phase 11 SQLite activation is not approved for production deployment until every item in this checklist is implemented, tested, cleanly rebuilt, committed, and validated.

Do not start Phase 12 from this checklist. Do not implement unrelated features, architecture changes, deployment changes, or production-server changes.

## P11-R1 — Remove Automatic Schema Migration From Daemon Startup

### Problem
Current SQLite startup executes `MigrationEngine::migrate()` automatically. This violates the approved design because `containercpd` startup must never modify the database schema.

### Required behavior
- `containercpd` startup must never run schema migrations.
- Startup may only open the database, validate activation state, validate schema version, validate startup integrity, and continue.
- Unsupported schema versions must fail startup.
- Schema migration remains available only through `containercp storage migrate-to-sqlite`.
- No automatic migration may occur anywhere during daemon startup.

### Implementation plan
- Remove automatic schema migration from `Storage` SQLite startup.
- Add explicit schema-version validation against the runtime expected version.
- Keep migration execution inside the manual migration/orchestration path.
- Ensure validation failure shuts down the SQLite pool and throws a controlled startup error.

### Validation plan
- Add focused tests proving startup does not create or update schema metadata.
- Add focused tests proving unsupported schema versions fail startup.
- Run focused P11-R1 tests.
- Run full doctest suite.
- Run clean rebuild for `containercp_tests` and `containercpd`.

### Status
- [x] Complete

Validation evidence:
- Focused tests: `P11-R1*` passed (`2` tests, `19` assertions).
- Full suite: `635` tests, `3925` assertions passed.
- CTest: `1/1` tests passed.
- Clean rebuild: `cmake --build build2 --clean-first --target containercp_tests containercpd` completed successfully.
- Known existing warning debt remains; no new P11-R1-specific warning was introduced.

## P11-R2 — Replace Activation-State Substring Parsing

### Problem
Activation state parsing currently searches strings manually, which can accept malformed or ambiguous JSON and does not validate the complete activation-state contract.

### Required behavior
- Use a strict parser equivalent in quality to the Phase 10 manifest parser.
- Reject malformed JSON.
- Reject duplicate keys.
- Reject missing required keys.
- Reject unknown keys.
- Reject wrong value types.
- Reject invalid strings.
- Reject invalid enums.
- Validate all required fields: `state_version`, `migration_id`, `active_backend`, `database_path`, `schema_version`, `source_version`, `target_version`, `activation_timestamp`, `archive_path`, and `verification_result`.
- Startup must fail closed on any parsing problem.

### Implementation plan
- Introduce a strict activation-state parser for the approved JSON shape.
- Parse into a typed activation-state structure.
- Validate keys, value types, enums, and required fields centrally.
- Replace substring extraction in startup validation with the strict parser.

### Validation plan
- Add focused tests for malformed JSON, duplicate keys, missing required keys, unknown keys, wrong value types, invalid strings, invalid enums, and valid activation state.
- Run focused P11-R2 tests.
- Run full doctest suite.
- Run clean rebuild for `containercp_tests` and `containercpd`.

### Status
- [ ] Not complete

## P11-R3 — Validate Activation State Consistency

### Problem
Startup does not fully prove that activation state represents a real completed migration.

### Required behavior
- Verify archive exists.
- Verify archive path matches activation state.
- Verify migration ID is valid.
- Verify database path matches the configured database path.
- Verify schema version matches runtime expectation.
- Activation state must represent a real completed migration.

### Implementation plan
- Extend activation-state validation after strict parsing.
- Reuse existing migration/archive validation helpers where appropriate.
- Check archive path existence and expected identity.
- Check migration ID and schema version before SQLite startup proceeds.

### Validation plan
- Add focused tests for missing archive, invalid archive path, invalid migration ID, database path mismatch, schema version mismatch, and valid completed migration state.
- Run focused P11-R3 tests.
- Run full doctest suite.
- Run clean rebuild for `containercp_tests` and `containercpd`.

### Status
- [ ] Not complete

## P11-R4 — Do Not Silently Ignore SQLite Write Failures

### Problem
SQLite write paths frequently ignore failures using `(void)try_save_xxx(...)`, which can hide failed writes and cause silent data loss.

### Required behavior
- Every SQLite write failure must propagate to the caller.
- Propagation may use a return failure, controlled exception, or Result type.
- Callers must always know when a write fails.
- No SQLite write failure may be silently ignored.

### Implementation plan
- Replace ignored `try_save_xxx` results with controlled failure propagation in SQLite-backed `Storage` write paths.
- Preserve existing public APIs only where necessary by throwing controlled exceptions from void save methods.
- Add focused tests that force SQLite write failures and assert caller-visible failure.

### Validation plan
- Add focused tests for representative parent, child, and mail configuration write failures.
- Verify failure does not create TXT fallback files.
- Run focused P11-R4 tests.
- Run full doctest suite.
- Run clean rebuild for `containercp_tests` and `containercpd`.

### Status
- [ ] Not complete

## P11-R5 — Complete Production Failure Handling Tests

### Problem
Current failure testing does not cover enough production startup and activation failure modes.

### Required behavior
- Add failure tests for schema validation.
- Add failure tests for startup validation.
- Add failure tests for activation-state validation.
- Add failure tests for archive validation.
- Add failure tests for SQLite open.
- Add failure tests for migration state mismatch.
- Add failure tests for corrupted activation state.
- Add failure tests for unsupported schema version.
- Every failure must stop startup, never fallback to TXT, and leave SQLite untouched.

### Implementation plan
- Add focused production failure tests around startup validation and activation-state validation.
- Ensure each test asserts failure, no TXT fallback file creation, and no unintended SQLite mutation.
- Add minimal production code changes only if tests expose gaps.

### Validation plan
- Run focused P11-R5 tests.
- Run full doctest suite.
- Run clean rebuild for `containercp_tests` and `containercpd`.

### Status
- [ ] Not complete

## P11-R6 — Complete Production Security Validation

### Problem
Production startup must validate filesystem security for SQLite activation inputs, not only basic existence.

### Required behavior
- Verify database permissions.
- Verify activation-state permissions.
- Verify directory permissions.
- Verify ownership.
- Verify regular files only.
- Reject symlinks.
- Reject unexpected filesystem objects.
- Fail closed on security validation failure.

### Implementation plan
- Add explicit filesystem security checks for database directory, database file, activation-state file, and archive path.
- Keep checks local to startup validation.
- Avoid deployment or production-server changes.
- Add focused tests for unsafe permissions, ownership where feasible, symlinks, directories, and unexpected file types.

### Validation plan
- Run focused P11-R6 tests.
- Run full doctest suite.
- Run clean rebuild for `containercp_tests` and `containercpd`.

### Status
- [ ] Not complete

## P11-R7 — Complete End-to-End Production Upgrade Test

### Problem
No single test simulates the full production upgrade path from TXT storage to SQLite activation, runtime read/write, restart, and validation.

### Required behavior
The end-to-end production test must perform:
- TXT storage.
- Manual migration.
- Verification.
- Archive creation.
- Activation state creation.
- Daemon startup path.
- Runtime read.
- Runtime write.
- Restart.
- Runtime read again.
- Validation succeeds.
- No TXT fallback.

### Implementation plan
- Add one complete integration test using the closest available in-process startup and runtime storage path.
- Use existing fixture data and migration orchestration.
- Exercise a runtime write after activation.
- Reopen through startup validation and verify persisted data.
- Assert no legacy TXT fallback files are created for SQLite-mode writes.

### Validation plan
- Run focused P11-R7 test.
- Run full doctest suite.
- Run clean rebuild for `containercp_tests` and `containercpd`.

### Status
- [ ] Not complete

## Final Production Validation Report

### Required evidence
- Commit SHA for every checklist item.
- Focused test evidence for every checklist item.
- Full test-suite evidence.
- Clean rebuild evidence.
- Git status evidence.
- Production readiness summary.

### Status
- [ ] Not complete
