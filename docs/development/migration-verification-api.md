# Migration Verification API — Phase 9 Final

## Files
- `libs/storage/Verification.h/.cpp` — verification pipeline
- `libs/storage/StorageCanonicalizer.h` — shared canonical functions (17 types + presence-aware mail_config)
- `libs/storage/SQLiteSnapshotReader.h` — checked typed snapshots (16 types + CheckedOptionalValue)
- `libs/storage/LegacyDatasetReader.h/.cpp` — unified legacy TXT parser
- `libs/storage/LegacyImporter.h/.cpp` — import with baseline capture

## Result types

```cpp
struct FieldMismatch {
    std::string resource_type; uint64_t record_id;
    std::string source;     // "storage" or "checked_sqlite" (reopen)
    std::string field; std::string expected; std::string actual;
};

struct ResourceVerificationResult {
    bool success; VerificationStatus status; std::string resource_type;
    uint64_t legacy_record_count; uint64_t sqlite_record_count;
    std::string legacy_checksum; std::string sqlite_checksum;
    std::string storage_checksum_alt;   // production Storage (reopen)
    std::string checked_checksum_alt;   // checked SQLite (reopen)
    std::vector<FieldMismatch> mismatches;
    std::string error; std::string diagnostics;
};

struct ResourceBaseline { bool success; uint64_t record_count; std::string canonical_checksum; std::string error; };
struct CheckedSnapshot<T> { bool success; std::vector<T> records; std::string error; };
struct CheckedOptionalValue { bool success, present; std::string value, error; };
```

## SQLiteSnapshotReader

Checked typed loads. Each `read_*()` validates: ReadLease, prepare, step/DONE, row conversion.
- Empty table: `success=true, records=[]`
- Query failure: `success=false, error=no_lease|prepare_failed|step_failed|row_convert_failed`
- `read_mail_config_key(key)`: validates DONE after ROW, distinguishes absent/present-empty/non-empty

Row conversion validates: ProfileType, Permission, MailDomainMode enums, uint64 non-negative, non-null IDs.

## Legacy DatasetReader (shared parser)

Single authoritative TXT parser. 16 `read_*()` + `read_mail_config()` with presence-aware `MailConfigResult`:
- `module_state_present` / `smarthost_present` — determined by file existence, not content
- Missing → `present=false`. Present-empty → `present=true, value=""`

## Baseline capture (fail-closed)

`LegacyImporter::capture_baseline(type)` uses `SQLiteSnapshotReader`. Snapshot failure → `baseline_capture_failed`. Import stops before parse/write. Checksum matches `StorageCanonicalizer`.

## Initial verification pipeline

1. Import context validation (17 resources, no dupes, no Failed, no template_profiles)
2. Pool init + schema check
3. 17 per-resource verifications (LegacyDatasetReader + field adapters + checksums + transient)
4. Fail-closed PRAGMA FK + integrity
5. Freeze typed evidence via SQLiteSnapshotReader (fail-closed, checksum verified)

## Skipped verification

For `SkippedMissingOptional`/`SkippedEmpty`: typed load + canonical checksum vs baseline. Both must match. Baseline evidence populated in result.

## Production Storage reopen

1. Pool shutdown
2. `Storage(storage_dir, CoreStorageBackend::SqlitePhase5)`
3. All runtime resources via `load_*_checked()` — explicit success flag
4. **Dual field comparison**: expected vs Storage (`source=storage`) + expected vs checked SQLite (`source=checked_sqlite`)
5. Mail_config via `CheckedOptionalValue` with presence flags
6. Importer-only (backups, auth_users) via direct SQLite + field comparison
7. Fail-closed support pools
8. Exactly 17 unique reopened results (dup/missing/unknown rejected)
9. Post-reopen FK/integrity checks (separate initial/reopened evidence)

## Sensitive redaction

`db_password`, `password_hash`, `key_path`, `dkim_private_key_path`, `smarthost` → `[REDACTED]` in both `storage` and `checked_sqlite` mismatch sources.

## Manual invocation only. Phase 10/11 not started.
