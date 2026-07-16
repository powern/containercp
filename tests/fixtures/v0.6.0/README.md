# v0.6.0 Storage Fixtures

Immutable test fixtures representing ContainerCP v0.6.0 pipe-delimited
TXT storage formats. These are used by the ARCH-008 migration test suite
to verify that legacy data is parsed and migrated correctly.

## Directory structure

```
v0.6.0/
  README.md                 — this file
  normal/                   — well-formed datasets for all 19 resource types
  legacy/                   — legacy format variants (5-field sites, 4-field SSL, 10-field mail)
  sentinels/                — records with sentinel values (site_id=0, domain_id=0, node_id=0, orphan FKs)
  empty/                    — empty directory (verifies empty-load behavior)
  malformed/                — deliberately corrupted files for parser tolerance testing
```

## Normal fixtures (19 files)

| File | Resource | Records | Format |
|------|----------|---------|--------|
| `nodes.db` | Node | 1 | current |
| `sites.db` | Site | 3 | current (6-field) |
| `users.db` | User | 2 | current |
| `domains.db` | Domain | 4 | current |
| `php_versions.db` | PhpVersion | 3 | current |
| `databases.db` | Database | 2 | current (with plaintext passwords) |
| `backups.db` | Backup | 2 | current |
| `ssl_certificates.db` | SslCertificate | 1 | current (20-field) |
| `mail_domains.db` | MailDomain | 2 | current (12-field) |
| `mail_mailboxes.db` | Mailbox | 2 | current (with password hashes) |
| `mail_aliases.db` | MailAlias | 1 | current |
| `mail_state.db` | Mail state | 1 | single-line string |
| `mail_smarthost.db` | Smarthost | 1 | single-line config (with password) |
| `access_users.db` | AccessUser | 1 | current (with password hash) |
| `access_grants.db` | AccessGrant | 1 | current |
| `reverse_proxies.db` | ReverseProxy | 2 | current |
| `profiles.db` | Profile | 2 | current |
| `auth_users.db` | AuthUser | 1 | current (with password hash) |

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

| File | Corruption type |
|------|-----------------|
| `malformed/wrong_field_count.db` | Record with too few fields, then record with too many |
| `malformed/invalid_int.db` | Non-numeric string in integer field |
| `malformed/duplicate_id.db` | Two records with same primary key |
| `malformed/empty_file.db` | Zero-byte file |
| `malformed/multiline_corruption.db` | Newline embedded in record data |
| `malformed/truncated_record.db` | Record truncated mid-line |
| `malformed/invalid_delimiter.db` | Tilde character in field value |

## Usage

Fixtures are loaded by `test_fixture_loader.cpp` which copies fixture
files to a temporary directory, creates a `Storage` instance pointing
to that directory, and calls `load_*()` methods. The tests verify that:

1. Normal fixtures parse to the expected record count.
2. Legacy fixtures are correctly identified by format detection.
3. Sentinel values are preserved.
4. Empty directories return empty vectors.
5. Malformed fixtures exhibit documented current behavior (throw or partial parse).

## Security notes

- All password hashes are fictional placeholder values.
- No real credentials are included.
- Database passwords are plaintext placeholder values (matching current behavior).
- Smarthost password is a placeholder value.
