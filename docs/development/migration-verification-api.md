# Migration Verification API

## Purpose

The `Verification` class proves that data imported from legacy TXT storage into SQLite by `LegacyImporter` is equivalent. Verification compares canonical parsed legacy data against SQLite data loaded through checked queries.

Verification is invoked **explicitly only** — never during daemon startup. Phase 10 (legacy archive) and Phase 11 (startup migration gate) are not implemented.

## Files

- `libs/storage/Verification.h` / `.cpp` — verification pipeline
- `libs/storage/StorageCanonicalizer.h` — shared canonical serialization
- `libs/storage/LegacyDatasetReader.h` / `.cpp` — unified legacy TXT parser
- `libs/storage/LegacyImporter.h` / `.cpp` — import with baseline capture
- `docs/development/migration-verification-api.md` — this document

## Result types

```cpp
enum class VerificationStatus { Passed, Failed, Skipped };

struct FieldMismatch {
    std::string resource_type; uint64_t record_id;
    std::string field; std::string expected; std::string actual;
};

struct ResourceVerificationResult {
    bool success; VerificationStatus status; std::string resource_type;
    uint64_t legacy_record_count; uint64_t sqlite_record_count;
    std::string legacy_checksum; std::string sqlite_checksum;
    std::vector<FieldMismatch> mismatches;
    std::string error; std::string diagnostics;
};

struct DatabaseVerificationResult {
    bool success;
    std::vector<ResourceVerificationResult> resources;         // initial
    std::vector<ResourceVerificationResult> reopened_resources; // post-reopen
    std::string initial_integrity_check_result;
    std::string reopened_integrity_check_result;
    std::vector<std::string> initial_foreign_key_violations;
    std::vector<std::string> reopened_foreign_key_violations;
    bool initial_verification_passed; bool reopen_succeeded;
    bool reopened_verification_passed;
    std::string error;
};

struct CheckedOptionalValue {
    bool success; bool present; std::string value; std::string error;
};

struct ResourceBaseline {
    bool success; uint64_t record_count;
    std::string canonical_checksum; std::string error;
};
```

## Public API

```cpp
class Verification {
public:
    Verification(legacy_directory, sqlite_path, import_result, storage_directory = "");

    ResourceVerificationResult verify_nodes(); // ... 17 resources
    DatabaseVerificationResult verify_all();

    static std::string sha256(const std::string& data);
    static void append_field(std::string& out, const std::string& value);
    static void append_field(std::string& out, uint64_t value);
    // canonical_* methods (delegate to StorageCanonicalizer)
};
```

## StorageCanonicalizer (shared)

`libs/storage/StorageCanonicalizer.h` — one authoritative canonical implementation:
- `sha256(data)` — SHA-256 lowercase 64-char hex
- `append_field(out, value)` — length-prefixed field serialization
- `canonical_nodes(records)` ... `canonical_mail_aliases(records)` — per-type canonical encoding
- `canonical_mail_config(ms_present, ms, sh_present, sh)` — presence-aware mail_config

Used by: LegacyImporter (baseline), Verification (checksums), reopen (comparison).

## Baseline capture (fail-closed)

`LegacyImporter::capture_baseline(type)`:
1. Runs `checked_baseline_count()` via ReadLease — fails on lease/prepare/step errors
2. Loads typed records via `SQLiteStorage::load_*()`
3. Compares loaded count against checked COUNT(*) — `baseline_load_mismatch` if different
4. Computes `canonical_checksum` via `StorageCanonicalizer`
5. On ANY failure: `ResourceBaseline.success = false`, import stops with `baseline_capture_failed`

No parser or write runs after baseline failure.

## Skipped resource verification

For `SkippedMissingOptional` and `SkippedEmpty`:
1. Checked-load ALL current typed SQLite records
2. Compute count + canonical SHA-256 via StorageCanonicalizer
3. Compare both against `ImportResult::baseline`
4. Pass → baseline evidence populated; Fail → `baseline_mismatch`

## Initial verification pipeline

1. Validate import context (17 resources, no dupes, no Failed)
2. `pool_.initialize(sqlite_path_)`
3. Verify all 17 resources (LegacyDatasetReader + field adapters + checksums)
4. Checked `PRAGMA foreign_key_check` (fail-closed)
5. Checked `PRAGMA integrity_check` (fail-closed — must return "ok")
6. Freeze initial evidence (count + checksum + typed vectors)
7. `pool_.shutdown()`

## Production Storage reopen

1. `Storage(storage_dir_, CoreStorageBackend::SqlitePhase5)`
2. All 14 runtime resources loaded via Storage + count/checksum compared against initial evidence
3. Checked confirmation pool verifies each resource count
4. mail_config via Storage + direct query confirmation
5. Importer-only (backups, auth_users) via direct SQLite + typed field comparison
6. Post-reopen FK/integrity via fail-closed helpers on fresh pool
7. Exactly 17 unique reopened results verified

## Reopened field comparison

When reopened checksums differ from initial evidence:
- `compare_resource()` runs field-by-field adapters
- FieldMismatch entries populated with safe values
- Sensitive fields redacted to `[REDACTED]`
- Transient validation included

## Sensitive redaction

| Field | Redacted |
|-------|----------|
| `db_password` | `[REDACTED]` |
| `password_hash` (AccessUser, AuthUser, Mailbox) | `[REDACTED]` |
| `key_path` | `[REDACTED]` |
| `dkim_private_key_path` | `[REDACTED]` |
| `smarthost` | `[REDACTED]` |

Sensitive values participate in SHA-256 checksums but never appear in mismatch output.

## Manual invocation only

Verification is never wired into daemon startup. Phase 10/11 not started.
