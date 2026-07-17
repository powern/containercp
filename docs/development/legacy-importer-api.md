# LegacyImporter API

## Purpose

The `LegacyImporter` reads ContainerCP v0.6.0 TXT (pipe-delimited) files from a
legacy directory and writes all parsed records into SQLite.  Source files are
**never modified**.  The importer is invoked **explicitly only** — never during
normal daemon startup or Storage construction.

**Phase 9 (migration verification — checksums, canonical comparison, integrity
pragmas) is not implemented.**

## File

`libs/storage/LegacyImporter.h` / `.cpp`

## Public API

```cpp
class LegacyImporter {
public:
    LegacyImporter(const std::string& legacy_directory, ConnectionPool& pool);

    // Per-resource import (17 resource types — profiles and template_profiles
    // map to the same SQLite table and are combined via import_all_profiles)
    ImportResult import_nodes();
    ImportResult import_php_versions();
    ImportResult import_profiles();
    ImportResult import_template_profiles();
    ImportResult import_all_profiles();  // combined profiles + template_profiles
    ImportResult import_users();
    ImportResult import_sites();
    ImportResult import_domains();
    ImportResult import_databases();
    ImportResult import_backups();
    ImportResult import_reverse_proxies();
    ImportResult import_access_users();
    ImportResult import_access_grants();
    ImportResult import_auth_users();
    ImportResult import_ssl_certificates();
    ImportResult import_mail_domains();
    ImportResult import_mail_mailboxes();
    ImportResult import_mail_aliases();
    ImportResult import_mail_config();

    // Import all resources in dependency-safe order.
    // Stops on first real persistence failure; earlier resources remain committed.
    ImportAllResult import_all();
};
```

## Result types

```cpp
struct ImportResult {
    bool success = false;              // false if any error occurred
    ImportDisposition disposition;     // Imported / SkippedMissingOptional / SkippedEmpty / Failed
    std::string resource_type;         // e.g. "nodes", "sites"
    std::string source_file;           // filename(s) read
    uint64_t record_count = 0;         // number of records committed
    std::string error;                 // safe error key (no secrets)
    std::string diagnostics;           // internal diagnostics (no secrets)
};

struct ImportAllResult {
    bool success = false;
    std::vector<ImportResult> resources;  // per-resource results in import order
    std::string failed_resource;          // name of the first failing resource
    std::string error;                    // aggregated safe error
};
```

## Checked persistence contract

LegacyImporter **never** reports `Imported` unless the SQLite write transaction
committed successfully.  The `finish_import` helper returns:

| Condition | success | disposition | error |
|-----------|---------|-------------|-------|
| `checked_saver` returned `true` | `true` | `Imported` | (empty) |
| `checked_saver` returned `false` | `false` | `Failed` | `sqlite_write_failed` |

The importer calls `SQLiteStorage::try_save_*` methods (which return `bool`)
rather than the void `save_*` methods.  The checked saver returns `true` only
when:

- a transaction was started;
- all DELETE / UPSERT / INSERT operations succeeded;
- `COMMIT` succeeded (no rollback occurred).

## FK / write failure error mapping

| Error | Meaning |
|-------|---------|
| `sqlite_write_failed` | Transaction did not commit (FK violation, connection issue, or SQL error) |
| `transaction_failed` | TransactionGuard failed to activate |
| `prepare_failed` | SQL prepare failed |
| `bind_failed` | SQL bind failed |
| `step_failed` | SQL step returned an error |

## File state classification

`LegacyImporter::inspect_file()` classifies source files into six states:

| State | Meaning | Policy |
|-------|---------|--------|
| `Missing` | Path does not exist | Required → `Failed`/`file_missing`; Optional → `SkippedMissingOptional` |
| `RegularReadable` | Regular file with content | Proceed to parse |
| `Empty` | Regular file, zero bytes | `SkippedEmpty` with record_count=0; no SQLite mutation |
| `Unreadable` | Cannot open for reading | `Failed`/`file_unreadable` |
| `InvalidType` | Not a regular file | `Failed`/`invalid_file_type` |
| `ReadError` | I/O error during read | `Failed`/`file_read_error` |

## Empty-file policy

An empty source file produces `SkippedEmpty` with `record_count=0`.  The
checked saver is **not called**, so:

- no mutation occurs in SQLite;
- previously imported rows (from an earlier run) are preserved intact.

This is distinct from a valid empty authoritative dataset (which would call
the saver and erase).  The importer treats a physically empty legacy TXT file
as "no data" rather than "clear all data".

## Duplicate detection

Every resource parser rejects duplicate explicit IDs within the same file
before reaching SQLite.  Detection uses `std::set<uint64_t>` keyed on `id`.

Where the schema enforces a unique constraint on a logical key (e.g.
`username`, `domain`, `fqdn`, `db_name`), the parser also rejects duplicates
on that key with a `duplicate_*` error.

On duplicate detection:

- `success = false`
- `disposition = Failed`
- `error = "duplicate_id"` or `"duplicate_<field>"`
- diagnostics include filename and line number (no raw line content).

Duplicates are detected **during parsing, before any SQLite write**.

## Strict field parsing

All present fields are parsed strictly.  `parse_uint64`, `parse_int`, and
`parse_bool` reject:

- empty fields
- malformed values (e.g. `"abc"` for an integer)
- trailing characters after a valid parse
- overflow / underflow

**Absent historical fields** receive documented defaults.  **Present malformed
fields** cause import failure.  No malformed value is silently converted to a
default.

### Integer parsing (`parse_uint64`)

```cpp
static bool parse_uint64(const std::string& s, uint64_t& out, std::string& err);
```

- Rejects empty string
- Rejects leading `-` (negative)
- Sets `errno = 0` before conversion
- Calls `strtoull`
- Rejects `ERANGE` overflow
- Rejects trailing characters
- Accepts `0` where contract permits

| Input | Result |
|-------|--------|
| `"0"` | `0`, success |
| `"18446744073709551615"` | `UINT64_MAX`, success |
| `"18446744073709551616"` | overflow, failure |
| `"-1"` | negative, failure |
| `""` | empty, failure |
| `"123abc"` | trailing chars, failure |

### Bool parsing (`parse_bool`)

```cpp
static bool parse_bool(const std::string& s, bool& out, std::string& err);
```

- `"1"` → `true`
- `"0"` → `false`
- Everything else → failure, `"invalid boolean (expected 0 or 1)"`

## Trailing empty fields

The pipe splitter (`LineParser::split`) preserves trailing empty fields.

| Input | Fields |
|-------|--------|
| `"1\|name\|"` | `["1", "name", ""]` |
| `"1\|\|"` | `["1", "", ""]` |
| `"1\|name\|value\|"` | `["1", "name", "value", ""]` |
| `"1\|\|value\|"` | `["1", "", "value", ""]` |

This is required for formats containing optional final fields.

## Supported formats

### Current (18 resources)

| Resource | Fields | Required |
|----------|--------|----------|
| nodes | 3 | Yes |
| php_versions | 5 | Yes |
| profiles | 9 | Yes |
| template_profiles | 8 (WEB_SERVER type assumed) | No |
| users | 6 | Yes |
| sites | 6 | Yes |
| domains | 7+ | Yes |
| databases | 9 | Yes |
| backups | 10 | Yes |
| reverse_proxies | 8 | Yes |
| access_users | 5 | No |
| access_grants | 4 | No |
| auth_users | 6+ | No |
| ssl_certificates | 20 | No |
| mail_domains | 12 | No |
| mail_mailboxes | 13 | No |
| mail_aliases | 7 | No |
| mail_config (mail_state) | 1 (singleton) | No |
| mail_config (smarthost) | 1 (singleton) | No |

### Legacy format detection

| Resource | Legacy delimiter | Legacy fields | Detection |
|----------|-----------------|---------------|-----------|
| sites | pipe count ≤ 5 | 5 (no php_mail_enabled) | `count_pipes() <= 5` |
| ssl_certificates | total fields < 20 | 10 (4 common fields after 6 mandatory) | `f.size() < 20` |
| mail_domains | pipe count ≤ 9 | 10 (no site_id, no dkim_public_key_dns) | `count_pipes() <= 9` |
| template_profiles | 8-field | type hardcoded to WEB_SERVER | distinct filename |

## Dependency order (import_all)

1. nodes
2. php_versions
3. profiles (combined — profiles.db + template_profiles.db via import_all_profiles)
4. users
5. sites (FK child of users)
6. domains
7. databases
8. backups
9. reverse_proxies
10. access_users
11. access_grants (FK child of access_users + sites)
12. auth_users
13. ssl_certificates
14. mail_domains (FK parent of mailboxes + aliases)
15. mail_mailboxes (FK child of mail_domains)
16. mail_aliases (FK child of mail_domains)
17. mail_config

`import_all` appends each resource result and checks `success` after every
step.  On the first `success = false`, `import_all` returns immediately
without attempting later resources.  Earlier resources remain committed.

## mail_config result semantics

`import_mail_config()` returns:

| Condition | success | disposition | record_count |
|-----------|---------|-------------|--------------|
| Both files absent/empty | `true` | `SkippedMissingOptional` | `0` |
| Present files all empty | `true` | `SkippedEmpty` | `0` |
| At least one key committed | `true` | `Imported` | `1` or `2` |
| Any file unreadable/invalid | `false` | `Failed` | `0` |
| SQLite write failure | `false` | `Failed` | `0` |

Module state is validated: must be `"active"` or `"inactive"` (matching
`MailModuleState` serialization).  Any other value produces
`Failed`/`invalid_module_state`.

If state saves successfully but smarthost fails, state remains committed
but overall result is `Failed` with `"sqlite_write_failed"` error.

## Combined profiles import (profiles.db + template_profiles.db)

Both `profiles.db` and `template_profiles.db` map into the same SQLite
`profiles` table.  During `import_all()`, they are treated as one atomic
logical resource via `import_all_profiles()`.

### Behavior

1. `profiles.db` is inspected as a **required** file.
2. `template_profiles.db` is inspected as an **optional** file.
3. Both files are **fully parsed** before any SQLite write.
4. Cross-file **duplicate id** and **duplicate profile_name** are rejected.
5. One checked `try_save_profiles(combined)` call persists the union.
6. On success: `record_count` = total rows from both files.
7. On any failure: no SQLite mutation occurs.

### Cross-file duplicate validation

- Duplicate `id` across `profiles.db` and `template_profiles.db`:
  `Failed` / `duplicate_id`
- Duplicate `profile_name` across the two files: `Failed` /
  `duplicate_profile_name`
- Diagnostics identify both source filenames and the conflicting field

### Optional template file

- **Missing:** profiles.db imports normally.
- **Empty:** profiles.db imports normally; no second empty replacement.
- **Malformed after valid profiles.db:** combined write is skipped,
  no partial commit.

### Standalone API safety

`import_profiles()` replaces the profiles table with only `profiles.db`
data — correct when called standalone.

`import_template_profiles()` is safe standalone: it loads existing SQLite
profiles, merges the parsed template records, validates cross-file
collisions, and saves the combined vector.  It never erases existing
profiles.

`import_all_profiles()` is the combined method used by `import_all()`;
it parses both files atomically.

## Source immutability

The importer opens all source files with `std::ifstream` (read-only).  No
write-mode stream is opened.  No file is created, renamed, deleted, or
modified in the legacy directory.  Verified by SHA-256 byte identity and
file-size comparison in tests.

## Backend boundary

LegacyImporter uses:
- `SQLiteStorage::try_save_*` for 15 of the 17 resource types
- Direct `TransactionGuard` for `backups` and `auth_users` (not exposed by SQLiteStorage at runtime, but schema tables exist)

The importer is never wired into daemon startup or Storage construction.

## Sensitive data

All error messages and diagnostics are safe — they contain resource names,
operation names, field names, line numbers, and error category tags.
No bound values, passwords, or keys are included in diagnostics.
