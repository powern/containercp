# SQLite Storage Schema

## Phase 4 status

This document describes the SQLite business schema created by schema
migration v1 (`initial_business_schema`).

**Active backend:** TXT (pipe-delimited files). SQLite schema exists
but is NOT yet the active persistence backend. The switch to SQLite
occurs in Phase 11 (Startup Migration Gate).

---

## Table inventory

A fully initialized empty SQLite database contains **19 project tables**
in three categories:

### Application tables created by migration v1 (17)

**16 resource tables:**

| # | Table | TXT source |
|---|-------|------------|
| 1 | `nodes` | `nodes.db` |
| 2 | `sites` | `sites.db` |
| 3 | `users` | `users.db` |
| 4 | `domains` | `domains.db` |
| 5 | `php_versions` | `php_versions.db` |
| 6 | `databases` | `databases.db` |
| 7 | `backups` | `backups.db` |
| 8 | `ssl_certificates` | `ssl_certificates.db` |
| 9 | `mail_domains` | `mail_domains.db` |
| 10 | `mail_mailboxes` | `mail_mailboxes.db` |
| 11 | `mail_aliases` | `mail_aliases.db` |
| 12 | `access_users` | `access_users.db` |
| 13 | `access_grants` | `access_grants.db` |
| 14 | `reverse_proxies` | `reverse_proxies.db` |
| 15 | `profiles` | `profiles.db` |
| 16 | `auth_users` | `auth_users.db` |

**1 configuration table:**

| # | Table | TXT source |
|---|-------|------------|
| 17 | `mail_config` | `mail_state.db` + `mail_smarthost.db` |

### MigrationEngine metadata tables (2)

| # | Table | Managed by |
|---|-------|------------|
| 18 | `schema_migrations` | MigrationEngine (see `docs/development/migration-api.md`) |
| 19 | `storage_meta` | MigrationEngine (see `docs/development/migration-api.md`) |

### Notes

- **No `template_profiles` table.** The legacy `template_profiles.db`
  TXT file is read once by `Storage::migrate_template_profiles()` and
  the data is mapped into the `profiles` table.
- **No `jobs` table.** Job persistence is owned by ARCH-010.
- **No PEM or private-key content columns.** All cryptographic material
  remains filesystem-managed.

---

## Table schemas

### `nodes`

Server nodes. A fresh installation seeds one local node.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment primary key |
| `name` | TEXT | NOT NULL | — | Node name |
| `type` | TEXT | NOT NULL | `'local'` | Node type |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Indices: none (PK index implicit).

---

### `sites`

Hosted websites. Each site represents one container stack.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `domain` | TEXT | NOT NULL | — | Primary domain name |
| `owner` | TEXT | NOT NULL | `''` | Owner username |
| `node_id` | INTEGER | NOT NULL | `0` | No FK — 0 = default/local node sentinel |
| `web_server` | TEXT | NOT NULL | `'apache'` | `'apache'` or `'nginx'` |
| `php_mail_enabled` | INTEGER | NOT NULL | `0` | 0 = disabled, 1 = enabled |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Index: `idx_sites_node_id` ON `node_id`.

---

### `users`

System user accounts (admin, developers).

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `username` | TEXT | NOT NULL | — | Login name |
| `uid` | INTEGER | NOT NULL | `0` | System UID |
| `home_directory` | TEXT | NOT NULL | `''` | Home directory path |
| `shell` | TEXT | NOT NULL | `'/usr/sbin/nologin'` | Login shell |
| `enabled` | INTEGER | NOT NULL | `1` | 0 = disabled, 1 = enabled |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Indices: none.

---

### `domains`

Domain names associated with sites.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `fqdn` | TEXT | NOT NULL | — | Fully qualified domain name |
| `owner_id` | INTEGER | NOT NULL | `0` | No FK — 0 = system sentinel |
| `site_id` | INTEGER | NOT NULL | `0` | No FK — 0 = orphan/admin panel sentinel |
| `php_version` | TEXT | NOT NULL | `'8.4'` | PHP version for this domain |
| `ssl_enabled` | INTEGER | NOT NULL | `0` | SSL requested |
| `enabled` | INTEGER | NOT NULL | `1` | Domain active |
| `type` | TEXT | NOT NULL | `'primary'` | `'primary'`, `'alias'`, `'redirect'`, `'wildcard'` |
| `target` | TEXT | NOT NULL | `''` | Target site domain or redirect URL |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Indices: `idx_domains_site_id` ON `site_id`, `idx_domains_owner_id` ON `owner_id`.

---

### `php_versions`

Available PHP runtime versions.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `version` | TEXT | NOT NULL | — | e.g. `'8.4'` |
| `image` | TEXT | NOT NULL | — | Docker image reference |
| `enabled` | INTEGER | NOT NULL | `1` | Available for site creation |
| `default_version` | INTEGER | NOT NULL | `0` | Default for new sites |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Indices: none.

---

### `databases`

Site database credentials.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `db_name` | TEXT | NOT NULL | — | Database name |
| `db_user` | TEXT | NOT NULL | `''` | Database login user |
| `db_password` | TEXT | NOT NULL | `''` | **Sensitive** — plaintext (tech debt) |
| `engine` | TEXT | NOT NULL | `'mariadb'` | Database engine |
| `version` | TEXT | NOT NULL | `'lts'` | Engine version |
| `owner_id` | INTEGER | NOT NULL | `0` | No FK — 0 = system sentinel |
| `site_id` | INTEGER | NOT NULL | `0` | No FK — 0 = orphan sentinel |
| `enabled` | INTEGER | NOT NULL | `1` | Database active |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Index: `idx_databases_site_id` ON `site_id`.

**Security:** `db_password` contains plaintext database passwords. This
is a backward-compatibility measure — future encryption is tracked as
technical debt. Never expose `db_password` through API responses, Web
UI, logs, or audit output.

---

### `backups`

Backup archive metadata.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `site_id` | INTEGER | NOT NULL | `0` | No FK — 0 = orphan site sentinel |
| `owner_id` | INTEGER | NOT NULL | `0` | No FK — 0 = system sentinel |
| `filename` | TEXT | NOT NULL | — | Archive filename |
| `type` | TEXT | NOT NULL | `'manual'` | Backup type |
| `size` | INTEGER | NOT NULL | `0` | File size in bytes |
| `created_at` | TEXT | NOT NULL | — | ISO-8601 UTC — no default, set by application |
| `status` | TEXT | NOT NULL | `'completed'` | Backup status |
| `file_path` | TEXT | NOT NULL | `''` | Filesystem path to archive |
| `compression` | TEXT | NOT NULL | `'gzip'` | Compression algorithm |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Index: `idx_backups_site_id` ON `site_id`.

---

### `ssl_certificates`

SSL certificate metadata. PEM content stays on the filesystem.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `domain_id` | INTEGER | NOT NULL | `0` | No FK — 0 = orphan sentinel |
| `domain` | TEXT | NOT NULL | — | Primary domain name |
| `provider` | TEXT | NOT NULL | `'placeholder'` | Certificate provider |
| `certificate_path` | TEXT | NOT NULL | `''` | Filesystem path to fullchain PEM |
| `key_path` | TEXT | NOT NULL | `''` | Filesystem path to private key PEM |
| `chain_path` | TEXT | NOT NULL | `''` | Filesystem path to chain PEM |
| `issued_at` | TEXT | NOT NULL | `''` | ISO-8601 UTC |
| `expires_at` | TEXT | NOT NULL | `''` | ISO-8601 UTC |
| `renew_after` | TEXT | NOT NULL | `''` | ISO-8601 UTC — earliest renewal time |
| `status` | TEXT | NOT NULL | `'http_only'` | `'http_only'`, `'requested'`, `'issuing'`, `'active'`, `'expired'`, `'error'` |
| `auto_renew` | INTEGER | NOT NULL | `1` | Auto-renewal enabled |
| `https_enabled` | INTEGER | NOT NULL | `0` | HTTPS configured |
| `redirect_enabled` | INTEGER | NOT NULL | `0` | HTTP→HTTPS redirect |
| `domains` | TEXT | NOT NULL | `''` | SAN domain list (comma-separated) |
| `challenge_type` | TEXT | NOT NULL | `''` | ACME challenge type |
| `last_error` | TEXT | NOT NULL | `''` | Last issuance/renewal error |
| `last_validation` | TEXT | NOT NULL | `''` | Last validation timestamp |
| `renew_attempts` | INTEGER | NOT NULL | `0` | Consecutive renewal failures |
| `version` | INTEGER | NOT NULL | `1` | Metadata format version |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Index: `idx_ssl_domain_id` ON `domain_id`.

**Filesystem boundary:** SSL PEM content stays on the filesystem at
`/srv/containercp/ssl/<site_id>/`. SQLite stores only metadata and
file paths (`certificate_path`, `key_path`, `chain_path`). Nginx,
Postfix, Dovecot, and ACME clients read PEM files from the filesystem.

---

### `mail_domains`

Mail domain configuration per domain.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `domain_id` | INTEGER | NOT NULL | `0` | No FK — 0 = external domain sentinel |
| `site_id` | INTEGER | NOT NULL | `0` | No FK — 0 = unlinked sentinel |
| `domain_name` | TEXT | NOT NULL | — | Mail domain name |
| `mode` | TEXT | NOT NULL | `'disabled'` | `MailDomainMode`: `'disabled'`, `'local-primary'`, `'external-relay'`, `'split-m365'` |
| `relay_host` | TEXT | NOT NULL | `''` | SMTP relay host for external modes |
| `dkim_selector` | TEXT | NOT NULL | `'dkim'` | DKIM DNS selector |
| `dkim_private_key_path` | TEXT | NOT NULL | `''` | Filesystem path to DKIM private key PEM |
| `dkim_public_key_dns` | TEXT | NOT NULL | `''` | DKIM public DNS TXT record value |
| `max_mailboxes` | INTEGER | NOT NULL | `0` | 0 = unlimited |
| `max_aliases` | INTEGER | NOT NULL | `0` | 0 = unlimited |
| `catch_all` | TEXT | NOT NULL | `''` | Catch-all email address |
| `enabled` | INTEGER | NOT NULL | `1` | Domain processing enabled |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Indices: `idx_mail_domains_domain_id` ON `domain_id`,
`idx_mail_domains_site_id` ON `site_id`.

**Filesystem boundary:** `dkim_private_key_path` stores a filesystem
path. The actual DKIM private key PEM remains on the filesystem.
`dkim_public_key_dns` is a public DNS record value — not private key
content.

---

### `mail_mailboxes`

Mailbox accounts within a mail domain.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `domain_id` | INTEGER | NOT NULL | — | FK → `mail_domains(id)` ON DELETE RESTRICT |
| `local_part` | TEXT | NOT NULL | — | Local part (before @) |
| `password_hash` | TEXT | NOT NULL | `''` | **Sensitive** — Dovecot SHA-512-CRYPT hash |
| `quota_bytes` | INTEGER | NOT NULL | `0` | 0 = unlimited |
| `quota_messages` | INTEGER | NOT NULL | `0` | 0 = unlimited |
| `enabled` | INTEGER | NOT NULL | `1` | Mailbox active |
| `display_name` | TEXT | NOT NULL | `''` | Display name |
| `forward_to` | TEXT | NOT NULL | `''` | Forwarding destination |
| `spam_enabled` | INTEGER | NOT NULL | `1` | Spam filtering |
| `last_login` | TEXT | NOT NULL | `''` | ISO-8601 UTC |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Index: `idx_mailboxes_domain_id` ON `domain_id`.

**Security:** `password_hash` contains a Dovecot-compatible
SHA-512-CRYPT password hash (credential verifier, not a private key).
Never expose through API responses, Web UI, logs, or audit output.

---

### `mail_aliases`

Mail alias routing within a mail domain.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `domain_id` | INTEGER | NOT NULL | — | FK → `mail_domains(id)` ON DELETE RESTRICT |
| `source_local_part` | TEXT | NOT NULL | — | Source alias local part |
| `destination` | TEXT | NOT NULL | — | Full destination email address |
| `enabled` | INTEGER | NOT NULL | `1` | Alias active |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Index: `idx_aliases_domain_id` ON `domain_id`.

---

### `access_users`

SFTP/access user accounts.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `username` | TEXT | NOT NULL | — | SFTP username |
| `auth_type` | TEXT | NOT NULL | `'password'` | Authentication method |
| `password_hash` | TEXT | NOT NULL | `''` | **Sensitive** — credential verifier hash |
| `enabled` | INTEGER | NOT NULL | `1` | User active |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Indices: none.

---

### `access_grants`

Site-level access permissions for SFTP users.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `access_user_id` | INTEGER | NOT NULL | — | FK → `access_users(id)` ON DELETE RESTRICT |
| `site_id` | INTEGER | NOT NULL | — | FK → `sites(id)` ON DELETE RESTRICT |
| `permission` | TEXT | NOT NULL | `'read_write'` | `'read_only'`, `'read_write'`, `'deploy'` |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Indices: `idx_grants_user_id` ON `access_user_id`,
`idx_grants_site_id` ON `site_id`.

**Both `access_user_id` and `site_id` have enforced FK constraints.**
Sentinel 0 is NOT valid for either column.

---

### `reverse_proxies`

Nginx reverse proxy configuration entries.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `domain` | TEXT | NOT NULL | — | Proxy domain name |
| `site_id` | INTEGER | NOT NULL | `0` | No FK — 0 = admin panel sentinel |
| `provider` | TEXT | NOT NULL | `'nginx'` | Proxy provider |
| `config_path` | TEXT | NOT NULL | `''` | Nginx config file path |
| `upstream` | TEXT | NOT NULL | `''` | Upstream target |
| `enabled` | INTEGER | NOT NULL | `1` | Proxy active |
| `status` | TEXT | NOT NULL | `'active'` | Proxy status |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Index: `idx_proxies_site_id` ON `site_id`.

---

### `profiles`

Web server template profiles.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `profile_name` | TEXT | NOT NULL | — | Profile name |
| `type` | TEXT | NOT NULL | `'web_server'` | ProfileType string |
| `web_server` | TEXT | NOT NULL | `'apache'` | `'apache'` or `'nginx'` |
| `runtime` | TEXT | NOT NULL | `'docker'` | Container runtime |
| `template_path` | TEXT | NOT NULL | `''` | Config template path on disk |
| `description` | TEXT | NOT NULL | `''` | Human-readable description |
| `enabled` | INTEGER | NOT NULL | `1` | Available for use |
| `default_profile` | INTEGER | NOT NULL | `0` | Default for new sites |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Indices: none.

---

### `auth_users`

Admin panel authentication users.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `id` | INTEGER | NOT NULL | PK | Auto-increment |
| `username` | TEXT | NOT NULL | — | Admin login name |
| `password_hash` | TEXT | NOT NULL | `''` | **Sensitive** — SHA-256 credential verifier |
| `must_change_password` | INTEGER | NOT NULL | `0` | Force password change on next login |
| `enabled` | INTEGER | NOT NULL | `1` | Account active |
| `role` | TEXT | NOT NULL | `'admin'` | Authorization role |
| `created_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |
| `updated_at` | TEXT | NOT NULL | `strftime(...)` | ISO-8601 UTC |

Indices: none.

---

### `mail_config`

Mail module configuration key-value store.

| Column | Type | Nullable | Default | Notes |
|--------|------|----------|---------|-------|
| `key` | TEXT | NOT NULL | PK | Config key name |
| `value` | TEXT | NOT NULL | — | Config value |

Expected keys:

| Key | Value type | Example |
|-----|------------|---------|
| `module_state` | `'active'` or `'inactive'` | `'active'` |
| `smarthost` | JSON | `'{"host":"smtp.example.com","port":587,"username":"u","password":"p","enabled":true}'` |

**Security:** The `smarthost` value may contain SMTP relay credentials.
Never expose through API responses, Web UI, logs, or audit output.

---

### MigrationEngine metadata tables

#### `schema_migrations`

Managed by MigrationEngine. See `docs/development/migration-api.md` for
the complete contract.

| Column | Type | Notes |
|--------|------|-------|
| `version` | INTEGER PK | Migration version number |
| `name` | TEXT NOT NULL | Migration name |
| `checksum` | TEXT NOT NULL | SHA-256 of version + descriptor |
| `started_at` | TEXT NOT NULL | ISO-8601 UTC |
| `completed_at` | TEXT | ISO-8601 UTC, NULL if incomplete |
| `status` | TEXT NOT NULL | `'pending'`, `'running'`, `'completed'`, `'failed'` |
| `diagnostics` | TEXT | Error details for failed migrations |

#### `storage_meta`

Managed by MigrationEngine. Key-value store for Storage metadata.

| Column | Type | Notes |
|--------|------|-------|
| `key` | TEXT PK | Metadata key |
| `value` | TEXT NOT NULL | Metadata value |

Current values (set by migration v1):

| Key | Value | Meaning |
|-----|-------|---------|
| `storage_backend` | `"sqlite_schema"` | Schema exists in SQLite; TXT is the active persistence backend |
| `schema_version` | `"1"` | Current schema version |
| `migration_state` | `"schema_created"` | Schema migration completed |

---

## Foreign key policy

### FK-enforced relationships (ON DELETE RESTRICT)

4 relationships with mandatory parent rows — sentinel 0 is NOT valid.

| Child table | Column | Parent table | Policy |
|------------|--------|-------------|--------|
| `mail_mailboxes` | `domain_id` | `mail_domains(id)` | ON DELETE RESTRICT |
| `mail_aliases` | `domain_id` | `mail_domains(id)` | ON DELETE RESTRICT |
| `access_grants` | `access_user_id` | `access_users(id)` | ON DELETE RESTRICT |
| `access_grants` | `site_id` | `sites(id)` | ON DELETE RESTRICT |

### Sentinel-capable no-FK relationships (11 columns)

These columns intentionally have NO SQLite foreign key constraint.
Sentinel value 0 (or other non-existent parent IDs) is valid.
Application-level validation enforces business integrity.

| Table | Column | Sentinel meaning |
|-------|--------|------------------|
| `sites` | `node_id` | 0 = default/local node |
| `domains` | `owner_id` | 0 = system owner |
| `domains` | `site_id` | 0 = orphan/admin panel |
| `databases` | `owner_id` | 0 = system owner |
| `databases` | `site_id` | 0 = orphan |
| `backups` | `owner_id` | 0 = system owner |
| `backups` | `site_id` | 0 = orphan (site deleted) |
| `ssl_certificates` | `domain_id` | 0 = orphan (domain deleted) |
| `mail_domains` | `domain_id` | 0 = external domain |
| `mail_domains` | `site_id` | 0 = unlinked |
| `reverse_proxies` | `site_id` | 0 = admin panel |

---

## Indices (13)

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

## Sensitive fields

| Table | Column | Classification | Rule |
|-------|--------|---------------|------|
| `databases` | `db_password` | Plaintext credential (tech debt) | Never expose via API, Web UI, logs, or audit |
| `mail_mailboxes` | `password_hash` | Credential verifier (SHA-512-CRYPT) | Never expose; hash, not plaintext |
| `access_users` | `password_hash` | Credential verifier | Never expose |
| `auth_users` | `password_hash` | Credential verifier (SHA-256) | Never expose |
| `mail_config` | value (smarthost) | May contain SMTP credentials | Never expose |

## Filesystem-managed content

SQLite contains NO PEM content, NO private key material.

| Data | Location in SQLite | Filesystem location |
|------|-------------------|-------------------|
| SSL certificate PEM | `certificate_path`, `key_path`, `chain_path` (paths only) | `/srv/containercp/ssl/<site_id>/` |
| ACME account key | Not stored (path not tracked) | `/srv/containercp/ssl/<site_id>/` |
| DKIM private key PEM | `dkim_private_key_path` (path only) | `<dkim_dir>/<domain>/<selector>.private` |
| SSH host keys | Not stored | `/etc/ssh/` |

---

## Migration version and descriptor

| Migration | Version | Name | Descriptor |
|-----------|---------|------|------------|
| Initial business schema | 1 | `initial_business_schema` | Full canonical DDL string |

The descriptor is the complete DDL SQL. Any DDL change automatically
produces a different SHA-256 checksum, preventing silent schema drift.

Future schema changes must use incrementing version numbers with their
own unique descriptors.

---

## Extension rules

1. Every new schema change MUST be a new `Migration` with an
   incremented version.
2. The descriptor MUST be the canonical DDL or a deterministic
   representation of the change.
3. New tables MUST use `CREATE TABLE IF NOT EXISTS`.
4. New indices MUST use `CREATE INDEX IF NOT EXISTS`.
5. FK constraints with `ON DELETE RESTRICT` for mandatory parent
   relationships.
6. Sentinel-capable columns must NOT have SQLite FK constraints
   (see the 11-column list above).
7. No PEM, private key, or SSH key content columns may be added.
8. No `jobs` table — ARCH-010 owns that via its own migration.
