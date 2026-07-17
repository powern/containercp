# Migration Verification API

## Purpose

The `Verification` class proves that data imported from legacy TXT storage
into SQLite by `LegacyImporter` is equivalent.  Verification compares
canonical parsed legacy data against SQLite data loaded through checked
queries.

Verification is invoked **explicitly only** — never during daemon startup.
Phase 10 (legacy archive) and Phase 11 (startup migration gate) are not
implemented.

## File

`libs/storage/Verification.h` / `.cpp`

## Result types

... (existing result types documented here) ...

## Baseline capture (ResourceBaseline)

Before each Phase 8 import operation, `LegacyImporter` captures a
`ResourceBaseline` containing:

- `success` — `false` if the baseline could not be captured (connection,
  prepare, or step failure)
- `record_count` — number of rows in the SQLite table before import
- `canonical_checksum` — SHA-256 of the canonical serialized pre-import
  SQLite records
- `error` — safe error category if capture failed

Baseline capture failure causes the resource import to fail with
`baseline_capture_failed`.

Baseline is stored in `ImportResult::baseline`.

## Skipped-resource verification

For `SkippedMissingOptional` and `SkippedEmpty` dispositions,
Verification:

1. Checked-loads current SQLite state
2. Computes current count and canonical checksum
3. Compares against the captured baseline
4. Returns `Passed`/`Skipped` only when both count and checksum match
5. Fails with `baseline_mismatch` if state changed

This proves the Phase 8 no-mutation contract.

## Production Storage reopen

After successful initial verification:

1. All ReadLease objects released
2. `pool_.shutdown()` called
3. Fresh `Storage` instance created with `CoreStorageBackend::SqlitePhase5`
4. `sqlite_ready()` confirmed

Every runtime SQLite-backed resource is loaded through Storage and
compared by count.  A second checked SQLite pool confirms Storage results
are not silent failures.

## Post-reopen verification

After reopen:

- All runtime resources counted through Storage
- Checked confirmation pool verifies all 14 runtime resource counts
- Importer-only tables (backups, auth_users) verified through full
  checked loads
- `PRAGMA foreign_key_check` and `PRAGMA integrity_check` run on fresh
  pool

All support pools are fail-closed — initialization failure sets
`reopen_succeeded = false`.

## Manual invocation only

Verification is never wired into daemon startup or Storage construction.
It is invoked explicitly by tests and future migration orchestration.
