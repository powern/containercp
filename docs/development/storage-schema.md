# SQLite Storage Schema

## Phase 4 status

This document describes the SQLite business schema created by schema
migration v1 (`initial_business_schema`).

**Active backend:** TXT (pipe-delimited files). SQLite schema exists
but is NOT yet the active persistence backend. The switch to SQLite
occurs in Phase 11 (Startup Migration Gate).

---

## Table inventory

18 tables total (17 business + 1 configuration).

| # | Table | Purpose | Records in TXT |
|---|-------|---------|----------------|
| 1 | `nodes` | Server nodes (local, remote) | `nodes.db` |
| 2 | `sites` | Hosted websites | `sites.db` |
| 3 | `users` | System users (admin, developer) | `users.db` |
| 4 | `domains` | Domain names per site | `domains.db` |
| 5 | `php_versions` | Available PHP runtimes | `php_versions.db` |
| 6 | `databases` | Site database credentials | `databases.db` |
| 7 | `backups` | Backup archives | `backups.db` |
| 8 | `ssl_certificates` | SSL certificate metadata | `ssl_certificates.db` |
| 9 | `mail_domains` | Mail domain configuration | `mail_domains.db` |
| 10 | `mail_mailboxes` | Mailbox accounts | `mail_mailboxes.db` |
| 11 | `mail_aliases` | Mail alias routing | `mail_aliases.db` |
| 12 | `access_users` | SFTP/access user accounts | `access_users.db` |
| 13 | `access_grants` | Site-level access permissions | `access_grants.db` |
| 14 | `reverse_proxies` | Nginx reverse proxy entries | `reverse_proxies.db` |
| 15 | `profiles` | Web server template profiles | `profiles.db` |
| 16 | `auth_users` | Admin panel authentication | `auth_users.db` |
| 17 | `mail_config` | Mail module state + smarthost config | `mail_state.db` + `mail_smarthost.db` |
| 18 | `schema_migrations` | Migration journal (engine-managed) | — |

**Note on template_profiles:** The legacy `template_profiles.db` TXT
file is NOT a separate SQLite table. It is read once by
`Storage::migrate_template_profiles()` and the data is migrated into
the `profiles` table. No `template_profiles` table exists in SQLite.

---

## Table schemas

### nodes

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| name | TEXT NOT NULL | | Node name |
| type | TEXT NOT NULL | `'local'` | Node type |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

### sites

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| domain | TEXT NOT NULL | | Primary domain |
| owner | TEXT NOT NULL | `''` | Owner username |
| node_id | INTEGER NOT NULL | `0` | Node FK (0 = default/local) |
| web_server | TEXT NOT NULL | `'apache'` | Web server type |
| php_mail_enabled | INTEGER NOT NULL | `0` | PHP mail enabled |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

Index: `idx_sites_node_id` ON `node_id`.

### users

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| username | TEXT NOT NULL | | Login name |
| uid | INTEGER NOT NULL | `0` | System UID |
| home_directory | TEXT NOT NULL | `''` | Home dir path |
| shell | TEXT NOT NULL | `'/usr/sbin/nologin'` | Login shell |
| enabled | INTEGER NOT NULL | `1` | Account enabled |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

### domains

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| fqdn | TEXT NOT NULL | | Fully qualified domain name |
| owner_id | INTEGER NOT NULL | `0` | No FK (0 = system sentinel) |
| site_id | INTEGER NOT NULL | `0` | No FK (0 = orphan/admin) |
| php_version | TEXT NOT NULL | `'8.4'` | PHP version |
| ssl_enabled | INTEGER NOT NULL | `0` | SSL enabled |
| enabled | INTEGER NOT NULL | `1` | Domain active |
| type | TEXT NOT NULL | `'primary'` | Domain type |
| target | TEXT NOT NULL | `''` | Target domain/URL |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

Indices: `idx_domains_site_id` ON `site_id`, `idx_domains_owner_id` ON `owner_id`.

### php_versions

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| version | TEXT NOT NULL | | e.g. "8.4" |
| image | TEXT NOT NULL | | Docker image |
| enabled | INTEGER NOT NULL | `1` | Available for use |
| default_version | INTEGER NOT NULL | `0` | Default for new sites |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

### databases

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| db_name | TEXT NOT NULL | | Database name |
| db_user | TEXT NOT NULL | `''` | Database user |
| db_password | TEXT NOT NULL | `''` | **Sensitive** — plaintext, tech debt |
| engine | TEXT NOT NULL | `'mariadb'` | Database engine |
| version | TEXT NOT NULL | `'lts'` | Engine version |
| owner_id | INTEGER NOT NULL | `0` | No FK (0 = system sentinel) |
| site_id | INTEGER NOT NULL | `0` | No FK (0 = orphan) |
| enabled | INTEGER NOT NULL | `1` | Database active |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

Index: `idx_databases_site_id` ON `site_id`.

### backups

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| site_id | INTEGER NOT NULL | `0` | No FK (0 = orphan) |
| owner_id | INTEGER NOT NULL | `0` | No FK (0 = system) |
| filename | TEXT NOT NULL | | Archive filename |
| type | TEXT NOT NULL | `'manual'` | Backup type |
| size | INTEGER NOT NULL | `0` | File size in bytes |
| created_at | TEXT NOT NULL | | No default — set by application |
| status | TEXT NOT NULL | `'completed'` | Backup status |
| file_path | TEXT NOT NULL | `''` | Filesystem path |
| compression | TEXT NOT NULL | `'gzip'` | Compression type |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

Index: `idx_backups_site_id` ON `site_id`.

### ssl_certificates

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| domain_id | INTEGER NOT NULL | `0` | No FK (0 = orphan) |
| domain | TEXT NOT NULL | | Domain name |
| provider | TEXT NOT NULL | `'placeholder'` | Cert provider |
| certificate_path | TEXT NOT NULL | `''` | Path to fullchain PEM |
| key_path | TEXT NOT NULL | `''` | Path to private key PEM |
| chain_path | TEXT NOT NULL | `''` | Path to chain PEM |
| issued_at | TEXT NOT NULL | `''` | ISO-8601 |
| expires_at | TEXT NOT NULL | `''` | ISO-8601 |
| renew_after | TEXT NOT NULL | `''` | ISO-8601 |
| status | TEXT NOT NULL | `'http_only'` | Cert status |
| auto_renew | INTEGER NOT NULL | `1` | Auto-renew enabled |
| https_enabled | INTEGER NOT NULL | `0` | HTTPS enabled |
| redirect_enabled | INTEGER NOT NULL | `0` | HTTP→HTTPS redirect |
| domains | TEXT NOT NULL | `''` | SAN domains (comma-sep) |
| challenge_type | TEXT NOT NULL | `''` | ACME challenge type |
| last_error | TEXT NOT NULL | `''` | Last error message |
| last_validation | TEXT NOT NULL | `''` | Last validation time |
| renew_attempts | INTEGER NOT NULL | `0` | Retry count |
| version | INTEGER NOT NULL | `1` | Metadata format version |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

Index: `idx_ssl_domain_id` ON `domain_id`.

**Cryptographic boundary:** SSL PEM content stays on filesystem at
`/srv/containercp/ssl/<site_id>/`. SQLite stores only metadata and
file paths.

### mail_domains

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| domain_id | INTEGER NOT NULL | `0` | No FK (0 = external domain) |
| site_id | INTEGER NOT NULL | `0` | No FK (0 = unlinked) |
| domain_name | TEXT NOT NULL | | Mail domain name |
| mode | TEXT NOT NULL | `'disabled'` | MailDomainMode string |
| relay_host | TEXT NOT NULL | `''` | SMTP relay for external modes |
| dkim_selector | TEXT NOT NULL | `'dkim'` | DKIM DNS selector |
| dkim_private_key_path | TEXT NOT NULL | `''` | Path to DKIM private key PEM |
| dkim_public_key_dns | TEXT NOT NULL | `''` | DKIM public DNS record |
| max_mailboxes | INTEGER NOT NULL | `0` | 0 = unlimited |
| max_aliases | INTEGER NOT NULL | `0` | 0 = unlimited |
| catch_all | TEXT NOT NULL | `''` | Catch-all address |
| enabled | INTEGER NOT NULL | `1` | Domain active |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

Indices: `idx_mail_domains_domain_id`, `idx_mail_domains_site_id`.

**Cryptographic boundary:** `dkim_private_key_path` stores a filesystem
path. The actual DKIM private key PEM stays on disk. `dkim_public_key_dns`
is public information.

### mail_mailboxes

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| domain_id | INTEGER NOT NULL | | FK → mail_domains(id) ON DELETE RESTRICT |
| local_part | TEXT NOT NULL | | Local part (before @) |
| password_hash | TEXT NOT NULL | `''` | **Sensitive** — SHA-512-CRYPT hash |
| quota_bytes | INTEGER NOT NULL | `0` | 0 = unlimited |
| quota_messages | INTEGER NOT NULL | `0` | 0 = unlimited |
| enabled | INTEGER NOT NULL | `1` | Mailbox active |
| display_name | TEXT NOT NULL | `''` | Display name |
| forward_to | TEXT NOT NULL | `''` | Forwarding address |
| spam_enabled | INTEGER NOT NULL | `1` | Spam filtering |
| last_login | TEXT NOT NULL | `''` | ISO-8601 |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

Index: `idx_mailboxes_domain_id` ON `domain_id`.

### mail_aliases

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| domain_id | INTEGER NOT NULL | | FK → mail_domains(id) ON DELETE RESTRICT |
| source_local_part | TEXT NOT NULL | | Source local part |
| destination | TEXT NOT NULL | | Full destination address |
| enabled | INTEGER NOT NULL | `1` | Alias active |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

Index: `idx_aliases_domain_id` ON `domain_id`.

### access_users

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| username | TEXT NOT NULL | | SFTP username |
| auth_type | TEXT NOT NULL | `'password'` | Auth method |
| password_hash | TEXT NOT NULL | `''` | **Sensitive** — credential verifier |
| enabled | INTEGER NOT NULL | `1` | User active |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

### access_grants

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| access_user_id | INTEGER NOT NULL | | FK → access_users(id) ON DELETE RESTRICT |
| site_id | INTEGER NOT NULL | `0` | No FK (0 = undesignated) |
| permission | TEXT NOT NULL | `'read_write'` | Permission level |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

Indices: `idx_grants_user_id` ON `access_user_id`, `idx_grants_site_id` ON `site_id`.

### reverse_proxies

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| domain | TEXT NOT NULL | | Proxy domain |
| site_id | INTEGER NOT NULL | `0` | No FK (0 = admin panel) |
| provider | TEXT NOT NULL | `'nginx'` | Proxy provider |
| config_path | TEXT NOT NULL | `''` | Nginx config path |
| upstream | TEXT NOT NULL | `''` | Upstream target |
| enabled | INTEGER NOT NULL | `1` | Proxy active |
| status | TEXT NOT NULL | `'active'` | Proxy status |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

Index: `idx_proxies_site_id` ON `site_id`.

### profiles

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| profile_name | TEXT NOT NULL | | Profile name |
| type | TEXT NOT NULL | `'web_server'` | Profile type |
| web_server | TEXT NOT NULL | `'apache'` | Web server software |
| runtime | TEXT NOT NULL | `'docker'` | Container runtime |
| template_path | TEXT NOT NULL | `''` | Config template path |
| description | TEXT NOT NULL | `''` | Human-readable description |
| enabled | INTEGER NOT NULL | `1` | Profile available |
| default_profile | INTEGER NOT NULL | `0` | Default for new sites |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

### auth_users

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| id | INTEGER PK | | Auto-increment |
| username | TEXT NOT NULL | | Admin login name |
| password_hash | TEXT NOT NULL | `''` | **Sensitive** — credential verifier |
| must_change_password | INTEGER NOT NULL | `0` | Force password change |
| enabled | INTEGER NOT NULL | `1` | Account active |
| role | TEXT NOT NULL | `'admin'` | Authorization role |
| created_at | TEXT NOT NULL | ISO-8601 UTC | |
| updated_at | TEXT NOT NULL | ISO-8601 UTC | |

### mail_config

| Column | Type | Default | Notes |
|--------|------|---------|-------|
| key | TEXT PK | | Config key name |
| value | TEXT NOT NULL | | Config value |

Expected keys: `module_state` (`"active"` or `"inactive"`), `smarthost`
(JSON with host/port/username/password/enabled).

---

## Foreign key matrix

FKs are enforced only where the referenced resource always exists.
Sentinel values (0) are valid for FK-free columns.

| Referencing table | Column | Referenced table | Policy | Sentinel 0 valid? |
|-------------------|--------|-----------------|--------|-------------------|
| `mail_mailboxes` | `domain_id` | `mail_domains(id)` | ON DELETE RESTRICT | No |
| `mail_aliases` | `domain_id` | `mail_domains(id)` | ON DELETE RESTRICT | No |
| `access_grants` | `access_user_id` | `access_users(id)` | ON DELETE RESTRICT | No |

The following relationships have NO FK constraint because the
referenced resource may not exist (sentinel 0 or orphan value):

| Table | Column | Sentinel meaning |
|-------|--------|------------------|
| `sites` | `node_id` | 0 = default/local node |
| `domains` | `owner_id` | 0 = system owner |
| `domains` | `site_id` | 0 = orphan/admin panel |
| `databases` | `owner_id` | 0 = system owner |
| `databases` | `site_id` | 0 = orphan |
| `backups` | `site_id` | 0 = orphan (site deleted) |
| `backups` | `owner_id` | 0 = system owner |
| `ssl_certificates` | `domain_id` | 0 = orphan (domain deleted) |
| `mail_domains` | `domain_id` | 0 = external domain |
| `mail_domains` | `site_id` | 0 = unlinked |
| `access_grants` | `site_id` | 0 = undesignated |
| `reverse_proxies` | `site_id` | 0 = admin panel |

---

## Indices

| Index | Table | Column(s) |
|-------|-------|-----------|
| `idx_sites_node_id` | sites | node_id |
| `idx_domains_site_id` | domains | site_id |
| `idx_domains_owner_id` | domains | owner_id |
| `idx_databases_site_id` | databases | site_id |
| `idx_backups_site_id` | backups | site_id |
| `idx_ssl_domain_id` | ssl_certificates | domain_id |
| `idx_mail_domains_domain_id` | mail_domains | domain_id |
| `idx_mail_domains_site_id` | mail_domains | site_id |
| `idx_mailboxes_domain_id` | mail_mailboxes | domain_id |
| `idx_aliases_domain_id` | mail_aliases | domain_id |
| `idx_grants_user_id` | access_grants | access_user_id |
| `idx_grants_site_id` | access_grants | site_id |
| `idx_proxies_site_id` | reverse_proxies | site_id |

---

## Excluded tables and columns

### Not created in Phase 4

| Table | Reason |
|-------|--------|
| `jobs` | ARCH-010 owns job persistence. Created by ARCH-010 via its own migration. |

### Not stored in SQLite

| Data | Location | Reason |
|------|----------|--------|
| SSL certificate PEM | `/srv/containercp/ssl/<id>/fullchain.pem` | Filesystem — read by nginx, postfix, dovecot |
| SSL private key PEM | `/srv/containercp/ssl/<id>/privkey.pem` | Filesystem — cryptographic material |
| SSL chain PEM | `/srv/containercp/ssl/<id>/chain.pem` | Filesystem |
| ACME account key | `/srv/containercp/ssl/<id>/` | Filesystem — cryptographic material |
| DKIM private key PEM | `<dkim_dir>/<domain>/<selector>.private` | Filesystem — cryptographic material |
| SSH host keys | `/etc/ssh/` | Filesystem — system-managed |
| SSH user private keys | User-provided | Filesystem — never generated by ContainerCP |

---

## Migration version and descriptor strategy

| Migration | Version | Name | Descriptor |
|-----------|---------|------|------------|
| Initial business schema | 1 | `initial_business_schema` | Full canonical DDL string |

The descriptor is the complete DDL SQL. Any DDL change automatically
produces a different SHA-256 checksum, preventing silent schema drift.

Future schema changes must use incrementing version numbers (2, 3, ...)
with their own unique descriptors.

---

## Storage metadata values

Set by migration v1:

| Key | Value | Meaning |
|-----|-------|---------|
| `storage_backend` | `"sqlite_schema"` | Schema exists in SQLite, but TXT is the active persistence backend |
| `schema_version` | `"1"` | Current schema version |
| `migration_state` | `"schema_created"` | Schema migration completed |

The `storage_backend` value `"sqlite_schema"` explicitly means "SQLite
schema exists" — it does NOT mean SQLite is the active backend. The
active backend switches to SQLite only after Phase 11 (Startup
Migration Gate) verifies the TXT→SQLite migration.

---

## Extension rules

1. Every new schema change MUST be a new `Migration` with an
   incremented version number registered in `SchemaMigrations.cpp`.
2. The descriptor MUST be the canonical DDL or a deterministic
   representation of the schema change.
3. New tables MUST use `CREATE TABLE IF NOT EXISTS`.
4. New indices MUST use `CREATE INDEX IF NOT EXISTS`.
5. FK constraints with `ON DELETE RESTRICT` are preferred for
   mandatory parent-child relationships.
6. Sentinel 0 relationships must NOT have FK constraints.
7. No PEM, private key, or SSH key content columns may be added.
8. No `jobs` table — ARCH-010 owns that via its own migration.
