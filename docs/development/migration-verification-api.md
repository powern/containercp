# Migration Verification API

## Purpose

The `Verification` class proves that data imported from legacy TXT storage into SQLite by `LegacyImporter` is equivalent. Verification compares canonical parsed legacy data against SQLite data loaded through checked queries.

Verification is invoked **explicitly only** — never during daemon startup. Phase 10 (legacy archive) and Phase 11 (startup migration gate) are not implemented.

## File

`libs/storage/Verification.h` / `.cpp`

## Result types

```cpp
enum class VerificationStatus { Passed, Failed, Skipped };

struct FieldMismatch {
    std::string resource_type;
    uint64_t record_id;
    std::string field;
    std::string expected;    // "[REDACTED]" for sensitive fields
    std::string actual;      // "[REDACTED]" for sensitive fields
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
    std::vector<ResourceVerificationResult> resources;         // initial
    std::vector<ResourceVerificationResult> reopened_resources; // post-reopen
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

```cpp
struct ResourceBaseline {
    bool success;              // false if capture failed
    uint64_t record_count;     // pre-import SQLite record count
    std::string canonical_checksum;  // SHA-256 of canonical serialized pre-import records
    std::string error;         // safe error category on failure
};
```

Captured before each import via `LegacyImporter::capture_baseline(type)`. Uses `SQLiteStorage::load_*()` + `Verification::append_field()` for canonical serialization identical to Verification's format (booleans as "true"/"false", enums via typed converters).

On failure: import stops with `baseline_capture_failed`.

## Public API

```cpp
class Verification {
public:
    Verification(const std::string& legacy_directory,
                 const std::string& sqlite_path,
                 const ImportAllResult& import_result,
                 const std::string& storage_directory = "");

    ResourceVerificationResult verify_nodes();
    // ... all 17 resources
    ResourceVerificationResult verify_mail_config();
    DatabaseVerificationResult verify_all();

    static std::string sha256(const std::string& data);
    static void append_field(std::string& out, const std::string& value);
    static void append_field(std::string& out, uint64_t value);
    // canonical_* functions (public for testing)
};
```

## Skipped resource verification

For `SkippedMissingOptional` and `SkippedEmpty`:
1. Checked-load all current typed SQLite records
2. Compute count + canonical SHA-256
3. Compare both against `ImportResult::baseline`
4. Pass only on exact match; fail with `baseline_mismatch` otherwise

## Import context validation

`verify_all()` requires exactly 17 resources: nodes, php_versions, profiles, users, sites, domains, databases, backups, reverse_proxies, access_users, access_grants, auth_users, ssl_certificates, mail_domains, mail_mailboxes, mail_aliases, mail_config. Rejects missing, duplicate, unknown, `template_profiles` as independent, and `Failed` dispositions.

## Production Storage reopen

After initial verification:
1. `pool_.shutdown()` — original pool closed
2. `Storage(storage_dir_, CoreStorageBackend::SqlitePhase5)` — fresh production instance
3. All 14 runtime resources loaded via Storage (count + canonical checksum)
4. Checked confirmation pool verifies each resource count + checksum
5. mail_config loaded via Storage + confirmed via direct query
6. Importer-only tables (backups, auth_users) via full typed checked loads
7. Post-reopen FK (`checked_fk_check`) and integrity (`checked_integrity`) via fresh pool
8. All support pools fail-closed (init failure → reopen failure)

## Complete reopened comparison

Every runtime resource is compared after reopen:
- count matches expected
- Storage canonical checksum matches checked-query canonical checksum
- `reopened_resources` vector stores per-resource results

## Sensitive field redaction

| Field | Type | Redacted |
|-------|------|----------|
| `db_password` | Database | `[REDACTED]` |
| `password_hash` | AccessUser, AuthUser, Mailbox | `[REDACTED]` |
| `key_path` | SslCertificate | `[REDACTED]` |
| `dkim_private_key_path` | MailDomain | `[REDACTED]` |
| `smarthost` | mail_config | `[REDACTED]` |

Sensitive values participate in SHA-256 checksums internally but never appear in `FieldMismatch.expected` or `.actual`.

## Transient reconstruction validation

Verified after SQLite load: `name == domain`, `name == username`, `name == profile_name`, `name == version`, `name == local_part`, etc. Transient mismatch fails the resource.

## Manual invocation only

Verification is never wired into daemon startup. Phase 10/11 not started.
