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

```cpp
enum class VerificationStatus { Passed, Failed, Skipped };

struct FieldMismatch {
    std::string resource_type;
    uint64_t record_id;
    std::string field;
    std::string expected;
    std::string actual;
};

struct ResourceVerificationResult {
    bool success;
    VerificationStatus status;
    std::string resource_type;
    uint64_t legacy_record_count;
    uint64_t sqlite_record_count;
    std::string legacy_checksum;
    std::string sqlite_checksum;
    std::vector<FieldMismatch> mismatches;
    std::string error;
    std::string diagnostics;
};

struct DatabaseVerificationResult {
    bool success;
    std::vector<ResourceVerificationResult> resources;
    std::vector<ResourceVerificationResult> reopened_resources;
    std::string initial_integrity_check_result;
    std::string reopened_integrity_check_result;
    std::vector<std::string> initial_foreign_key_violations;
    std::vector<std::string> reopened_foreign_key_violations;
    bool initial_verification_passed;
    bool reopen_succeeded;
    bool reopened_verification_passed;
    std::string error;
};
```

## ResourceBaseline (captured by LegacyImporter)

Before each import operation, `LegacyImporter::capture_baseline(type)`
returns:
- `success` — false if the baseline could not be captured
- `record_count` — number of rows in the SQLite table before import
- `canonical_checksum` — SHA-256 of canonical serialized pre-import records
- `error` — safe error category

On failure, import stops with `baseline_capture_failed`.

## Skipped resource verification

For `SkippedMissingOptional` and `SkippedEmpty`, Verification:
1. Checked-loads all current SQLite records for the resource type
2. Computes count and SHA-256 using the same canonical function
3. Compares both against `ImportResult::baseline`
4. Fails with `baseline_mismatch` if either differs

## Field-by-field comparison

Every resource has an explicit field adapter function. Each field reports:
- Field name
- Safe expected value
- Safe actual value
- Sensitive flag (redacted to `[REDACTED]`)

## Transient reconstruction

After SQLite load, derived fields are validated:
- `name == domain`, `name == username`, etc.

## Production Storage reopen

After initial verification:
1. `pool_.shutdown()`
2. `Storage(storage_dir_, CoreStorageBackend::SqlitePhase5)`
3. All runtime resources loaded through Storage (count check)
4. Checked confirmation pool verifies each resource count
5. Importer-only tables (backups, auth_users) via full typed loads
6. Post-reopen `PRAGMA foreign_key_check` and `integrity_check`

All support pools are fail-closed.

## Manual invocation only

Verification is never wired into daemon startup.  Phase 10/11 not started.
