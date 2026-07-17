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

## Public API

```cpp
class Verification {
public:
    Verification(const std::string& legacy_directory,
                 const std::string& sqlite_path,
                 const ImportAllResult& import_result);

    ResourceVerificationResult verify_nodes();
    // ... all 17 resources same pattern ...
    ResourceVerificationResult verify_mail_config();

    DatabaseVerificationResult verify_all();

    static std::string sha256(const std::string& data);
    static void append_field(std::string& out, const std::string& value);
    static void append_field(std::string& out, uint64_t value);
};
```

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
    std::string integrity_check_result;
    std::vector<std::string> foreign_key_violations;
    bool initial_verification_passed;
    bool reopen_succeeded;
    bool reopened_verification_passed;
    std::string error;
};
```

## Import context

Verification requires a completed `ImportAllResult`.  It uses per-resource
dispositions to determine what to verify:

| Disposition | Verification behavior |
|-------------|----------------------|
| `Imported` | Full parse + load + canonical comparison |
| `SkippedMissingOptional` | `Skipped` — no check |
| `SkippedEmpty` | `Skipped` — no check |
| `Failed` | Not expected — construction from a failed import result is undefined |

`verify_all()` returns `initial_verification_passed = false` when
`import_result.success == false` or any resource verification fails.

## Canonical framing

Each resource is serialized to a deterministic byte stream for SHA-256
checksumming:

- **Length-prefixed fields:** Each field is prefixed by its byte length
  as a big-endian 8-byte `uint64_t`, followed by the field content.
- **Record order:** Records are sorted by `id` ascending.
- **Field order:** As declared in schema (documented per resource below).
- **Booleans:** `"true"` or `"false"` literal strings.
- **Integers:** Decimal string representation.
- **Empty strings:** Length 0 with correct 8-byte prefix.

This format avoids ambiguity: `"a|bc"` vs `"ab|c"` produce different
length-prefixed streams because the boundaries are explicit.

## SHA-256 contract

- Uses OpenSSL's `SHA256_Init/Update/Final`.
- Output: lowercase 64-character hexadecimal.
- Stable across process restarts, input order, and platform.
- Computed over the exact canonical byte stream.

## Checksum + count + field comparison

Verification proceeds in three ordered stages:

1. **Count comparison:** `legacy_record_count == sqlite_record_count`.
   Mismatch → `Failed` immediately (no field comparison).

2. **Checksum comparison:** `SHA256(legacy_canonical) == SHA256(sqlite_canonical)`.
   Match → `Passed`.

3. **Field-by-field comparison** (only when counts match but checksums
   differ): Iterates both sorted-by-ID vectors, matching records by ID.
   Reports up to 100 mismatches per resource.  Each mismatch includes
   the record ID, field name/path, expected canonical value, and actual
   canonical value.

## Per-resource field coverage

### Node
`id`, `name`, `type`

### PhpVersion
`id`, `version`, `image`, `enabled`, `default_version`

### Profile
`id`, `profile_name`, `type`, `web_server`, `runtime`, `template_path`,
`description`, `enabled`, `default_profile`

Combined from `profiles.db` + `template_profiles.db` following the
Phase 8 combined contract.

### User
`id`, `username`, `uid`, `home_directory`, `shell`, `enabled`

### Site
`id`, `domain`, `owner`, `node_id`, `web_server`, `php_mail_enabled`

### Domain
`id`, `fqdn`, `owner_id`, `site_id`, `php_version`, `ssl_enabled`,
`enabled`, `type`, `target`

### Database
`id`, `db_name`, `db_user`, `db_password`, `engine`, `version`,
`owner_id`, `site_id`, `enabled`

### Backup
`id`, `site_id`, `owner_id`, `filename`, `type`, `size`, `created_at`,
`status`, `file_path`, `compression`

### ReverseProxy
`id`, `domain`, `site_id`, `provider`, `config_path`, `upstream`,
`enabled`, `status`

### AccessUser
`id`, `username`, `auth_type`, `password_hash`, `enabled`

### AccessGrant
`id`, `access_user_id`, `site_id`, `permission`

### AuthUser
`id`, `username`, `password_hash`, `must_change_password`, `enabled`,
`role`

### SslCertificate
`id`, `domain_id`, `domain`, `provider`, `certificate_path`, `key_path`,
`chain_path`, `issued_at`, `expires_at`, `renew_after`, `status`,
`auto_renew`, `https_enabled`, `redirect_enabled`, `domains`,
`challenge_type`, `last_error`, `last_validation`, `renew_attempts`,
`version`

### MailDomain
`id`, `domain_id`, `site_id`, `domain_name`, `mode`, `relay_host`,
`dkim_selector`, `dkim_private_key_path`, `dkim_public_key_dns`,
`max_mailboxes`, `max_aliases`, `catch_all`, `enabled`, `created_at`,
`updated_at`

### Mailbox
`id`, `domain_id`, `local_part`, `password_hash`, `quota_bytes`,
`quota_messages`, `enabled`, `display_name`, `forward_to`,
`spam_enabled`, `last_login`, `created_at`, `updated_at`

### MailAlias
`id`, `domain_id`, `source_local_part`, `destination`, `enabled`,
`created_at`, `updated_at`

### Mail config
`module_state` (key), `smarthost` (key) — sorted by key.

## Checked SQLite loads

Verification does not use `SQLiteStorage::load_*` methods (which return
empty on error).  Instead it uses `checked_query<T>()` which returns a
`CheckedLoadResult<T>` with explicit `success`, `records`, and `error`
fields.  Any query failure (prepare, step, connection) is propagated as
a verification failure.

## Sensitive data

Sensitive fields (database passwords, password hashes, smarthost config,
DKIM private-key paths, SSL key paths) participate in SHA-256 checksum
verification but are **redacted** from mismatch reports:

```
expected = "[REDACTED]"
actual = "[REDACTED]"
```

The complete canonical byte stream containing sensitive values is used
only for SHA-256 computation and is never persisted or exposed.

## Integrity check

```sql
PRAGMA integrity_check;
```

Expected result: `"ok"`. Any other value fails verification.  The result
string is captured in `DatabaseVerificationResult::integrity_check_result`.

## Foreign-key check

```sql
PRAGMA foreign_key_check;
```

Expected result: zero rows returned.  Any row (table name, rowid)
indicates a violation and fails verification.  Violations are captured
in `DatabaseVerificationResult::foreign_key_violations`.

## Production reopen

When `verify_all()` passes initial verification:

1. The original `ConnectionPool` is shut down cleanly.
2. A fresh `Storage` instance is created against the same SQLite path
   with `CoreStorageBackend::SqlitePhase5`.
3. Runtime SQLite-backed resources (nodes, php_versions, profiles) are
   reloaded through the normal `Storage` API.
4. Counts are compared against the initial verification counts.
5. `reopen_succeeded` and `reopened_verification_passed` are set.

Importer-only tables (backups, auth_users) remain TXT-delegated at
runtime — verification uses direct SQLite queries for these.

## Backend boundary

Verification never modifies SQLite data.  It never creates archives,
writes to TXT files, or activates SQLite by default.  All operations
are read-only.

## Manual invocation only

Verification is never wired into daemon startup or Storage construction.
It is invoked explicitly by tests and future migration orchestration.
