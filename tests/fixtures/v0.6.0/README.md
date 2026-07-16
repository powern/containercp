# v0.6.0 Storage Fixtures

Immutable test fixtures representing ContainerCP v0.6.0 pipe-delimited
TXT storage formats. These are used by the ARCH-008 migration test suite
to verify that legacy data is parsed and migrated correctly.

## Directory structure

```
v0.6.0/
  README.md                 — this file
  normal/                   — well-formed active + singleton datasets (18 files)
  legacy/                   — legacy format variants + migration-only template_profiles.db
  sentinels/                — records with sentinel values (site_id=0, domain_id=0, node_id=0, orphan FKs)
  empty/                    — empty directory (verifies empty-load behavior)
  malformed/                — deliberately corrupted files for parser tolerance testing
```

## Terminology

| Term | Meaning | Count |
|------|---------|-------|
| Active resource file | Persists a vector of resource records (17 types) | 17 |
| Singleton file | Persists a single string value (mail_state, mail_smarthost) | 2 |
| **Total normal active/singleton** | All well-formed files for current Storage load methods | **19** |
| Migration-only file | Read once by `migrate_template_profiles()`; not standard Storage | 1 |
| Legacy variant | Same resource, older field format | 3 |
| Sentinel | Records with special sentinel FK values | 5 |
| Malformed | Deliberately corrupted for parser tolerance testing | 7 |

**Clarification on the 19 normal files:** Only 18 of these are loaded
by standard `Storage::load_*()` methods (17 active resource types +
2 singleton strings = 19 logical entry points, but 2 singletons share
1 active resource table in SQLite). The 19th file is the `template_profiles.db`
migration-only file, which is NOT loaded by `load_profiles()` — it is
read by the separate `migrate_template_profiles()` method.

## Normal fixtures (18 files, 19 persisted entry points)

| File | Resource | Records | Storage method | Format |
|------|----------|---------|----------------|--------|
| `nodes.db` | Node | 1 | `load_nodes()` | current (3 fields) |
| `sites.db` | Site | 3 | `load_sites()` | current (6-field) |
| `users.db` | User | 2 | `load_users()` | current (6 fields) |
| `domains.db` | Domain | 4 | `load_domains()` | current (7 fields) |
| `php_versions.db` | PhpVersion | 3 | `load_php_versions()` | current (5 fields) |
| `databases.db` | Database | 2 | `load_databases()` | current (9 fields, plaintext passwords) |
| `backups.db` | Backup | 2 | `load_backups()` | current (10 fields) |
| `ssl_certificates.db` | SslCertificate | 1 | `load_ssl_certificates()` | current (20-field) |
| `mail_domains.db` | MailDomain | 2 | `load_mail_domains()` | current (12-field) |
| `mail_mailboxes.db` | Mailbox | 2 | `load_mailboxes()` | current (13 fields, password hashes) |
| `mail_aliases.db` | MailAlias | 1 | `load_mail_aliases()` | current (7 fields) |
| `mail_state.db` | Mail state | 1 | `load_mail_module_state()` | singleton (single line) |
| `mail_smarthost.db` | Smarthost config | 1 | `load_mail_smarthost()` | singleton (single line, plaintext password) |
| `access_users.db` | AccessUser | 1 | `load_access_users()` | current (5 fields, password hash) |
| `access_grants.db` | AccessGrant | 1 | `load_access_grants()` | current (4 fields) |
| `reverse_proxies.db` | ReverseProxy | 2 | `load_reverse_proxies()` | current (8 fields) |
| `profiles.db` | Profile | 2 | `load_profiles()` | current (9 fields) |
| `auth_users.db` | AuthUser | 1 | `load_auth_users()` | current (6 fields, password hash) |

**Note:** `mail_state.db` and `mail_smarthost.db` are singleton files —
each contains exactly one line. They are separate files in TXT format
but merge into a single `mail_config` table in SQLite.

## Migration-only fixture

| File | Resource | Records | Format | Storage method |
|------|----------|---------|--------|----------------|
| `legacy/template_profiles.db` | Profile | 2 | 8-field (no type field) | `migrate_template_profiles()` |

The `template_profiles.db` format differs from `profiles.db`:
- No `type` field (hardcoded to `WEB_SERVER` by the migration code).
- 8 fields: `id|profile_name|web_server|runtime|template_path|description|enabled|default_profile`
- Read once during startup migration, then the file is deleted by
  `ServiceRegistry`. The `migrate_template_profiles()` method itself
  does NOT delete the file.

## Legacy format fixtures

| File | Resource | Records | Format |
|------|----------|---------|--------|
| `legacy/sites_5field.db` | Site | 2 | Legacy 5-field (no php_mail_enabled) |
| `legacy/ssl_certificates_4field.db` | SslCertificate | 1 | Legacy 4-field (pre-v0.5 SSL) |
| `legacy/mail_domains_10field.db` | MailDomain | 1 | Legacy 10-field (no site_id, no dkim_public_key_dns) |

## Sentinel fixtures

| File | Sentinel | Value | Meaning |
|------|----------|-------|---------|
| `sentinels/reverse_proxies_site0.db` | site_id | 0 | Admin panel proxy |
| `sentinels/domains_site0.db` | site_id | 0 | Orphan domain (site deleted) |
| `sentinels/mail_domains_external.db` | domain_id, site_id | 0, 0 | External domain (not in Domain table) |
| `sentinels/backups_orphan.db` | site_id | 99 | Backup for deleted site |
| `sentinels/sites_node0.db` | node_id | 0 | Site on default/local node |

## Malformed fixtures

| File | Corruption type | Behavior |
|------|-----------------|----------|
| `malformed/wrong_field_count.db` | Record with too few fields, then record with too many | Current parser tolerates both: missing fields get stale token, extra fields ignored |
| `malformed/invalid_int.db` | Non-numeric string in integer field | `std::stoull` throws `std::invalid_argument` |
| `malformed/duplicate_id.db` | Two records with same primary key | Parser does not reject duplicates |
| `malformed/empty_file.db` | Zero-byte file | Returns empty vector |
| `malformed/multiline_corruption.db` | Newline embedded in record data | Split line causes `std::stoull` on non-numeric text → throws |
| `malformed/truncated_record.db` | Record truncated mid-line | Partial record may parse with stale token values |
| `malformed/delimiter_collision.db` | Unescaped pipe character inside intended field value | Pipe splits field → extra parsed fields, extra fields ignored |

**Note on delimiter collision:** The `delimiter_collision.db` fixture
has a second record where the intended domain `"site|name|with|pipe"`
contains pipe characters. The pipe-delimited parser splits at each
`|`, producing 8 fields instead of the expected 6. The extra fields
are silently ignored, and the shifted values produce incorrect
parsed data rather than an error. This documents the current parser's
lack of delimiter escaping.

## Usage

Fixtures are loaded by `test_fixture_loader.cpp` which copies fixture
files to a temporary directory, creates a `Storage` instance pointing
to that directory, and calls `load_*()` methods. The tests verify that:

1. Normal fixtures parse to the expected record count.
2. Legacy fixtures are correctly identified by format detection.
3. Sentinel values are preserved.
4. Migration-only fixtures parse through the dedicated method.
5. Empty directories return empty vectors.
6. Malformed fixtures exhibit documented current behavior (throw or partial parse).

## Security notes

- All password hashes are fictional placeholder values.
- No real credentials are included.
- Database passwords are plaintext placeholder values (matching current behavior).
- Smarthost password is a placeholder value.
