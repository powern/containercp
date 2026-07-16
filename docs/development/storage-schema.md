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

### Application tables created by Phase 4 v1 (17)

**16 resource tables:**

| # | Table | Purpose | TXT source |
|---|-------|---------|------------|
| 1 | `nodes` | Server nodes | `nodes.db` |
| 2 | `sites` | Hosted websites | `sites.db` |
| 3 | `users` | System users | `users.db` |
| 4 | `domains` | Domain names | `domains.db` |
| 5 | `php_versions` | PHP runtimes | `php_versions.db` |
| 6 | `databases` | Database credentials | `databases.db` |
| 7 | `backups` | Backup archives | `backups.db` |
| 8 | `ssl_certificates` | SSL metadata | `ssl_certificates.db` |
| 9 | `mail_domains` | Mail domain config | `mail_domains.db` |
| 10 | `mail_mailboxes` | Mailbox accounts | `mail_mailboxes.db` |
| 11 | `mail_aliases` | Mail alias routing | `mail_aliases.db` |
| 12 | `access_users` | SFTP users | `access_users.db` |
| 13 | `access_grants` | Access permissions | `access_grants.db` |
| 14 | `reverse_proxies` | Proxy entries | `reverse_proxies.db` |
| 15 | `profiles` | Web server profiles | `profiles.db` |
| 16 | `auth_users` | Admin auth users | `auth_users.db` |

**1 configuration table:**

| # | Table | Purpose | TXT source |
|---|-------|---------|------------|
| 17 | `mail_config` | Mail module state + smarthost | `mail_state.db` + `mail_smarthost.db` |

### MigrationEngine metadata tables (2)

| # | Table | Purpose |
|---|-------|---------|
| 18 | `schema_migrations` | Migration journal (engine-managed) |
| 19 | `storage_meta` | Storage subsystem metadata (engine-managed) |

### Notes

- **No `template_profiles` table.** The legacy `template_profiles.db`
  TXT file is read once by `Storage::migrate_template_profiles()`.
  The data is mapped into the `profiles` table.
- **No `jobs` table.** Job persistence is owned by ARCH-010.
- **No PEM or private-key content columns.** All cryptographic material
  remains filesystem-managed.

---

## Foreign key policy

### FK-enforced relationships (ON DELETE RESTRICT)

These relationships MUST reference a real parent row. Sentinel 0 is
NOT valid.

| Referencing table | Column | Referenced table | Policy |
|-------------------|--------|-----------------|--------|
| `mail_mailboxes` | `domain_id` | `mail_domains(id)` | ON DELETE RESTRICT |
| `mail_aliases` | `domain_id` | `mail_domains(id)` | ON DELETE RESTRICT |
| `access_grants` | `access_user_id` | `access_users(id)` | ON DELETE RESTRICT |
| `access_grants` | `site_id` | `sites(id)` | ON DELETE RESTRICT |

### No-FK relationships (sentinel 0 valid)

These relationships intentionally have NO SQLite foreign key because
the approved historical model requires value 0 where no persisted
parent row 0 exists. Application-level validation is responsible for
enforcing business integrity.

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

**Rule:** Do not add SQLite foreign keys to this list. If a new
relationship requires sentinel 0, it must be approved by the same
process that approved these exceptions.

---

## Foreign key matrix (PRAGMA foreign_key_list)

The following is the authoritative FK matrix as enforced by SQLite.
Inspect using `PRAGMA foreign_key_list(<table>)`.

| Child table | Column | Parent table | Parent column | Delete rule |
|------------|--------|-------------|---------------|-------------|
| `mail_mailboxes` | `domain_id` | `mail_domains` | `id` | RESTRICT |
| `mail_aliases` | `domain_id` | `mail_domains` | `id` | RESTRICT |
| `access_grants` | `access_user_id` | `access_users` | `id` | RESTRICT |
| `access_grants` | `site_id` | `sites` | `id` | RESTRICT |

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

## Table schemas

(Complete column definitions for all 17 application tables follow the
same structure as the canonical DDL in `SchemaMigrations.cpp`.)

---

## Storage metadata values

Set by migration v1:

| Key | Value | Meaning |
|-----|-------|---------|
| `storage_backend` | `"sqlite_schema"` | Schema exists in SQLite; TXT is the active persistence backend |
| `schema_version` | `"1"` | Current schema version |
| `migration_state` | `"schema_created"` | Schema migration completed |

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
6. Sentinel-capable relationships must NOT have SQLite FK constraints
   (see the No-FK list above). Application-level validation enforces
   business integrity for these.
7. No PEM, private key, or SSH key content columns may be added.
8. No `jobs` table — ARCH-010 owns that via its own schema migration.
