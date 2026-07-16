# ARCH-008: SQLite Storage Foundation

**Status:** Draft
**Target Version:** v0.7.0
**Product Owner:** Approved (2026-07-16)
**Epic Size:** Medium-Large

---

## 1. Executive Summary

ContainerCP currently uses hand-rolled pipe-delimited text files (`.db`)
for all persisted state. Each resource type has its own file. Every `save()`
rewrites the entire file. There is no atomicity, no concurrency, no
relational integrity, no indices, and no schema versioning.

ARCH-008 replaces this with a single SQLite database at
`/srv/containercp/database/containercp.db`. The existing `Storage`
interface is preserved so that managers, API handlers, and the Web UI
require zero changes. A versioned migration engine converts existing
`.db` files to SQLite tables, verifies equivalence, and becomes the
foundation for all future schema changes.

The migration runs at daemon startup before any manager, API listener,
or background worker starts. Failure causes fail-fast shutdown with
diagnostic output. Legacy `.db` files are backed up and removed from
the active directory after successful migration.

---

## 2. Current Storage Architecture

### 2.1 Overview

Current persistence lives in `libs/storage/Storage.h` and
`libs/storage/Storage.cpp` (788 lines).

The `Storage` class takes a directory path and exposes type-safe
save/load pairs:

- `save_nodes(vectors)` / `load_nodes()`
- `save_sites(vector)` / `load_sites()`
- `save_users(vector)` / `load_users()`
- `save_domains(vector)` / `load_domains()`
- `save_php_versions(vector)` / `load_php_versions()`
- `save_databases(vector)` / `load_databases()`
- `save_backups(vector)` / `load_backups()`
- `save_ssl_certificates(vector)` / `load_ssl_certificates()`
- `save_mail_domains(vector)` / `load_mail_domains()`
- `save_mailboxes(vector)` / `load_mailboxes()`
- `save_mail_aliases(vector)` / `load_mail_aliases()`
- `save_mail_module_state(string)` / `load_mail_module_state()`
- `save_mail_smarthost(string)` / `load_mail_smarthost()`
- `save_access_users(vector)` / `load_access_users()`
- `save_access_grants(vector)` / `load_access_grants()`
- `save_reverse_proxies(vector)` / `load_reverse_proxies()`
- `save_profiles(vector)` / `load_profiles()`
- `save_auth_users(vector)` / `load_auth_users()`
- `migrate_template_profiles()` — reads legacy `template_profiles.db` and returns `Profile` objects

### 2.2 File-per-type

Each resource type has a dedicated file in `database_dir()`
(default `/srv/containercp/database/`):

| File | Resource |
|------|----------|
| `nodes.db` | `node::Node` |
| `sites.db` | `site::Site` |
| `users.db` | `user::User` |
| `domains.db` | `domain::Domain` |
| `php_versions.db` | `php::PhpVersion` |
| `databases.db` | `database::Database` |
| `backups.db` | `backup::Backup` |
| `ssl_certificates.db` | `ssl::SslCertificate` |
| `mail_domains.db` | `mail::MailDomain` |
| `mail_mailboxes.db` | `mail::Mailbox` |
| `mail_aliases.db` | `mail::MailAlias` |
| `mail_state.db` | Mail module state (single string) |
| `mail_smarthost.db` | Smarthost config (single string) |
| `access_users.db` | `access::AccessUser` |
| `access_grants.db` | `access::AccessGrant` |
| `reverse_proxies.db` | `proxy::ReverseProxy` |
| `profiles.db` | `profile::Profile` |
| `template_profiles.db` | Legacy — read once by `migrate_template_profiles()` |
| `auth_users.db` | `auth::AuthUser` |

**Total: 19 files** (17 active, 2 special/singleton, 1 migration-only).

### 2.3 Serialization format

All files use pipe-delimited (`|`) text, one record per line.

- Fields are positional, not named.
- There is no escaping mechanism for pipes, newlines, or special
  characters in field values.
- Boolean values are stored as `"1"` or `"0"`.
- Integer fields are stored as decimal strings.
- Timestamps are ISO-8601 strings (`YYYY-MM-DDTHH:MM:SSZ`).
- Multiline data (e.g., `last_error`, `dkim_public_key_dns`) is stored
  inline — pipes or newlines in these fields would corrupt the file.

### 2.4 Write semantics

Every `save_*()` call:

1. Opens the file with `std::ofstream` (truncates).
2. Iterates over the in-memory vector.
3. Writes each record as a pipe-delimited line.
4. Closes the file.

This means:

- **No atomicity.** If the process crashes mid-write, the file is
  truncated or contains partial data.
- **No concurrency.** Two simultaneous `save_*()` calls on the same
  file would corrupt it. The current single-threaded CLI design avoids
  this, but the async job executor creates concurrent save paths.
- **No transactional integrity.** Saving a site, its domain, and its
  database requires three separate file writes with no rollback.
- **O(n) per write.** Every save rewrites the entire file regardless
  of how many records changed.

### 2.5 Read semantics

Every `load_*()` call:

1. Opens the file with `std::ifstream`.
2. Reads line by line.
3. Parses pipe-delimited fields by position.
4. Returns the complete vector.

There is no lazy loading, no pagination, no indexed lookup. Loading
1000 sites reads and parses all 1000 records even if only one is
needed.

### 2.6 Format version detection

The code contains multiple ad-hoc format migration paths:

- **Sites:** Counts pipes to distinguish 5-field (legacy) vs 6-field
  format. Sets transient `php_mail_enabled_present` flag.
- **SSL certificates:** Counts remaining fields after common prefix
  to distinguish old format (4 fields) vs new format (14+ fields).
- **Mail domains:** Counts pipes to distinguish 10-field old format
  from 12-field current format.

These are fragile and tested only by production experience.

### 2.7 Who calls Storage

The `ServiceRegistry` constructor (`ServiceRegistry.cpp`) loads all
resource types during startup. The `ServiceRegistry::save()` method
saves all resource types. Individual managers do not call Storage
directly — the `DaemonApp` CLI handler calls `save()` after every
mutating CLI command. API handlers call `save()` after mutating
API operations.

Storage is called from:
- `libs/core/ServiceRegistry.cpp` — constructor and `save()`
- `libs/api/ApiServer.cpp` — various mutation handlers
- `libs/auth/AuthService.cpp` — session persistence
- `libs/site/Site.h` — transient field only
- `libs/mail/DkimManager.h` — filesystem paths, not storage

---

## 3. Complete Inventory of Current TXT Databases

All 19 files are documented in section 2.2. The following represents
the complete persisted state of a ContainerCP v0.6.0 installation.

### 3.1 Active resource files (17)

Each contains zero or more pipe-delimited records. The files are
created empty on first daemon startup when `Storage()` constructor
creates the database directory.

### 3.2 Special singleton files (2)

- `mail_state.db` — single line, no delimiter
- `mail_smarthost.db` — single line, no delimiter

### 3.3 Migration-only file (1)

- `template_profiles.db` — read once by `migrate_template_profiles()`,
  then the disk file is deleted.

---

## 4. Current Resource Serialization Formats

### 4.1 Nodes (`nodes.db`)

```
id|name|type
```

### 4.2 Sites (`sites.db`)

```
id|domain|owner|node_id|web_server|php_mail_enabled
```

Legacy 5-field format (no `php_mail_enabled`):
```
id|domain|owner|node_id|web_server
```
Detected by counting pipe characters.

### 4.3 Users (`users.db`)

```
id|username|uid|home_directory|shell|enabled
```

### 4.4 Domains (`domains.db`)

```
id|fqdn|owner_id|site_id|php_version|ssl_enabled|enabled
```

### 4.5 PHP Versions (`php_versions.db`)

```
id|version|image|enabled|default_version
```

### 4.6 Databases (`databases.db`)

```
id|db_name|db_user|db_password|engine|version|owner_id|site_id|enabled
```

**Contains secrets:** `db_password` is stored in plain text.

### 4.7 Backups (`backups.db`)

```
id|site_id|owner_id|filename|type|size|created_at|status|file_path|compression
```

### 4.8 SSL Certificates (`ssl_certificates.db`)

```
id|domain_id|domain|provider|certificate_path|key_path|chain_path|issued_at|expires_at|renew_after|status|auto_renew|https_enabled|redirect_enabled|domains|challenge_type|last_error|last_validation|renew_attempts|version
```

Legacy 4-field format:
```
id|domain_id|domain|provider|certificate_path|key_path|expires_at|status|enabled|auto_renew
```

**Field with special characters:** `last_error`, `dkim_public_key_dns`,
`domains` (comma-separated list) may contain characters that collide
with the pipe delimiter or break line-based parsing.

### 4.9 Mail Domains (`mail_domains.db`)

Current 12-field format:
```
id|mode_str|domain_name|domain_id|site_id|enabled|catch_all|dkim_selector|dkim_public_key_dns|relay_host|max_mailboxes|max_aliases
```

Legacy 10-field format (no `site_id`, no `dkim_public_key_dns`):
```
id|mode_str|domain_name|owner_id|enabled|catch_all|dkim_selector|relay_host|max_mailboxes|max_aliases
```

### 4.10 Mailboxes (`mail_mailboxes.db`)

```
id|domain_id|local_part|password_hash|quota_bytes|quota_messages|enabled|display_name|forward_to|spam_enabled|last_login|created_at|updated_at
```

**Contains secrets:** `password_hash` is a Dovecot-compatible
SHA-512-CRYPT hash.

### 4.11 Mail Aliases (`mail_aliases.db`)

```
id|domain_id|source_local_part|destination|enabled|created_at|updated_at
```

### 4.12 Access Users (`access_users.db`)

```
id|username|auth_type|password_hash|enabled
```

**Contains secrets:** `password_hash`.

### 4.13 Access Grants (`access_grants.db`)

```
id|access_user_id|site_id|permission_str
```

### 4.14 Reverse Proxies (`reverse_proxies.db`)

```
id|domain|site_id|provider|config_path|upstream|enabled|status
```

### 4.15 Profiles (`profiles.db`)

```
id|profile_name|type_str|web_server|runtime|template_path|description|enabled|default_profile
```

### 4.16 Auth Users (`auth_users.db`)

```
id|username|password_hash|must_change_password|enabled|role
```

**Contains secrets:** `password_hash`.

### 4.17 Legacy Template Profiles (`template_profiles.db`)

```
id|profile_name|web_server|runtime|template_path|description|enabled|default_profile
```

Read by `migrate_template_profiles()`, then the file is deleted.

### 4.18 Mail module state (`mail_state.db`)

Single line: `"inactive"` or `"active"`.

### 4.19 Mail smarthost (`mail_smarthost.db`)

Single line: serialized smarthost config string.
Format: `host:port:username:password:enabled` (pipe-delimited, single line).

---

## 5. Current Resource Relationships

### 5.1 Relationship map

```
Node (nodes.db)
  └── Site.node_id ───────────────────────────── FK to Node.id

Site (sites.db)
  ├── Domain.site_id ──────────────────────────── FK to Site.id
  ├── Database.site_id ────────────────────────── FK to Site.id
  ├── Backup.site_id ──────────────────────────── FK to Site.id
  ├── ReverseProxy.site_id ────────────────────── FK to Site.id (0 = admin panel)
  ├── MailDomain.site_id ──────────────────────── FK to Site.id (0 = unlinked)
  └── AccessGrant.site_id ─────────────────────── FK to Site.id

User (users.db)
  ├── Domain.owner_id ─────────────────────────── FK to User.id (0 = system)
  ├── Database.owner_id ───────────────────────── FK to User.id (0 = system)
  └── Backup.owner_id ─────────────────────────── FK to User.id (0 = system)

Domain (domains.db)
  ├── SslCertificate.domain_id ────────────────── FK to Domain.id (0 = orphan)
  └── MailDomain.domain_id ────────────────────── FK to Domain.id (0 = external)

MailDomain (mail_domains.db)
  ├── Mailbox.domain_id ───────────────────────── FK to MailDomain.id
  └── MailAlias.domain_id ─────────────────────── FK to MailDomain.id

AccessUser (access_users.db)
  └── AccessGrant.access_user_id ──────────────── FK to AccessUser.id

AccessGrant (access_grants.db)
  └── AccessGrant.site_id ─────────────────────── FK to Site.id

ReverseProxy (reverse_proxies.db)
  └── ReverseProxy.site_id ────────────────────── FK to Site.id (0 = admin panel)
```

### 5.2 Sentinel values and special records

| Value | Meaning | Where used |
|-------|---------|------------|
| `id = 0` | Virtual/system resource | Domain.site_id, ReverseProxy.site_id, SslCertificate.domain_id, MailDomain.site_id, MailDomain.domain_id |
| `node_id = 0` | Local/default node | Site.node_id |
| `owner_id = 0` | System/admin owner | Domain.owner_id, Database.owner_id, Backup.owner_id |

### 5.3 Orphan-tolerant relationships

These relationships tolerate the referenced resource being deleted:

- **Backup.site_id** → Site may be deleted but backup record remains
  (backup files are cleaned up by `SiteRemoveOperation`, but the
  record may outlive the site if removal fails midway).
- **SslCertificate.domain_id** → Domain may be deleted; the cert
  record may persist if removal was interrupted.
- **MailDomain.domain_id** = 0 → External domain not managed by
  ContainerCP's Domain module.
- **MailDomain.site_id** = 0 → Unlinked domain mail config.

### 5.4 Synthetic resources

The admin panel virtual site/domain uses `site_id = 0` throughout:

- `SitesViewService.cpp` synthesizes a JSON site with `id: 0`,
  `system_role: "admin-panel"` for the frontend. This is NOT stored
  in any `.db` file.
- `CertificateStore` stores SSL metadata for `site_id = 0` on the
  filesystem (for the admin panel's own HTTPS).
- `ReverseProxy` entries with `site_id = 0` represent the admin
  panel proxy configuration.

---

## 6. Proposed Single-Database Architecture

### 6.1 Database file

**Path:** `/srv/containercp/database/containercp.db`

Single file for all control-plane state. Rationale:
- Cross-resource transactions (create site = insert site + domain + database)
- Relational integrity via foreign keys
- One backup file captures all metadata
- One migration chain, no synchronization between files
- One set of connection settings, one WAL

### 6.2 Storage interface preservation

The existing `Storage` class public API remains unchanged. Every
manager that calls `storage.save_*(vector)` or `storage.load_*()`
continues to work without modification.

Internally, the implementation switches from pipe-delimited file I/O
to parameterized SQL statements.

### 6.3 Source file changes

| File | Change |
|------|--------|
| `libs/storage/Storage.h` | No change to public API |
| `libs/storage/Storage.cpp` | Replace implementation entirely |
| `libs/storage/SQLiteStorage.h` | New: internal SQLite wrapper |
| `libs/storage/SQLiteStorage.cpp` | New: SQLite connection pool, prepared statements |
| `libs/storage/MigrationEngine.h` | New: schema migration framework |
| `libs/storage/MigrationEngine.cpp` | New: migration runner, verification |
| `libs/storage/LegacyImporter.h` | New: TXT → SQLite migration |
| `libs/storage/LegacyImporter.cpp` | New: import logic |
| `CMakeLists.txt` | Add SQLite3 dependency (`find_package(SQLite3)`) |
| `packaging/containercp.service` | No change |
| `scripts/install.sh` | Add `libsqlite3-dev` to build dependencies |
| `scripts/update.sh` | No change |

---

## 7. Proposed Table and Schema Design

### 7.1 Schema versioning table

```sql
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    checksum    TEXT NOT NULL,
    started_at  TEXT NOT NULL,
    completed_at TEXT,
    status      TEXT NOT NULL DEFAULT 'pending',
    diagnostics TEXT
);
```

Valid statuses: `pending`, `running`, `completed`, `failed`.

### 7.2 Resource tables

Each resource type gets its own table. Column names map to struct
field names. All tables include `created_at` and `updated_at` timestamps
for audit purposes.

#### `nodes`

```sql
CREATE TABLE nodes (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    type        TEXT NOT NULL DEFAULT 'local',
    created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
```

#### `sites`

```sql
CREATE TABLE sites (
    id                INTEGER PRIMARY KEY,
    domain            TEXT NOT NULL,
    owner             TEXT NOT NULL DEFAULT '',
    node_id           INTEGER NOT NULL DEFAULT 0 REFERENCES nodes(id),
    web_server        TEXT NOT NULL DEFAULT 'apache',
    php_mail_enabled  INTEGER NOT NULL DEFAULT 0,
    created_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX idx_sites_node_id ON sites(node_id);
```

**Note:** The transient `php_mail_enabled_present` field is NOT stored
in SQLite — it was only needed for the one-time legacy migration that
is now complete.

#### `users`

```sql
CREATE TABLE users (
    id              INTEGER PRIMARY KEY,
    username        TEXT NOT NULL,
    uid             INTEGER NOT NULL DEFAULT 0,
    home_directory  TEXT NOT NULL DEFAULT '',
    shell           TEXT NOT NULL DEFAULT '/usr/sbin/nologin',
    enabled         INTEGER NOT NULL DEFAULT 1,
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
```

#### `domains`

```sql
CREATE TABLE domains (
    id          INTEGER PRIMARY KEY,
    fqdn        TEXT NOT NULL,
    owner_id    INTEGER NOT NULL DEFAULT 0 REFERENCES users(id) ON DELETE RESTRICT,
    site_id     INTEGER NOT NULL DEFAULT 0 REFERENCES sites(id) ON DELETE RESTRICT,
    php_version TEXT NOT NULL DEFAULT '8.4',
    ssl_enabled INTEGER NOT NULL DEFAULT 0,
    enabled     INTEGER NOT NULL DEFAULT 1,
    type        TEXT NOT NULL DEFAULT 'primary',
    target      TEXT NOT NULL DEFAULT '',
    created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX idx_domains_site_id ON domains(site_id);
CREATE INDEX idx_domains_owner_id ON domains(owner_id);
```

#### `php_versions`

```sql
CREATE TABLE php_versions (
    id              INTEGER PRIMARY KEY,
    version         TEXT NOT NULL,
    image           TEXT NOT NULL,
    enabled         INTEGER NOT NULL DEFAULT 1,
    default_version INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
```

#### `databases`

```sql
CREATE TABLE databases (
    id          INTEGER PRIMARY KEY,
    db_name     TEXT NOT NULL,
    db_user     TEXT NOT NULL DEFAULT '',
    db_password TEXT NOT NULL DEFAULT '',
    engine      TEXT NOT NULL DEFAULT 'mariadb',
    version     TEXT NOT NULL DEFAULT 'lts',
    owner_id    INTEGER NOT NULL DEFAULT 0 REFERENCES users(id) ON DELETE RESTRICT,
    site_id     INTEGER NOT NULL DEFAULT 0 REFERENCES sites(id) ON DELETE RESTRICT,
    enabled     INTEGER NOT NULL DEFAULT 1,
    created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX idx_databases_site_id ON databases(site_id);
```

**Secrets note:** `db_password` contains plain-text database passwords.
This is existing behavior. Future encryption is out of scope for
ARCH-008 but should be tracked as technical debt.

#### `backups`

```sql
CREATE TABLE backups (
    id          INTEGER PRIMARY KEY,
    site_id     INTEGER NOT NULL DEFAULT 0 REFERENCES sites(id) ON DELETE RESTRICT,
    owner_id    INTEGER NOT NULL DEFAULT 0 REFERENCES users(id) ON DELETE RESTRICT,
    filename    TEXT NOT NULL,
    type        TEXT NOT NULL DEFAULT 'manual',
    size        INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL,
    status      TEXT NOT NULL DEFAULT 'completed',
    file_path   TEXT NOT NULL DEFAULT '',
    compression TEXT NOT NULL DEFAULT 'gzip',
    updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX idx_backups_site_id ON backups(site_id);
```

**Note:** `created_at` has no DEFAULT because it is set explicitly
from the application with the exact timestamp of backup creation.

#### `ssl_certificates`

```sql
CREATE TABLE ssl_certificates (
    id                INTEGER PRIMARY KEY,
    domain_id         INTEGER NOT NULL DEFAULT 0 REFERENCES domains(id) ON DELETE RESTRICT,
    domain            TEXT NOT NULL,
    provider          TEXT NOT NULL DEFAULT 'placeholder',
    certificate_path  TEXT NOT NULL DEFAULT '',
    key_path          TEXT NOT NULL DEFAULT '',
    chain_path        TEXT NOT NULL DEFAULT '',
    issued_at         TEXT NOT NULL DEFAULT '',
    expires_at        TEXT NOT NULL DEFAULT '',
    renew_after       TEXT NOT NULL DEFAULT '',
    status            TEXT NOT NULL DEFAULT 'http_only',
    auto_renew        INTEGER NOT NULL DEFAULT 1,
    https_enabled     INTEGER NOT NULL DEFAULT 0,
    redirect_enabled  INTEGER NOT NULL DEFAULT 0,
    domains           TEXT NOT NULL DEFAULT '',
    challenge_type    TEXT NOT NULL DEFAULT '',
    last_error        TEXT NOT NULL DEFAULT '',
    last_validation   TEXT NOT NULL DEFAULT '',
    renew_attempts    INTEGER NOT NULL DEFAULT 0,
    version           INTEGER NOT NULL DEFAULT 1,
    created_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX idx_ssl_domain_id ON ssl_certificates(domain_id);
```

**Cryptographic boundary:** This table stores metadata only (paths,
status, expiry). The actual PEM content remains on the filesystem
at `/srv/containercp/ssl/<site_id>/`.

#### `mail_domains`

```sql
CREATE TABLE mail_domains (
    id                  INTEGER PRIMARY KEY,
    domain_id           INTEGER NOT NULL DEFAULT 0,  -- 0 = external domain
    site_id             INTEGER NOT NULL DEFAULT 0,  -- 0 = unlinked
    domain_name         TEXT NOT NULL,
    mode                TEXT NOT NULL DEFAULT 'disabled',
    relay_host          TEXT NOT NULL DEFAULT '',
    dkim_selector       TEXT NOT NULL DEFAULT 'dkim',
    dkim_private_key_path TEXT NOT NULL DEFAULT '',
    dkim_public_key_dns TEXT NOT NULL DEFAULT '',
    max_mailboxes       INTEGER NOT NULL DEFAULT 0,
    max_aliases         INTEGER NOT NULL DEFAULT 0,
    catch_all           TEXT NOT NULL DEFAULT '',
    enabled             INTEGER NOT NULL DEFAULT 1,
    created_at          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX idx_mail_domains_domain_id ON mail_domains(domain_id);
CREATE INDEX idx_mail_domains_site_id ON mail_domains(site_id);
```

**FK note:** `domain_id` can be 0 (external domain not in ContainerCP's
domain table) — no FK constraint. `site_id` can be 0 — no FK constraint.
This matches existing behavior where mail domains can reference
externally-managed domains.

**Cryptographic boundary:** `dkim_private_key_path` stores a filesystem
path. The actual private key PEM file remains on the filesystem.
`dkim_public_key_dns` stores the DNS TXT record value (public
information) in SQLite.

#### `mail_mailboxes`

```sql
CREATE TABLE mail_mailboxes (
    id              INTEGER PRIMARY KEY,
    domain_id       INTEGER NOT NULL REFERENCES mail_domains(id) ON DELETE RESTRICT,
    local_part      TEXT NOT NULL,
    password_hash   TEXT NOT NULL DEFAULT '',
    quota_bytes     INTEGER NOT NULL DEFAULT 0,
    quota_messages  INTEGER NOT NULL DEFAULT 0,
    enabled         INTEGER NOT NULL DEFAULT 1,
    display_name    TEXT NOT NULL DEFAULT '',
    forward_to      TEXT NOT NULL DEFAULT '',
    spam_enabled    INTEGER NOT NULL DEFAULT 1,
    last_login      TEXT NOT NULL DEFAULT '',
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX idx_mailboxes_domain_id ON mail_mailboxes(domain_id);
```

**Secrets note:** `password_hash` stores Dovecot-compatible SHA-512-CRYPT
password hashes. These are authentication credentials (not private keys),
so they remain in SQLite.

#### `mail_aliases`

```sql
CREATE TABLE mail_aliases (
    id                INTEGER PRIMARY KEY,
    domain_id         INTEGER NOT NULL REFERENCES mail_domains(id) ON DELETE RESTRICT,
    source_local_part TEXT NOT NULL,
    destination       TEXT NOT NULL,
    enabled           INTEGER NOT NULL DEFAULT 1,
    created_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX idx_aliases_domain_id ON mail_aliases(domain_id);
```

#### `access_users`

```sql
CREATE TABLE access_users (
    id            INTEGER PRIMARY KEY,
    username      TEXT NOT NULL,
    auth_type     TEXT NOT NULL DEFAULT 'password',
    password_hash TEXT NOT NULL DEFAULT '',
    enabled       INTEGER NOT NULL DEFAULT 1,
    created_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
```

#### `access_grants`

```sql
CREATE TABLE access_grants (
    id              INTEGER PRIMARY KEY,
    access_user_id  INTEGER NOT NULL REFERENCES access_users(id) ON DELETE RESTRICT,
    site_id         INTEGER NOT NULL DEFAULT 0 REFERENCES sites(id) ON DELETE RESTRICT,
    permission      TEXT NOT NULL DEFAULT 'read_write',
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX idx_grants_user_id ON access_grants(access_user_id);
CREATE INDEX idx_grants_site_id ON access_grants(site_id);
```

#### `reverse_proxies`

```sql
CREATE TABLE reverse_proxies (
    id          INTEGER PRIMARY KEY,
    domain      TEXT NOT NULL,
    site_id     INTEGER NOT NULL DEFAULT 0,  -- 0 = admin panel proxy
    provider    TEXT NOT NULL DEFAULT 'nginx',
    config_path TEXT NOT NULL DEFAULT '',
    upstream    TEXT NOT NULL DEFAULT '',
    enabled     INTEGER NOT NULL DEFAULT 1,
    status      TEXT NOT NULL DEFAULT 'active',
    created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX idx_proxies_site_id ON reverse_proxies(site_id);
```

**FK note:** `site_id = 0` represents the admin panel's own proxy
entry. No FK constraint for `site_id = 0`.

#### `profiles`

```sql
CREATE TABLE profiles (
    id              INTEGER PRIMARY KEY,
    profile_name    TEXT NOT NULL,
    type            TEXT NOT NULL DEFAULT 'web_server',
    web_server      TEXT NOT NULL DEFAULT 'apache',
    runtime         TEXT NOT NULL DEFAULT 'docker',
    template_path   TEXT NOT NULL DEFAULT '',
    description     TEXT NOT NULL DEFAULT '',
    enabled         INTEGER NOT NULL DEFAULT 1,
    default_profile INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
```

#### `auth_users`

```sql
CREATE TABLE auth_users (
    id                  INTEGER PRIMARY KEY,
    username            TEXT NOT NULL,
    password_hash       TEXT NOT NULL DEFAULT '',
    must_change_password INTEGER NOT NULL DEFAULT 0,
    enabled             INTEGER NOT NULL DEFAULT 1,
    role                TEXT NOT NULL DEFAULT 'admin',
    created_at          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
```

### 7.3 Mail module state and smarthost (singletons)

These currently use single-line files. In SQLite they become a
module-level configuration table:

```sql
CREATE TABLE mail_config (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- Key names: 'module_state', 'smarthost'
-- module_state value: 'active' or 'inactive'
-- smarthost value: JSON object with host/port/username/password/enabled
```

### 7.4 Jobs table (schema foundation, future use)

Jobs are currently in-memory only. The table schema is defined here
so ARCH-010 can add persistence without a schema migration:

```sql
CREATE TABLE jobs (
    id            INTEGER PRIMARY KEY,
    type          TEXT NOT NULL,
    status        TEXT NOT NULL DEFAULT 'pending',
    progress      INTEGER NOT NULL DEFAULT 0,
    steps         TEXT NOT NULL DEFAULT '[]',   -- JSON array of step names
    current_step  INTEGER NOT NULL DEFAULT 0,
    message       TEXT NOT NULL DEFAULT '',
    created_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    completed_at  TEXT
);
CREATE INDEX idx_jobs_status ON jobs(status);
```

The `JobManager` remains in-memory for v0.7.0. When ARCH-010
implements job persistence, it writes to this table.

---

## 8. Foreign Key Matrix

### 8.1 FK policy decisions

| Referencing Table | Column | Referenced Table | Policy | Justification |
|-------------------|--------|-----------------|--------|---------------|
| sites | node_id | nodes(id) | `ON DELETE RESTRICT` | A site must belong to a valid node. Cannot delete a node that has sites. Business logic (SiteManager.remove) handles site lifecycle first. |
| domains | owner_id | users(id) | `ON DELETE RESTRICT` | Cannot delete a user that owns domains. Business logic handles reassignment first. |
| domains | site_id | sites(id) | `ON DELETE RESTRICT` | Cannot delete a site that has domains. Business logic (SiteRemoveOperation) removes domains first. |
| databases | owner_id | users(id) | `ON DELETE RESTRICT` | Same as domains. |
| databases | site_id | sites(id) | `ON DELETE RESTRICT` | Same as domains. |
| backups | site_id | sites(id) | `ON DELETE RESTRICT` | Backup records may outlive sites in edge cases. RESTRICT forces cleanup order. |
| backups | owner_id | users(id) | `ON DELETE RESTRICT` | Same as above. |
| ssl_certificates | domain_id | domains(id) | `ON DELETE RESTRICT` | SSL records must be cleaned up before domain removal. |
| mail_mailboxes | domain_id | mail_domains(id) | `ON DELETE RESTRICT` | Cannot delete a mail domain that has mailboxes. |
| mail_aliases | domain_id | mail_domains(id) | `ON DELETE RESTRICT` | Cannot delete a mail domain that has aliases. |
| access_grants | access_user_id | access_users(id) | `ON DELETE RESTRICT` | Cannot delete a user with active grants. |
| access_grants | site_id | sites(id) | `ON DELETE RESTRICT` | Cannot delete a site with active grants. |

### 8.2 Special cases without FK

| Table | Column | No FK | Reason |
|-------|--------|-------|--------|
| mail_domains | domain_id | No FK | Can be 0 (external domain). Even non-zero values may reference domains deleted through a different lifecycle path. |
| mail_domains | site_id | No FK | Can be 0 (unlinked mail config). |
| reverse_proxies | site_id | No FK | Can be 0 (admin panel proxy). |

For these cases, application-level validation in the API handler or
manager enforces integrity. The `ON DELETE RESTRICT` policy would
reject deletion of a site that has `access_grants` pointing to it,
which is correct — the business logic must clean up grants before
removing the site.

### 8.3 `ON DELETE CASCADE` exceptions

No `CASCADE` deletions are used by default. Each case must be
explicitly justified:

- **None proposed.** All resource lifecycles are managed by business
  logic in managers and operations. Foreign keys are an integrity
  boundary, not a lifecycle mechanism.

---

## 9. Index Strategy

### 9.1 Primary key indices

Every table has an implicit primary key index on `id`. SQLite
auto-creates this for `INTEGER PRIMARY KEY` columns.

### 9.2 Foreign key indices

- `sites(node_id)` — look up sites by node
- `domains(site_id)` — look up domains by site
- `domains(owner_id)` — look up domains by owner
- `databases(site_id)` — look up databases by site
- `backups(site_id)` — look up backups by site
- `ssl_certificates(domain_id)` — look up certs by domain
- `mail_domains(domain_id)` — look up mail domains by ContainerCP domain
- `mail_domains(site_id)` — look up mail domains by site
- `mail_mailboxes(domain_id)` — look up mailboxes by mail domain
- `mail_aliases(domain_id)` — look up aliases by mail domain
- `access_grants(access_user_id)` — look up grants by user
- `access_grants(site_id)` — look up grants by site
- `reverse_proxies(site_id)` — look up proxies by site
- `jobs(status)` — filter jobs by status (future)

### 9.3 Future indices (deferred)

- Name-based lookups (e.g., `sites(domain)`, `domains(fqdn)`,
  `profiles(profile_name)`) are not indexed initially because the
  manager-level `find()` methods scan an in-memory vector today.
  If performance requires it, add indices after migration.

---

## 10. SQLite PRAGMA Configuration

### 10.1 Required PRAGMAs

Every SQLite connection is initialized with:

```sql
PRAGMA journal_mode = WAL;        -- Write-Ahead Logging for concurrent reads
PRAGMA foreign_keys = ON;          -- Enforce FK constraints
PRAGMA synchronous = FULL;         -- Safe crash recovery (full fsync)
PRAGMA busy_timeout = 5000;        -- Wait 5s before SQLITE_BUSY
PRAGMA journal_size_limit = 65536; -- Limit WAL file growth
PRAGMA cache_size = -8000;         -- 8MB page cache
```

### 10.2 Justification

- **WAL mode:** Allows concurrent reads while a write is in progress.
  Critical for the REST API (reads) during background job writes.
- **FOREIGN KEYS:** Must be enabled per-connection (SQLite default is OFF).
- **SYNCHRONOUS = FULL:** Maximum durability. Every transaction waits
  for the storage layer to flush. Acceptable for control-plane metadata
  (not high-throughput).
- **BUSY_TIMEOUT = 5000:** If a write is blocked by another writer,
  wait 5 seconds before failing. Protects against transient contention.
- **WAL SIZE LIMIT:** Prevents unbounded WAL growth on busy systems.

### 10.3 Optional PRAGMAs (under review)

```sql
PRAGMA temp_store = MEMORY;        -- Temp tables in memory (faster)
PRAGMA mmap_size = 268435456;      -- 256MB memory-mapped I/O (faster reads)
```

These are performance optimizations and can be added after
benchmarking.

---

## 11. Connection and Threading Model

### 11.1 Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     Storage subsystem                     │
│                                                           │
│  ┌──────────────────────────────────────────────────┐    │
│  │          Write Connection (serialized)            │    │
│  │  protected by std::mutex, single SQLite handle    │    │
│  │  All save_*(), transaction begin/commit/rollback  │    │
│  └──────────────────────────────────────────────────┘    │
│                                                           │
│  ┌──────────────────────────────────────────────────┐    │
│  │          Read Connection Pool (3 handles)         │    │
│  │  std::array<std::unique_ptr<sqlite3>, 3>          │    │
│  │  Round-robin or try-lock allocation               │    │
│  │  All load_*(), find_*(), count_*(), list_*()      │    │
│  └──────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

### 11.2 Write connection

- **One connection, one mutex.** All mutating operations go through
  a single SQLite handle protected by `std::mutex`.
- **Transaction ownership:** The write connection owns all
  `BEGIN`, `COMMIT`, and `ROLLBACK` operations.
- **No concurrent writes.** SQLite serializes writes in WAL mode
  anyway. A single writer avoids unnecessary lock contention.
- **Lifetime:** Opened during storage initialization. Closed during
  daemon shutdown.

### 11.3 Read connection pool

- **Initial pool size: 3.** Supports concurrent API requests (REST API
  thread + Web UI thread + background job executor).
- **Allocation:** Simple round-robin with `std::atomic<int>` counter.
  If all connections are busy, wait on a condition variable (with
  configurable timeout).
- **Lifetime:** Connections are created on first use, cached, and
  closed on daemon shutdown.

### 11.4 Connection initialization

Every connection executes the PRAGMAs from section 10 on open.

### 11.5 Thread safety

- SQLite in WAL mode is thread-safe for reads from multiple threads
  while a write is in progress.
- The write mutex ensures that only one thread can write at a time.
- Read connections do not block each other.
- A read may block on write (if WAL checkpoint is needed) — this is
  handled by `busy_timeout`.

### 11.6 What managers and API handlers must NOT do

- No direct SQLite C API calls (`sqlite3_*`).
- No raw SQL strings outside the Storage implementation.
- No `BEGIN`/`COMMIT` outside Storage transaction methods.
- No connecting to `containercp.db` directly.

The entire codebase accesses persisted state exclusively through the
`Storage` class.

---

## 12. Transaction Model

### 12.1 Types of transactions

#### Single-resource CRUD

The Storage backend wraps every `save_*()` call in an implicit
transaction (single INSERT/UPDATE/DELETE). This is invisible to
callers — identical to current behavior but now atomic.

#### Multi-resource business transactions

Several managers currently perform multiple independent Storage writes
without atomicity:

- **SiteCreateOperation:** Creates site + domain + database records
  in sequence. If the process crashes after inserting the site but
  before inserting the database, the system is in an inconsistent
  state.

- **SiteRemoveOperation:** Removes site + cleans up backups + proxy
  configs. Multiple writes without rollback on failure.

- **Mail domain creation with DKIM:** Creates MailDomain + generates
  DKIM keys (filesystem). No atomicity between storage and filesystem.

**ARCH-008 does not solve these multi-resource transactions.** The
Storage API is not redesigned to expose a general-purpose transaction
interface. Instead, ARCH-008 adds a minimal transaction API:

```cpp
class Storage {
    // ... existing save/load methods ...

    // New in ARCH-008:
    bool begin_transaction();    // BEGIN IMMEDIATE on write connection
    bool commit_transaction();   // COMMIT
    bool rollback_transaction(); // ROLLBACK
};
```

This allows managers to group multiple writes:

```cpp
storage.begin_transaction();
storage.save_sites(sites);
storage.save_domains(domains);
storage.commit_transaction();
```

The transaction API is optional — single-resource saves remain
auto-committed. Managers are migrated to use transactions incrementally
in later epics.

#### Migration transactions

The migration from TXT to SQLite runs in a single transaction per
resource type (section 14).

#### Schema migration transactions

Each schema migration (section 13) runs in its own transaction.

### 12.2 Transaction semantics

- **Write connection only.** Transactions are always on the serialized
  write connection.
- **Implicit rollback.** If a process crash occurs during a transaction,
  SQLite's WAL recovery rolls back uncommitted changes on next open.
- **No nested transactions.** SQLite does not support them. Attempting
  `begin_transaction()` while one is active is a no-op (idempotent).

---

## 13. Migration Engine Architecture

### 13.1 MigrationEngine class

```
libs/storage/MigrationEngine.h
libs/storage/MigrationEngine.cpp
```

A reusable, versioned migration framework.

#### Migration definition

```cpp
struct Migration {
    int version;
    std::string name;
    std::function<bool(sqlite3*)> up;   // Apply migration
    std::function<bool(sqlite3*)> down; // Rollback (optional)
};
```

#### Engine API

```cpp
class MigrationEngine {
public:
    MigrationEngine(sqlite3* db);

    // Register a migration. Order is determined by version number.
    void register_migration(Migration m);

    // Detect current schema version from schema_migrations table.
    int current_version();

    // Apply all pending migrations in version order.
    // Returns false if any migration fails.
    bool migrate();

    // Rollback the last N completed migrations (recovery only).
    bool rollback(int count);

private:
    bool ensure_journal_table();
    bool apply_one(const Migration& m);
    bool verify_checksum(const Migration& m, const std::string& stored);
    std::string compute_checksum(const std::string& sql);
};
```

#### Migration journal table

```sql
CREATE TABLE IF NOT EXISTS schema_migrations (
    version     INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    checksum    TEXT NOT NULL,       -- SHA-256 of migration definition
    started_at  TEXT NOT NULL,
    completed_at TEXT,
    status      TEXT NOT NULL DEFAULT 'pending',
    diagnostics TEXT
);
```

#### Migration states

| State | Meaning |
|-------|---------|
| `pending` | Version > current version, not yet applied |
| `running` | Migration started but not completed (crash during migration) |
| `completed` | Migration finished successfully |
| `failed` | Migration failed (requires manual intervention) |

### 13.2 Migration execution semantics

1. Lock `schema_migrations` table.
2. Read current max version.
3. For each pending migration in version order:
   a. Insert row: `(version, name, checksum, now(), NULL, 'running', NULL)`.
   b. `BEGIN TRANSACTION`.
   c. Execute migration SQL/logic.
   d. `COMMIT`.
   e. Update row: `completed_at = now(), status = 'completed'`.
4. If any migration fails:
   a. `ROLLBACK` the current transaction.
   b. Update row: `status = 'failed', diagnostics = <error>`.
   c. Return false (fail-fast).

### 13.3 Checksum verification

Each migration has a SHA-256 checksum computed from the migration's
name and SQL content. When a migration is registered:

1. If no row exists for this version → migration is new, apply it.
2. If a row exists with `status = 'completed'`:
   - Compare stored checksum with computed checksum.
   - If match → skip (already applied, unchanged).
   - If mismatch → refuse to start (checksum mismatch error).
3. If a row exists with `status = 'failed'`:
   - Refuse to start (manual recovery required).
4. If a row exists with `status = 'running'`:
   - This means the daemon crashed during migration.
   - Check if the migration's schema changes are partially applied.
   - If safe to retry: reset to `pending` and re-apply.
   - If unsafe: refuse to start (manual recovery).

### 13.4 Migration retry behavior

- **Checksum mismatch:** Fail-fast. Do not retry. The operator must
  resolve why the migration definition changed after it was applied.
- **Interrupted (status=running):** On restart, detect partially
  applied migration. If the migration is idempotent (uses
  `CREATE TABLE IF NOT EXISTS`), retry. Otherwise, fail-fast.
- **Failed migration:** Fail-fast. Do not retry automatically. The
  operator must diagnose and fix, then either complete the migration
  manually or restore from backup.

---

## 14. Legacy TXT Import Process

### 14.1 Import steps

For each resource type (in the same order as current startup loading):

1. **Detect source file.** Check if `<type>.db` exists in the
   database directory. If not, skip (empty table).

2. **Parse records.** Use the existing `Storage::load_*()` methods
   (legacy TXT parser is preserved for migration and testing).
   Validate every record:
   - Correct field count.
   - Valid integer/boolean conversions.
   - Non-empty primary key (id).
   - No duplicate IDs within the file.

3. **Validate relationships.** For records referencing other resources
   by ID, verify the referenced resource exists in the already-migrated
   table. For `site_id = 0` sentinel values, allow without check.

4. **Canonicalize.** Convert each record to a canonical JSON
   representation (sorted keys, stable formatting).

5. **Compute TXT checksum.** SHA-256 of sorted canonical records.

6. **INSERT into SQLite.** One transaction per resource type. Use
   `INSERT OR FAIL` to reject duplicates.

7. **Read back from SQLite.** Load all records from the SQLite table.

8. **Canonicalize again.** Same algorithm as step 4.

9. **Compute SQLite checksum.** SHA-256 of sorted canonical records.

10. **Compare checksums.** Must match exactly. If mismatch, roll back
    the transaction and fail.

11. **Verify integrity.**
    - `PRAGMA foreign_key_check` — no orphaned references.
    - `PRAGMA integrity_check` — database structural integrity.

### 14.2 Import transaction isolation

Each resource type is imported in its own transaction. This allows
partial migration: if `mail_mailboxes` fails, the already-migrated
`sites`, `domains`, etc. remain committed. The failed type can be
retried after the source file is fixed.

### 14.3 Import ordering

1. `nodes` (no dependencies)
2. `users` (no dependencies)
3. `sites` (depends on nodes)
4. `domains` (depends on sites, users)
5. `php_versions` (no dependencies)
6. `profiles` (no dependencies)
7. `backups` (depends on sites, users)
8. `databases` (depends on sites, users)
9. `access_users` (no dependencies)
10. `auth_users` (no dependencies)
11. `reverse_proxies` (depends on sites — may have site_id=0)
12. `ssl_certificates` (depends on domains — may have domain_id=0)
13. `mail_domains` (depends on sites — may have site_id=0, domain_id=0)
14. `mail_mailboxes` (depends on mail_domains)
15. `mail_aliases` (depends on mail_domains)
16. `mail_config` (singleton state + smarthost, no dependencies)

### 14.4 Duplicate ID handling

- Within a single TXT file: duplicates are rejected at parse time.
- Cross-file: IDs are unique per type (each type has its own table
  with `INTEGER PRIMARY KEY`). No cross-type ID conflict possible.

### 14.5 Corrupted record handling

If a TXT record fails to parse (wrong field count, invalid integer):

1. Log the exact file and line number.
2. Log the raw line content.
3. Include the validation error.
4. Roll back the current type's transaction.
5. Continue to the next type (fail one type, not all).

The startup fails overall (fail-fast), but the operator can see
exactly which type, file, and record failed.

### 14.6 Idempotency

- Migration runs at most once. After successful import, the
  `schema_migrations` table records the migration.
- On next startup, the migration engine sees all migrations are
  `completed` and skips import.
- Migrated TXT files are backed up and removed from the active
  directory — they cannot be re-imported accidentally.
- If migration fails partway, already-committed types remain in SQLite
  but the overall startup fails. On retry, the migration engine skips
  completed types and retries only the failed ones.

---

## 15. Migration Idempotency

### 15.1 Schema migrations

Each schema migration has a unique version number. Once recorded as
`completed` in `schema_migrations`, it is never re-applied. The
checksum protects against the migration definition changing after
application.

### 15.2 Data import (TXT → SQLite)

The data import is not a schema migration — it is a one-time data
migration. It is gated by the presence of TXT files in the database
directory:

- **No TXT files → skip import.** Assume fresh SQLite installation.
- **TXT files present → run import.**
- **After successful import → archive TXT files.**
- **TXT files reappear after archiving → log warning, ignore.**

### 15.3 Fresh installation detection

- If `containercp.db` does not exist AND no `.db` files exist in the
  database directory → fresh installation. Create `containercp.db`,
  apply all schema migrations (no data import).
- If `containercp.db` does not exist AND `.db` files exist → v0.6.0
  upgrade path. Create `containercp.db`, apply schema migrations,
  import legacy data.
- If `containercp.db` exists AND `.db` files exist → mixed state.
  Log warning, verify equivalence, proceed with SQLite only.

---

## 16. Partial Failure and Recovery

### 16.1 Failure modes

| Failure | Impact | Recovery |
|---------|--------|----------|
| TXT file missing for expected type | Empty SQLite table for that type | WARNING log, continue |
| TXT file corrupted (parse error) | Type fails to import | FAIL log, startup aborted |
| SQLite INSERT fails (constraint) | Type fails to import | FAIL log, startup aborted |
| Checksum mismatch after import | Type fails verification | FAIL log, startup aborted |
| `PRAGMA integrity_check` fails | Database corrupt | FAIL log, startup aborted |
| Schema migration checksum mismatch | Migration refused | FAIL log, startup aborted |
| WAL/shm file corruption on restart | SQLite auto-recovery on open | WARNING log if recovery needed |

### 16.2 Fail-fast behavior

If any migration or import fails:

1. **Do not start managers.** No `ServiceRegistry` initialization
   beyond storage.
2. **Do not start API listeners.** Ports 8080/8081 remain closed.
3. **Do not start job executor.** No background tasks.
4. **Do not start runtime reconcilation.** No container operations.
5. **Do not start SSL renewal or mail sync.**
6. **Log the specific error.** Include: migration ID, resource type,
   source filename, record/line number, exact SQLite error.
7. **Roll back the active transaction.** Previously committed
   transactions remain committed.
8. **Exit the process.** The systemd service will restart (after
   `RestartSec=5`), and the migration will retry.

### 16.3 Operator recovery steps

1. Read the error log to identify the failed type and reason.
2. For parse errors: fix or remove the offending TXT record.
3. For checksum errors: verify source data integrity.
4. Remove or rename `containercp.db` to revert to TXT-only mode.
5. Restart the daemon.

### 16.4 Original TXT files remain unchanged

The migration process reads TXT files but does not modify or delete
them. They are copied to the archive directory only after successful
migration and verification. If migration fails, the TXT files remain
in their original location, ready for retry or manual recovery.

---

## 17. Startup Migration Gate

### 17.1 Required startup order

```
1. Acquire daemon single-instance lock (PID file)
2. Load minimal configuration (paths, data_root)
3. Detect storage state:
   a. Fresh install (no .db, no containercp.db)
   b. v0.6.0 upgrade (.db files exist, no containercp.db)
   c. Already migrated (containercp.db exists, no .db files)
   d. Mixed (.db + containercp.db both exist)
4. If mixed state:
   a. Log warning
   b. Verify equivalence (checksum comparison)
   c. If mismatch → fail-fast
   d. If match → log "verified", proceed
5. If fresh install:
   a. Create containercp.db
   b. Apply all schema migrations
   c. Seed initial data (admin user, local node, default PHP versions, profiles)
6. If upgrade (v0.6.0):
   a. Create containercp.db
   b. Apply all schema migrations
   c. Create legacy TXT backup archive
   d. Import each resource type (section 14)
   e. Verify full equivalence
   f. Archive legacy TXT files
7. If already migrated:
   a. Open containercp.db
   b. Verify schema version matches expected
   c. Apply any pending schema migrations (future updates)
8. Close migration connections
9. Reopen database through production Storage subsystem
10. Perform production reopen validation:
    a. Load all resource types via Storage API
    b. `PRAGMA foreign_key_check`
    c. `PRAGMA integrity_check`
11. Initialize managers and services (current ServiceRegistry startup)
12. Start jobs, runtime reconciliation, SSL renewal, mail sync
13. Start REST API and Web UI listeners
```

### 17.2 Gate condition

Steps 2–10 are the **migration gate**. Until `production reopen
validation` completes:

- No manager is initialized.
- No API port is open.
- No background thread is started.
- No runtime state is mutated.

### 17.3 Database metadata

The `containercp.db` file itself stores:

```sql
CREATE TABLE storage_meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- Expected keys:
-- 'storage_backend'       → 'sqlite'
-- 'schema_version'        → current migration version (integer)
-- 'migration_state'       → 'completed' | 'failed'
-- 'source_version'        → '0.6.0' (version migrated from, empty for fresh)
-- 'migration_completed_at' → ISO-8601 timestamp
-- 'migration_id'          → UUID of this migration run
```

The existence of `containercp.db` alone does not mean migration
succeeded. The `migration_state` key must be `'completed'`.

---

## 18. Migration Verification Strategy

### 18.1 Verification steps (every resource type)

1. **Parse success.** Every legacy TXT record parses without error.
2. **Field count.** Every record has the correct number of fields.
3. **Type validation.** Integer fields parse as integers. Boolean
   fields are `"0"` or `"1"`. Timestamps are valid ISO-8601.
4. **Validation rules.** Every resource passes existing manager-level
   validation (name length, domain format, etc.).
5. **Duplicate rejection.** Duplicate primary keys within a file
   are rejected.
6. **Record count match.** TXT file has N records, SQLite table has
   N rows.
7. **Read-back.** Every migrated record is read from SQLite through
   the Storage API.
8. **Canonical representation.** Both TXT and SQLite records are
   converted to a stable canonical JSON form:
   - Keys sorted alphabetically.
   - No whitespace.
   - Booleans as `true`/`false`.
   - Null/missing fields omitted.
9. **Field-by-field comparison.** TXT canonical vs SQLite canonical
   for every record.
10. **SHA-256 checksum.** Checksum is computed from canonical records
    sorted by primary key. TXT checksum must match SQLite checksum.
11. **`PRAGMA foreign_key_check`.** No orphaned foreign key references.
12. **`PRAGMA integrity_check`.** Database structural integrity.
13. **Close all connections.** Ensure WAL checkpoint.
14. **Reopen through production Storage.** Use the same code path as
    normal daemon startup.
15. **Load all resource types.** Call `load_*()` for every type.
16. **Repeat integrity checks.** Steps 11–12 after reopen.

### 18.2 Verification on mismatch

Any mismatch at any step causes fail-fast shutdown. The error log
identifies the exact type, record, and field that differs.

### 18.3 Silent normalization

Do not silently repair, normalize, discard, or skip unexpected source
data **unless** the exact normalization already exists in the current
production parser and is documented.

Currently documented normalizations:
- Site `web_server` defaults to `"apache"` when empty.
- Mail domain legacy field shift (`owner_id` → `domain_id`).
- SSL certificate format detection (old 4-field vs new 14+ field).

These are preserved in the migration parser.

---

## 19. Legacy Backup and Rollback Strategy

### 19.1 Backup directory

Before migration, create:

```
/srv/containercp/database/migrations/v0.6.0-to-v0.7.0-<UTC_TIMESTAMP>/
```

### 19.2 Backup contents

- Every source `.db` file (all 19 files from section 2.2).
- Manifest file `manifest.json`:

```json
{
    "migration_id": "uuid",
    "source_version": "0.6.0",
    "target_version": "0.7.0",
    "migration_timestamp": "2026-07-16T12:00:00Z",
    "files": [
        {
            "filename": "sites.db",
            "size": 1234,
            "sha256": "abc...",
            "record_count": 5
        },
        ...
    ],
    "result": "success",
    "checksum_match": true,
    "integrity_check": "ok",
    "foreign_key_check": "ok"
}
```

### 19.3 Post-migration cleanup

After successful migration:
1. Copy all `.db` files to the archive directory.
2. Create the manifest.
3. Remove `.db` files from the active database directory.
4. Set archive directory permissions to `0700` (root-only).
5. The archive becomes read-only (chmod -R a-w).

After failed migration:
1. `.db` files remain in the active directory.
2. No archive is created.
3. Daemon exits with error.

### 19.4 Rollback procedure

To roll back to v0.6.0 TXT storage:

1. Stop the daemon.
2. Remove (or rename) `containercp.db`.
3. Copy `.db` files from the archive back to the active directory.
4. Start the daemon.

The daemon will detect missing `containercp.db` and `.db` files
present → fresh v0.6.0 startup.

### 19.5 Automatic rollback is forbidden

The system does NOT automatically fall back to TXT storage on
migration failure. The operator must make a conscious decision to
roll back.

---

## 20. Cryptographic and Filesystem Artifact Boundary

### 20.1 Principle

SQLite is the system of record for control-plane metadata, configuration,
relationships, and state.

The filesystem remains the system of record for cryptographic material,
large artifacts, generated runtime files, site content, backups,
templates, logs, and container data.

### 20.2 What moves to SQLite

| Data | Current location | SQLite column(s) |
|------|-----------------|------------------|
| SSL cert metadata (status, expiry, provider, issuer, subject, fingerprint, serial) | `ssl_certificates.db` + `metadata.json` on filesystem | `ssl_certificates` table |
| SSL cert file paths | `ssl_certificates.db` | `certificate_path`, `key_path`, `chain_path` |
| DKIM public key DNS record | `mail_domains.db` | `dkim_public_key_dns` |
| DKIM private key path | `mail_domains.db` | `dkim_private_key_path` |
| Mailbox password hashes | `mail_mailboxes.db` | `password_hash` |
| Auth user password hashes | `auth_users.db` | `password_hash` |
| Database passwords | `databases.db` | `db_password` |
| Smarthost credentials (password) | `mail_smarthost.db` | `mail_config` table |

### 20.3 What stays on the filesystem

| Artifact | Filesystem path | Notes |
|----------|----------------|-------|
| SSL fullchain PEM | `/srv/containercp/ssl/<site_id>/fullchain.pem` | Nginx, Postfix, Dovecot read from filesystem |
| SSL private key PEM | `/srv/containercp/ssl/<site_id>/privkey.pem` | Same |
| SSL chain PEM | `/srv/containercp/ssl/<site_id>/chain.pem` | Same |
| SSL metadata JSON | `/srv/containercp/ssl/<site_id>/metadata.json` | Duplicate of SQLite data for filesystem tools |
| ACME account key | `/srv/containercp/ssl/<site_id>/account_key.pem` | Let's Encrypt |
| DKIM private key PEM | `<dkim_dir>/<domain>/<selector>.private` | OpenDKIM/Rspamd |
| SSH host keys | `/etc/ssh/` | System SSH |
| SSH user private keys | User-provided (not generated by ContainerCP) | SFTP access |
| Site files | `/srv/containercp/sites/<domain>/` | User content |
| Backups | `/srv/containercp/backups/` | Tar archives |
| Templates | `/etc/containercp/templates/web/` | Disk-based config |
| Logs | `/var/log/containercp/` | Journald |

### 20.4 Metadata-to-file consistency checks

On daemon startup (and on demand via API), the system verifies:

1. For each `ssl_certificates` row with `status = 'active'`:
   - `certificate_path` file exists and is non-empty.
   - `key_path` file exists and is non-empty.
   - Certificate at `certificate_path` has not expired.
2. For each `mail_domains` row with non-empty `dkim_private_key_path`:
   - File exists and is non-empty.
3. For each `backups` row with non-empty `file_path`:
   - File exists. (Missing backup = warning, not error.)

### 20.5 Missing file behavior

- SSL cert file missing: status set to `"error"`, `last_error` updated.
  HTTPS for the affected domain is disabled.
- DKIM key file missing: regeneration triggered. If regeneration fails,
  log warning.
- Backup file missing: record retained (allows manual recovery from
  off-site copy).

### 20.6 Backup preserves both SQLite and filesystem

The database backup (section 21) captures SQLite metadata. Any
restore procedure must also restore the associated filesystem
artifacts. The backup documentation will list required filesystem
paths.

---

## 21. Database Backup and Restore Strategy

### 21.1 SQLite-aware backup

`cp containercp.db` while the daemon is running is NOT a valid backup
because:
- WAL and SHM files contain uncheckpointed changes.
- The copy may be inconsistent if a write is in progress.

### 21.2 Recommended backup mechanism: SQLite Online Backup API

The Storage subsystem exposes:

```cpp
class Storage {
    // ...
    bool backup(const std::string& dest_path);
};
```

Implementation uses `sqlite3_backup_init()` / `sqlite3_backup_step()` /
`sqlite3_backup_finish()` to create a consistent snapshot while the
database is in use.

### 21.3 WAL and SHM file handling

- **Normal operation:** WAL (`containercp.db-wal`) and SHM
  (`containercp.db-shm`) exist alongside the main database.
- **Before backup:** Execute `PRAGMA wal_checkpoint(TRUNCATE)` to
  flush WAL into the main database and truncate the WAL file.
- **After backup:** WAL grows again normally from the checkpoint.
- **Daemon shutdown:** Execute `PRAGMA wal_checkpoint(TRUNCATE)` to
  ensure clean state.
- **Backup with Online Backup API:** The API handles WAL consistency
  automatically — no manual checkpoint needed.
- **Rollback:** If rolling back from a backup copy, the WAL/SHM files
  from the backup copy are used if present. SQLite auto-recovers.

### 21.4 Backup command

```
containercp storage backup <path>
```

Creates a consistent snapshot at the given path.

### 21.5 Restore command

```
containercp storage restore <path>
```

Stops the daemon, replaces `containercp.db`, removes WAL/SHM files,
restarts the daemon.

### 21.6 Full-site restore consideration

Restoring only `containercp.db` is insufficient if site files,
SSL certificates, or other filesystem artifacts are also needed.
Full disaster recovery must restore:
1. `containercp.db` (metadata + configuration)
2. `/srv/containercp/ssl/` (SSL certificates and keys)
3. `/srv/containercp/sites/` (site files, if needed)
4. `/srv/containercp/proxy/` (generated proxy configs)
5. `/srv/containercp/backups/` (backup archives)

---

## 22. Fresh Installation Behavior

### 22.1 Detection

Fresh installation = no `containercp.db` AND no `.db` files in the
database directory.

### 22.2 Process

1. Create `containercp.db`.
2. Apply all schema migrations (create all tables).
3. Seed initial data:
   - `storage_meta`: backend, version, migration state.
   - `nodes`: local node (id=1, name="local", type="local").
   - `users`: admin user.
   - `php_versions`: PHP 8.4 (default, enabled), PHP 8.3 (enabled),
     PHP 8.2 (enabled).
   - `profiles`: default web server profiles (apache-php-default as
     default, nginx-php-default, etc.).
   - `auth_users`: admin user with auto-generated password.
4. This matches the current `ServiceRegistry` constructor behavior
   that seeds initial data when `load_*()` returns empty vectors.

### 22.3 No data migration

No TXT files exist → no migration is attempted → no archive is created.

---

## 23. Upgrade Behavior from v0.6.0

### 23.1 Detection

Upgrade = `containercp.db` does NOT exist AND `.db` files exist in
the database directory.

### 23.2 Process

1. Create `containercp.db`.
2. Apply all schema migrations.
3. Create legacy backup archive (section 19).
4. Import all resource types (section 14).
5. Verify equivalence (section 18).
6. On success: archive TXT files, update `storage_meta`.
7. On failure: roll back, exit with diagnostic.

### 23.3 What if `containercp.db` already exists?

If the daemon is restarted after a successful migration (v0.6.0 → v0.7.0),
`containercp.db` exists and `.db` files have been archived. The startup
detects "already migrated" mode and proceeds normally.

If the daemon is restarted after a failed migration:
- `containercp.db` exists (partially written).
- `.db` files still exist in the active directory.
- This is "mixed state" (section 24).

### 23.4 install.sh changes

Add `libsqlite3-dev` to the build dependencies:

```bash
apt-get install -y -qq git cmake ninja-build g++ curl libsqlite3-dev
```

No other changes — the migration runs automatically on first daemon
startup after binary upgrade.

### 23.5 update.sh changes

No changes. The update script rebuilds the binary and restarts the
daemon. On restart, the migration gate detects `.db` files and runs
the migration automatically.

---

## 24. Invalid and Mixed Storage State Handling

### 24.1 State detection matrix

| `containercp.db` exists? | `.db` files exist? | `storage_meta` valid? | Interpretation | Action |
|--------------------------|-------------------|----------------------|----------------|--------|
| No | No | N/A | Fresh install | Create DB, apply schema, seed data |
| No | Yes | N/A | v0.6.0 upgrade | Create DB, migrate, import |
| Yes | No | Yes | Already migrated | Normal startup |
| Yes | No | No | Corrupt migration | Fail-fast |
| Yes | Yes | Yes | Mixed, already migrated | Verify equivalence, warn |
| Yes | Yes | No | Mixed, failed migration | Fail-fast |

### 24.2 Mixed state handling

When both `containercp.db` and `.db` files exist:

1. Verify `storage_meta` is valid (`migration_state = 'completed'`).
2. If valid → mixed state after successful migration:
   - Log warning.
   - Verify equivalence by checksum comparison.
   - If match → archive the `.db` files (they were missed by a
     previous cleanup) and proceed.
   - If mismatch → fail-fast (data drift between TXT and SQLite).
3. If `storage_meta` indicates `failed` migration:
   - Fail-fast. Do not attempt to repair.

### 24.3 Rejected states

- **No `containercp.db`, no `.db` files, but flags say setup completed:**
  Treat as fresh install (seed data).
- **`containercp.db` with WAL files but no manifest:**
  Open with SQLite auto-recovery. Log warning.
- **Empty `containercp.db` (0 bytes):**
  Treat as fresh install (overwrite).

---

## 25. API, CLI, Web UI, and Manager Compatibility

### 25.1 Storage API — unchanged

All `Storage` public methods (section 2.1) keep the same signatures.
No changes to any manager, API handler, CLI handler, or Web UI code
are required.

### 25.2 Compatibility shims

- The legacy TXT parser (`load_*` methods) is preserved as `MigrationTXTParser`
  for migration and testing only. It is NOT used at runtime after migration.
- The `template_profiles.db` migration (`migrate_template_profiles()`) is
  handled during the migration phase.

### 25.3 Code that becomes obsolete

| Code | Status |
|------|--------|
| TXT file path helpers (`nodes_file()`, `sites_file()`, etc.) | Migration-only |
| Format detection (pipe counting for sites/ssl/mail) | Migration-only |
| `php_mail_enabled_present` transient field | Removed |
| `migrate_template_profiles()` | Migration-only |
| `mail_state_file()`, `mail_smarthost_file()` | Migration-only |

### 25.4 Two permanent backends?

**No.** After migration completes, only SQLite is active. The legacy
TXT parser exists only in the migration code path and test code.

---

## 26. Installer and Updater Changes

### 26.1 `scripts/install.sh`

Add `libsqlite3-dev`:

```bash
apt-get install -y -qq git cmake ninja-build g++ curl libsqlite3-dev
```

### 26.2 `scripts/update.sh`

No changes. The update script rebuilds the binary (which now links
SQLite3) and restarts the daemon. Migration runs automatically.

### 26.3 `packaging/containercp.service`

No changes. The daemon handles migration internally.

### 26.4 `CMakeLists.txt`

Add:

```cmake
find_package(SQLite3 REQUIRED)

target_link_libraries(containercpd PRIVATE
    cares
    curl
    ssl
    crypto
    crypt
    SQLite::SQLite3
)

target_link_libraries(containercp_tests PRIVATE
    ...
    SQLite::SQLite3
)
```

---

## 27. Configuration Changes

No new configuration values. The existing `database_dir()` path
(`/srv/containercp/database/`) is reused. The single database file
is `containercp.db` within that directory.

---

## 28. Logging and Diagnostics

### 28.1 Migration logging

Every migration step is logged with category `STORAGE`:

```
[STORAGE] Starting migration from v0.6.0 to v0.7.0
[STORAGE] Migration ID: 550e8400-e29b-41d4-a716-446655440000
[STORAGE] Detected 15 resource types with data
[STORAGE] Creating legacy backup at /srv/containercp/database/migrations/v0.6.0-to-v0.7.0-20260716T120000Z/
[STORAGE] Importing nodes: 1 records, checksum match OK
[STORAGE] Importing sites: 5 records, checksum match OK
[STORAGE] Importing domains: 8 records, checksum match OK
...
[STORAGE] PRAGMA foreign_key_check: OK
[STORAGE] PRAGMA integrity_check: OK
[STORAGE] Migration completed successfully
[STORAGE] Legacy TXT files archived at ...
```

### 28.2 Error logging

On failure:

```
[STORAGE] MIGRATION FAILED
[STORAGE] Migration ID: 550e8400-e29b-41d4-a716-446655440000
[STORAGE] Failed at step: Importing mail_domains
[STORAGE] Source file: /srv/containercp/database/mail_domains.db
[STORAGE] Record 3: field count mismatch (expected 12, got 10)
[STORAGE] Raw line: "5|local|example.com|1|1|postmaster@example.com"
[STORAGE] SQLite error: SQLITE_CONSTRAINT
[STORAGE] Recovery: Fix mail_domains.db and restart the daemon.
[STORAGE] Previous types (nodes, sites, users) are committed.
[FATAL] Startup aborted due to storage migration failure.
```

### 28.3 Startup validation logging

```
[STORAGE] Storage backend: SQLite
[STORAGE] Schema version: 1
[STORAGE] Migration state: completed
[STORAGE] Source version: 0.6.0
[STORAGE] Migration completed at: 2026-07-16T12:00:00Z
[STORAGE] Production reopen check: PASSED
```

---

## 29. Security and Filesystem Permissions

### 29.1 Database file permissions

- `containercp.db`: `0600` (owner read/write only).
- WAL file (`containercp.db-wal`): inherits from main file.
- SHM file (`containercp.db-shm`): inherits from main file.
- Archive directory: `0700`.

### 29.2 Database directory permissions

- `/srv/containercp/database/`: `0700` (unchanged from current).

### 29.3 Secrets in SQLite

The database stores hashed passwords (auth users, mailboxes) and
plain-text database passwords. This is existing behavior. The file
permissions (`0600`) protect against unauthorized OS-level access.

### 29.4 Cryptographic keys are NOT in SQLite

Per the product-owner decision (section 20), all PEM private keys
remain on the filesystem. The SQLite database stores only file paths
and public metadata.

---

## 30. Performance Expectations and Benchmarks

### 30.1 Expected characteristics

| Operation | Current (pipe-delimited) | Proposed (SQLite) |
|-----------|--------------------------|-------------------|
| Load all sites (N=5) | O(N) file read + parse | O(N) SELECT (similar) |
| Load all sites (N=1000) | O(N) file read + parse | O(N) SELECT (similar, with indices) |
| Save one site | O(N) rewrite entire file | O(1) UPDATE/INSERT |
| Save 100 sites in batch | 100 × O(N) rewrites | 1 × O(N) transaction |
| Lookup site by ID | O(N) scan entire vector | O(1) index lookup |
| List sites with pagination | Not supported | `LIMIT 10 OFFSET 0` |

### 30.2 Concurrency

| Scenario | Current | Proposed |
|----------|---------|----------|
| Concurrent reads | Not safe | Safe (WAL mode) |
| Read during write | Not safe | Safe (WAL mode) |
| Concurrent writes | Not safe | Serialized (mutex + WAL) |

### 30.3 Benchmark targets

- Migration time: < 1 second for 1000 records total.
- Load all resources at startup: < 100ms.
- Single-resource save: < 10ms.
- Concurrent read throughput: > 100 reads/second.

---

## 31. Testing Strategy

### 31.1 Unit tests

| Test | File |
|------|------|
| SQLite wrapper: open, close, PRAGMAs | `tests/test_storage.cpp` |
| SQLite wrapper: INSERT, SELECT, UPDATE, DELETE | `tests/test_storage.cpp` |
| FK constraint enforcement | `tests/test_storage.cpp` |
| Transaction commit and rollback | `tests/test_storage.cpp` |
| Concurrent read from pool | `tests/test_storage.cpp` |
| Busy timeout during write contention | `tests/test_storage.cpp` |
| WAL checkpoint and recovery | `tests/test_storage.cpp` |

### 31.2 Migration tests

| Test | File |
|------|------|
| Schema migration ordering | `tests/test_migration.cpp` |
| Checksum match → skip | `tests/test_migration.cpp` |
| Checksum mismatch → fail | `tests/test_migration.cpp` |
| Interrupted migration (running status) → retry | `tests/test_migration.cpp` |
| Failed migration → no retry | `tests/test_migration.cpp` |
| All 19 TXT formats import correctly | `tests/test_migration.cpp` |
| Corrupted TXT record → fail with diagnostics | `tests/test_migration.cpp` |
| Duplicate ID in TXT → fail | `tests/test_migration.cpp` |
| Legacy format detection (sites, SSL, mail domains) | `tests/test_migration.cpp` |
| `site_id = 0` sentinel handling | `tests/test_migration.cpp` |
| `domain_id = 0` in MailDomain | `tests/test_migration.cpp` |

### 31.3 Verification tests

| Test | File |
|------|------|
| Canonical representation stable | `tests/test_migration.cpp` |
| TXT vs SQLite checksum match | `tests/test_migration.cpp` |
| TXT vs SQLite checksum mismatch → fail | `tests/test_migration.cpp` |
| `PRAGMA integrity_check` failure → fail | `tests/test_migration.cpp` |
| `PRAGMA foreign_key_check` failure → fail | `tests/test_migration.cpp` |
| Production reopen: load all types | `tests/test_migration.cpp` |
| Production reopen: re-verify integrity | `tests/test_migration.cpp` |

### 31.4 State detection tests

| Test | File |
|------|------|
| Fresh install (no files) → seed data | `tests/test_migration.cpp` |
| v0.6.0 upgrade (.db files) → migrate | `tests/test_migration.cpp` |
| Already migrated (SQLite only) → skip | `tests/test_migration.cpp` |
| Mixed state with match → warn + proceed | `tests/test_migration.cpp` |
| Mixed state with mismatch → fail | `tests/test_migration.cpp` |
| Corrupted `storage_meta` → fail | `tests/test_migration.cpp` |

### 31.5 Idempotency tests

| Test | File |
|------|------|
| Repeat startup after successful migration | `tests/test_migration.cpp` |
| Repeat startup with archived TXT → no re-import | `tests/test_migration.cpp` |
| TXT file reappears after archive → ignore | `tests/test_migration.cpp` |

### 31.6 Legacy archive tests

| Test | File |
|------|------|
| Archive contains all `.db` files | `tests/test_migration.cpp` |
| Manifest contains correct checksums | `tests/test_migration.cpp` |
| Archive directory permissions = 0700 | `tests/test_migration.cpp` |
| Files in archive are read-only | `tests/test_migration.cpp` |

### 31.7 Backup and restore tests

| Test | File |
|------|------|
| Online Backup API creates consistent snapshot | `tests/test_storage.cpp` |
| Snapshot is valid SQLite database | `tests/test_storage.cpp` |
| Restore from snapshot recovers all data | `tests/test_storage.cpp` |

### 31.8 Cryptographic boundary tests

| Test | File |
|------|------|
| SSL cert metadata in SQLite, PEM on filesystem | `tests/test_cert_store.cpp` |
| SSL metadata → file path consistency | `tests/test_cert_store.cpp` |
| Missing SSL cert file → error status | `tests/test_cert_store.cpp` |

### 31.9 Full regression suite

- All 257 existing tests must pass with the new Storage backend.
- The existing `test_storage.cpp` tests are rewritten to test SQLite
  behavior.

### 31.10 Validation VM tests

- Clean Debian 13 installation with SQLite backend.
- v0.6.0 → v0.7.0 upgrade from real `.db` files.
- Fresh installation from zero.
- Mixed state scenarios.
- Migration failure and recovery.

---

## 32. Proposed Implementation Phases

### Phase 1: SQLite wrapper and connection pool (1 week)

- `SQLiteStorage` class: open/close, PRAGMAs, connection pool.
- Write connection with mutex.
- Read connection pool (pool size 3).
- `begin_transaction()`, `commit_transaction()`, `rollback_transaction()`.
- `backup()` method using Online Backup API.
- Unit tests for all wrapper operations.

### Phase 2: Schema migration engine (1 week)

- `MigrationEngine` class: register, detect version, apply, rollback.
- `schema_migrations` table creation.
- `storage_meta` table creation.
- Checksum computation and verification.
- State machine: pending → running → completed → failed.
- Interrupted migration detection and retry.
- Unit tests for all migration states.

### Phase 3: Legacy importer for every resource type (1 week)

- `LegacyImporter` class.
- Import each of the 19 resource types.
- Format detection (pipe counting) for legacy variants.
- Canonical representation and checksum computation.
- Verification pipeline (16 steps from section 18).
- Import ordering (section 14.3).
- Duplicate and corrupted record handling.

### Phase 4: Legacy backup, archive, rollback (0.5 week)

- Archive directory creation.
- Manifest generation (SHA-256, record counts).
- Post-migration cleanup.
- Rollback procedure documentation.
- Verification tests.

### Phase 5: Startup migration gate (0.5 week)

- Modified `main.cpp` startup sequence.
- State detection (fresh / upgrade / migrated / mixed).
- Fail-fast implementation.
- Diagnostic logging.
- `storage_meta` initialization.

### Phase 6: New Storage implementation behind existing API (1 week)

- Replace `Storage.cpp` implementation with SQLite backend.
- All existing `save_*()` / `load_*()` methods backed by SQLite.
- Legacy TXT parser preserved for migration only.
- `CMakeLists.txt` changes (SQLite3 dependency).
- `install.sh` update.

### Phase 7: Production reopen validation (0.5 week)

- Close migration connections.
- Reopen through production Storage.
- Load all types, verify integrity.
- Seamless transition to normal startup.

### Phase 8: Integration tests and validation (1 week)

- Full regression suite.
- v0.6.0 upgrade test with real data.
- Fresh installation test.
- Mixed state tests.
- Validation VM deployment.
- Benchmark measurements.

**Total estimated duration: 6.5 weeks.**

---

## 33. Risks and Rejected Alternatives

### 33.1 Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Migration fails on real user data | Medium | High | Comprehensive test suite + verification checksums + manual recovery path |
| Performance regression in `load_all()` | Low | Medium | Benchmark before/after, add indices as needed |
| SQLite busy during high concurrency | Low | Low | `busy_timeout = 5000`, separate write/read connections |
| WAL file grows unbounded | Low | Low | `journal_size_limit`, periodic checkpoint on shutdown |
| Upgrade from corrupted `.db` file | Low | High | Fail-fast with diagnostic — operator must fix source data |
| Race condition in read pool | Low | Medium | Simple round-robin with atomic counter, protect with mutex if needed |

### 33.2 Rejected alternatives

#### Keep pipe-delimited storage

**Rejected because:** No atomicity, no concurrency, no indices,
no relational integrity, no schema versioning. Every downstream
feature (job persistence, audit log, pagination, backup scheduling)
is blocked.

#### Multiple SQLite databases (sites.db, domains.db, etc.)

**Rejected because:** Cross-resource transactions would be impossible.
Foreign keys across databases are not supported. Multiple migration
chains, multiple backup files, synchronization problems. The product
owner explicitly rejected this (approved decision #1).

#### LMDB or other key-value store

**Rejected because:** Schema changes require application-level
migration code. No SQL query capability for future features (audit
log filtering, metrics queries, pagination). More complex than
SQLite for the relational data model.

#### PostgreSQL or MySQL

**Rejected because:** ContainerCP is a single-server control panel.
Requiring a separate database server adds deployment complexity
that contradicts the product vision of "install on a clean Debian
server in 30 minutes." SQLite is zero-configuration and matches
the single-server architecture.

#### Full ORM (SQLAlchemy-style C++ library)

**Rejected because:** Adds a heavy dependency. The Storage API is
simple (save/load vectors). SQLite wrapper with prepared statements
is sufficient. An ORM would add complexity without proportional
benefit.

#### In-place migration (modify TXT files)

**Rejected because:** Safer to archive originals and build SQLite
from scratch. If migration fails, originals are untouched.

---

## 34. Open Questions Requiring Product-Owner Approval

1. **Mailbox password hash in SQLite.** Approved decision #10 says
   "do not store cryptographic material inside SQLite." Password
   hashes are authentication credentials (not private keys). **Proposed:**
   Password hashes remain in SQLite (current behavior). Requesting
   confirmation that this is acceptable.

2. **Database passwords in SQLite.** `databases.db_password` is
   stored in plain text (existing behavior). **Proposed:** Keep in
   SQLite for now. Future encryption is tracked as technical debt
   (out of ARCH-008 scope). OK?

3. **WAL mode + journal_size_limit.** Approved decision #5 requires
   WAL mode. The `journal_size_limit` of 64KB is proposed. Acceptable?

4. **Read pool size of 3.** Approved decision #6 requires a "small
   bounded pool." 3 is proposed for v0.7.0. Acceptable, or prefer a
   different initial size?

5. **Jobs table creation in ARCH-008.** The schema includes a `jobs`
   table that ARCH-010 will use for persistence. Is it acceptable to
   create the table now (with no runtime impact) so ARCH-010 does not
   need a schema migration?

6. **DKIM private key path in SQLite.** The `mail_domains.dkim_private_key_path`
   column stores a filesystem path, not the key material. The actual
   PEM file stays on disk. The `dkim_public_key_dns` column stores
   the public DNS record value (public information). **Proposed:** Both
   are acceptable in SQLite. Confirm?

7. **Smarthost password in SQLite.** The smarthost config includes a
   password for external SMTP relay. Currently stored in `mail_smarthost.db`.
   **Proposed:** Keep in SQLite `mail_config` table. Acceptable?

---

## 35. Acceptance Direction

### Integration validation

- [ ] 257 existing tests pass with SQLite backend.
- [ ] All 16 verification steps pass for every resource type.
- [ ] Fresh install creates correct initial data.
- [ ] Upgrade from real v0.6.0 `.db` files succeeds.
- [ ] Rolling back to TXT storage restores original state.

### Integrity validation

- [ ] `PRAGMA foreign_key_check` passes after migration.
- [ ] `PRAGMA integrity_check` passes after migration.
- [ ] Checksums match between TXT and SQLITE for every type.
- [ ] Production reopen validation loads and verifies all types.

### Failure validation

- [ ] Migration fails with diagnostic on corrupted TXT file.
- [ ] Migration fails with diagnostic on checksum mismatch.
- [ ] Migration fails with diagnostic on integrity check failure.
- [ ] Daemon exits (fail-fast) on migration failure.
- [ ] Original TXT files are readable after failed migration.
- [ ] After fixing source data, retry migration succeeds.

### Idempotency validation

- [ ] Repeated startup after successful migration does not re-import.
- [ ] Archived TXT files found later are ignored.
- [ ] Mixed state with matching data is handled correctly.

### Backup and restore validation

- [ ] Online Backup API creates valid snapshot.
- [ ] Snapshot restores all data correctly.
- [ ] WAL/SHM files are handled correctly during backup and restore.

### Security validation

- [ ] `containercp.db` has permissions 0600.
- [ ] Archive directory has permissions 0700.
- [ ] No PEM private key material exists in SQLite.
- [ ] Missing certificate files are detected and reported.
