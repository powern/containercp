# Migration Verification API — Phase 9 Final

## Files
- `libs/storage/Verification.h/.cpp` — verification pipeline
- `libs/storage/StorageCanonicalizer.h` — shared canonical functions
- `libs/storage/SQLiteSnapshotReader.h` — checked typed snapshots
- `libs/storage/LegacyDatasetReader.h/.cpp` — unified legacy TXT parser
- `libs/storage/LegacyImporter.h/.cpp` — import with baseline capture

## Result types

```cpp
struct FieldMismatch {
    std::string resource_type; uint64_t record_id;
    std::string source;  // "storage" or "checked_sqlite"
    std::string field; std::string expected; std::string actual;
};

struct CheckedSnapshot<T> { bool success; std::vector<T> records; std::string error; };
struct CheckedOptionalValue { bool success, present; std::string value, error; };
```

## SQLiteSnapshotReader

Checked typed loads. Every `read_*()` method validates: ReadLease, prepare, step/DONE, row conversion. Empty table succeeds. Any query failure returns `success=false`.

`read_mail_config_key(key)` validates: lease, prepare, bind, step, second-step DONE, error_code. Absent key = `present=false`. Present empty = `present=true, value=""`.

## MailConfigState (LegacyDatasetReader)

`read_mail_config()` returns `MailConfigResult` with explicit `module_state_present`, `smarthost_present` flags, plus values. Presence is determined by file existence and content, not string emptiness.

## Baseline capture (fail-closed)

`capture_baseline(type)` uses SQLiteSnapshotReader exclusively. Snapshot failure → `baseline_capture_failed` → import stops before parse/write. Mail_config checks both `ms.success` and `sh.success`.

## Skipped verification

Typed loads + canonical checksum comparison against baseline. Count and checksum must both match.

## Initial verification pipeline

1. Import context validation (17 resources)
2. Pool init + schema check
3. 17 per-resource verifications (LegacyDatasetReader + field adapters + checksums)
4. Fail-closed PRAGMA FK + integrity
5. Freeze typed evidence via SQLiteSnapshotReader (fail-closed, checksum verified)

## Production Storage reopen

1. Pool shutdown
2. `Storage(storage_dir_, CoreStorageBackend::SqlitePhase5)`
3. All runtime resources: Storage load + checked SQLite load
4. **Dual field comparison**: expected vs Storage (source=`storage`) + expected vs checked SQLite (source=`checked_sqlite`)
5. Mail_config with presence-aware canonicalization
6. Fail-closed support pools
7. Exactly 17 unique reopened results

## Sensitive redaction

`db_password`, `password_hash`, `key_path`, `dkim_private_key_path`, `smarthost` → `[REDACTED]` in both `storage` and `checked_sqlite` mismatch sources.

## Manual invocation only. Phase 10/11 not started.
