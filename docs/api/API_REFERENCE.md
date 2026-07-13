# REST API Reference

Before implementing, modifying, or reviewing ANY REST API endpoint,
read this document first.  It is the authoritative index of all
REST API endpoints.

---

## 1. Entry point

All REST API routes are registered in `libs/api/ApiServer.cpp`.
The `ApiServer` class implements an HTTP server on port 8081 (Web UI
static files on 8080).  API handlers are registered as lambdas via
`Router::add()` (exact match) or `Router::add_prefix()` (prefix match).

### Request flow

```
HTTP request
    ↓
ApiServer::handle() → Router::dispatch()
    ↓
Route handler lambda (thin — validates input, calls subsystem)
    ↓
Owning manager / service / executor (business logic)
    ↓
Storage / provider / runtime (persistence / external I/O)
    ↓
JSON response ← Response struct with status code
```

### Rules

- API handlers must be **thin** — no business logic, no Docker commands,
  no direct file I/O.
- Business logic belongs in managers, services, providers, or executors.
- Web UI and CLI are only clients of the REST API.

---

## 2. API groups

### 2.1 General

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/version` | Daemon version string | `core::VERSION` |
| GET | `/api/health` | Daemon health check | built-in |

### 2.2 Sites

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/sites` | List all sites | `SiteManager` |
| POST | `/api/sites/create` | Create a new site | `SiteCreateOperation` (via HostingProvider) |
| POST | `/api/sites/remove` | Remove a site by domain | `SiteRemoveOperation` |

**GET /api/sites** — returns `{"success":true,"data":[{"id":N,"domain":"...","owner":"...","web_server":"apache|nginx",...}]}`

**POST /api/sites/create** — body: `{"owner":"...","domain":"...","profile":"..."}`

**POST /api/sites/remove** — body: `{"domain":"..."}`

### 2.3 Runtime

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/runtime/<site_id>` | Per-service container status + HTTPS | `SiteRuntimeManager` → `RuntimeActionExecutor` |
| POST | `/api/runtime/<site_id>/<action>` | Execute runtime restart action | `RuntimeActionExecutor` (async via `JobExecutor`) |

**GET /api/runtime/<site_id>** — returns:

```json
{
  "success": true,
  "data": {
    "web": "Running",
    "php": "Running",
    "db": "Running",
    "cache": "Running",
    "https": "Active"
  }
}
```

Possible status values: `Running`, `Starting`, `Stopped`, `Unhealthy`,
`Unknown`, `Error`.  HTTPS-specific: `Active`, `Disabled`, `Expired`,
`Expiring`, `Error`, `Issuing`.

**POST /api/runtime/<site_id>/<action>** — supported actions:

| Action | Compose services | Meaning |
|--------|-----------------|---------|
| `restart-web` | `web` | Restart frontend (Apache/Nginx) |
| `restart-php` | `php` | Restart PHP-FPM |
| `restart-db` | `mariadb` | Restart database |
| `restart-redis` | `redis` | Restart Redis cache |
| `restart-all` | *(all)* | Restart every service in the compose project |

Returns (HTTP 202):

```json
{
  "success": true,
  "data": {
    "job_id": 42,
    "status": "pending",
    "message": "Action restart-web queued for site 3"
  }
}
```

Flow: ApiServer → `SiteRuntimeManager::services_for_action()` →
`RuntimeActionExecutor::restart_services()` via `JobExecutor`.

### 2.4 Jobs

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/jobs` | List all jobs, or query `?id=N` for single job | `JobManager` |

**GET /api/jobs** — returns array of jobs.

**GET /api/jobs?id=123** — returns single job with `current_step` field.

Jobs have: `id`, `type` (e.g. `ssl-issue`, `runtime-restart-web`),
`status` (`pending`|`running`|`completed`|`failed`), `progress` (0-100),
`message`, `created_at`.

### 2.5 SSL / Certificates

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/ssl` | List all sites with SSL metadata | `CertificateStore` |
| GET | `/api/ssl/providers` | List available cert providers | `CertificateProvider` |
| GET | `/api/ssl/<domain>` | Single domain SSL details | `CertificateStore` |
| POST | `/api/ssl/<domain>/issue` | Issue certificate (async) | `CertificateProvider` via `JobExecutor` |
| POST | `/api/ssl/<domain>/renew` | Renew certificate (async) | `CertificateProvider` via `JobExecutor` |
| POST | `/api/ssl/<domain>/enable` | Enable HTTPS (proxy attach) | `CertificateStore` + `ProxyProvider` |
| POST | `/api/ssl/<domain>/disable` | Disable HTTPS (proxy detach) | same |
| POST | `/api/ssl/<domain>/redirect/enable` | Enable HTTP→HTTPS redirect | same |
| POST | `/api/ssl/<domain>/redirect/disable` | Disable HTTP→HTTPS redirect | same |
| POST | `/api/ssl/enable` | Legacy: enable HTTPS by domain | `SslCertificateManager` (legacy) |
| POST | `/api/ssl/disable` | Legacy: disable HTTPS by domain | `SslCertificateManager` (legacy) |
| POST | `/api/ssl/remove` | Remove SSL record | `SslCertificateManager` (legacy) |

**GET /api/ssl** — returns per-site: `domain`, `site_id`, `status`
(`HTTP_ONLY`|`active`|`issuing`|`error`|`disabled`), `https_enabled`,
`redirect_enabled`, `auto_renew`, `expires_at`, `last_error`,
`provider_id`, `environment`.

SSL status belongs to `CertificateStore` (single source of truth).
Runtime/Sites pages consume this data — they do not reimplement it.

### 2.6 Settings

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/settings` | Get daemon settings | `Config` |
| POST | `/api/settings` | Update daemon settings | `Config` |

Fields: `version`, `server_hostname`.

### 2.7 Backups

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/backups` | List backups | `BackupManager` |
| POST | `/api/backups/create` | Create a backup | `BackupManager` |
| POST | `/api/backups/remove` | Remove a backup | `BackupManager` |
| POST | `/api/backups/restore` | Restore a backup | `BackupManager` |

### 2.8 Users

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/users` | List system users | `UserManager` |

### 2.9 Domains

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/domains` | List all domains (enriched with site name + SSL status) | `DomainManager` + `SiteManager` + `CertificateStore` |
| POST | `/api/domains/remove` | Remove a domain record | `DomainManager` |

**GET /api/domains** — returns per-domain: `id`, `domain`, `type`
(`primary`|`alias`|`redirect`|`wildcard`), `site_id`, `site_name`,
`site_domain`, `target`, `ssl_enabled`, `ssl_status` (from
`CertificateStore::https_display_status`), `enabled`.

The API handler enriches domain records with site info from
`SiteManager::find_by_id()` and SSL status from
`CertificateStore::load_metadata()` + `https_display_status()`.
No business logic is duplicated — the API consumes the owning
subsystems.

### 2.10 Proxy

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/proxy` | List proxy entries (enriched with site + SSL) | `ProxyViewService` |
| GET | `/api/proxy/health` | Global proxy health (container, config, counts) | `ProxyViewService` + `NginxProxyProvider` |
| POST | `/api/proxy/test` | Validate nginx configuration (`nginx -t`) | `NginxProxyProvider` |
| POST | `/api/proxy/reload` | Validate then reload nginx | `NginxProxyProvider` |
| POST | `/api/proxy/sync` | Regenerate all HTTPS configs from core state | `ServiceRegistry` |
| POST | `/api/proxy/recover` | Full proxy self-healing | `RecoveryManager` |
| POST | `/api/proxy/remove` | Remove a proxy entry (protected: admin entries cannot be removed) | `ReverseProxyManager` + `NginxProxyProvider` |

### 2.11 Databases

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/databases` | List databases | `DatabaseManager` |
| POST | `/api/databases/remove` | Remove a database record | `DatabaseManager` |

### 2.12 Access (SFTP)

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/access-users` | List SFTP access users | `AccessUserManager` |
| POST | `/api/access-users/remove` | Remove an access user | `AccessUserManager` |

### 2.13 Profiles

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/profiles` | List hosting profiles | `ProfileManager` |

### 2.14 Nodes

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/nodes` | List cluster nodes | `ResourceManager` |

### 2.15 Mail — Domains

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/mail/domains` | List all mail domains | `MailDomainManager` |
| GET | `/api/mail/domains/<id>` | Get a single mail domain | `MailDomainManager` |
| POST | `/api/mail/domains` | Create a mail domain | `MailDomainManager` |
| PATCH | `/api/mail/domains/<id>` | Update a mail domain | `MailDomainManager` |
| DELETE | `/api/mail/domains/<id>` | Remove a mail domain | `MailDomainManager` |

Domain modes: `disabled`, `local-primary`, `external-relay`, `split-m365`.

POST body: `{"domain":"example.com","mode":"local-primary","domain_id":1,"relay_host":"smtp.example.com:587"}`

PATCH body (partial update): `{"mode":"split-m365","relay_host":"company-com.mail.protection.outlook.com"}`

Response includes: `id`, `domain`, `mode`, `domain_id`, `site_id`, `enabled`, `relay_host`,
`dkim_selector`, `max_mailboxes`, `max_aliases`, `catch_all`, `created_at`, `updated_at`.

Validation:
- Domain is normalized (lowercase, trimmed, trailing dots removed) before duplicate check.
- Unknown mode strings return 400 with valid options list.
- Duplicate domains return 409.
- `domain_id` must reference an existing ContainerCP Domain (from a Site).  Pass 0 for external domains without a ContainerCP site.
- `relay_host` is required when mode is `external-relay` or `split-m365`.
- `relay_host` must be emptied (by changing mode first) before switching to a mode that does not require it.

### 2.16 Mail — Mailboxes

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/mail/domains/<id>/mailboxes` | List mailboxes for a domain | `MailboxManager` |
| POST | `/api/mail/domains/<id>/mailboxes` | Create a mailbox | `MailboxManager` |
| PATCH | `/api/mail/mailboxes/<id>` | Update a mailbox | `MailboxManager` |
| POST | `/api/mail/mailboxes/<id>/password` | Change mailbox password | `MailboxManager` |
| DELETE | `/api/mail/mailboxes/<id>` | Remove a mailbox | `MailboxManager` |

POST body (create): `{"local_part":"alice","password":"secret"}`

PATCH body (partial update): `{"enabled":false,"quota_bytes":1073741824,"display_name":"Alice","forward_to":"","spam_enabled":true}`

PATCH updates only fields present in the request.  Empty string or JSON null clears
`display_name` and `forward_to`.  `enabled` and `spam_enabled` accept only `true` or `false`.

POST body (password change): `{"password":"newsecret"}`

Password validation:
- Minimum 3 characters.
- Must not be empty.

Password is hashed using SHA-512-CRYPT (`$6$`) before storage.  Plaintext
passwords are never stored.  Hash is compatible with Dovecot's
`default_password_scheme = SHA512-CRYPT`.
`password_hash` is never exposed in API responses.

Validation:
- `local_part` is normalized (lowercased, trimmed) before validation and duplicate check.
- Allowed characters: a-z, 0-9, `.`, `_`, `-` (after normalization).
- Leading dot, trailing dot, consecutive dots, and whitespace are rejected.
- Duplicate `local_part` within the same domain returns 409 (after normalization).
- Password is required (minimum 3 characters).
- Referenced domain must exist (404 if not found).

Response includes full `address` field in the format `local_part@domain`.

### 2.17 Mail — Aliases

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/mail/domains/<id>/aliases` | List aliases for a domain | `MailAliasManager` |
| POST | `/api/mail/domains/<id>/aliases` | Create an alias | `MailAliasManager` |
| PATCH | `/api/mail/aliases/<id>` | Update an alias | `MailAliasManager` |
| DELETE | `/api/mail/aliases/<id>` | Remove an alias | `MailAliasManager` |

POST body: `{"source":"info","destination":"admin@example.com"}`

PATCH body (partial update): `{"destination":"other@example.com","enabled":false}`

Aliases map a source local part to one or more destination addresses.
Multiple aliases with the same source but different destinations are allowed.
An exact duplicate (same source, same destination, same domain) is rejected (409).

Aliases are written to Postfix `virtual_alias_maps` during config generation
and mounted into the Postfix container.  Changes take effect immediately via
`RuntimeSynchronizer` (no manual regenerate needed).

Validation:
- `source` is normalized (lowercased, trimmed) before duplicate check using the same
  rules as Mailbox local_part (alphanumeric, dots, underscores, hyphens only).
- `destination` must contain `@` (basic email format validation).  Empty string and
  JSON null are rejected (alias without a destination has no meaning).
- `enabled` accepts only `true` or `false`.
- Referenced domain must exist (404 if not found).
- Read-only fields: `id`, `domain_id`, `created_at`, `updated_at` cannot be changed via PATCH.
- PATCH semantics: omitted field → unchanged.  Explicit `destination: ""` or
  `destination: null` → 400 Bad Request.

Response format per alias entry:
```json
{
  "id": 1,
  "domain_id": 1,
  "source": "info",
  "destination": "admin@example.com",
  "enabled": true,
  "created_at": "2025-07-10T12:00:00Z",
  "updated_at": "2025-07-10T12:00:00Z"
}
```

### 2.18 Mail — DKIM

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| POST | `/api/mail/domains/<id>/dkim/generate` | Generate DKIM key pair for a domain | `DkimManager` |

Generates a 2048-bit RSA key pair using OpenSSL.  The private key is stored
in `<data_root>/mail/config/state/dkim/<domain>/<selector>.private`.
The public key DNS record is stored in `MailDomain::dkim_public_key_dns`
and returned in the response.

Response:
```json
{
  "success": true,
  "data": {
    "message": "DKIM key generated",
    "dns_record": "v=DKIM1; k=rsa; p=MIGfMA0..."
  }
}
```

The `dns_record` value should be added as a TXT record at
`<selector>._domainkey.<domain>` in the DNS provider.

### 2.19 Mail — Module

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/mail/status` | Query mail module state | `MailDomainManager` |
| GET | `/api/mail/health` | Mail module health report | `HealthRegistry` |
| POST | `/api/mail/activate` | Activate the mail module | `MailDomainManager` |
| POST | `/api/mail/deactivate` | Deactivate the mail module | `MailDomainManager` |

States: `inactive` (default), `active`, `error`.

The mail module is optional.  It is inactive by default.  Data (domains,
mailboxes, aliases) can be configured in any state.  Activating the module
creates Docker containers (Postfix + Dovecot + Redis) and generates mail
server configuration from the current data model.  Deactivating stops
containers without deleting configuration data.

Published ports:
| Service | Ports |
|---------|-------|
| Postfix | 25 (SMTP), 465 (SMTPS), 587 (submission) |
| Dovecot | 143 (IMAP), 993 (IMAPS), 24 (LMTP) |

Status response includes counts of configured domains, mailboxes, and aliases.

### Health response

`GET /api/mail/health` returns the generic `HealthReport` model:

- `status` — aggregate: `"ok"`, `"degraded"`, or `"error"`
- `services` — array of per-service status: `{"name","status","message"}`
- `details` — arbitrary JSON with module-specific data (module state, counts)

Example (active module, all services running):
```json
{
  "success": true,
  "data": {
    "status": "ok",
    "services": [
      {"name": "postfix", "status": "ok", "message": "running"},
      {"name": "dovecot", "status": "ok", "message": "running"},
      {"name": "redis", "status": "ok", "message": "running"}
    ],
    "details": {
      "module_state": "active",
      "domain_count": 3,
      "mailbox_count": 12,
      "alias_count": 5
    }
  }
}
```

The response structure is defined by the `HealthReport` model in
`libs/runtime/`.  Future modules register their own health checks
using the same model.

### 2.20 Mail — Regenerate, Reload, Recover

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| POST | `/api/mail/regenerate` | Regenerate mail server config and reload | `DockerMailProvider` |
| POST | `/api/mail/reload` | Reload Postfix config without full restart | `DockerMailProvider` |
| POST | `/api/mail/recover` | Full restart of mail containers | `DockerMailProvider` |

Regenerates Postfix `main.cf`, Dovecot `dovecot.conf`, virtual mailbox maps,
and Dovecot passwd file from the current ContainerCP data model.  Calls
`reload()` (postfix reload) after writing configs.  Requires the mail module
to be active.  Returns error if module is inactive.

`reload` signals Postfix to reload its configuration without stopping
containers.  Useful after manual config edits.

`recover` performs a full stop + config regenerate + start cycle.
Useful when a container becomes unresponsive or config is corrupted.

### 2.21 Logs

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/logs` | Stub — returns placeholder log entries | built-in |

**Note:** The `/api/logs` endpoint currently returns hardcoded
placeholder data.  This is a known limitation.

---

## 3. Standard response format

### Success (read)

```json
{
  "success": true,
  "data": { ... }
}
```

Arrays are always wrapped in `data`:

```json
{
  "success": true,
  "data": [ ... ]
}
```

### Success (write / no data)

```json
{
  "success": true,
  "message": "Operation completed"
}
```

### Error

```json
{
  "success": false,
  "error": "Human-readable description"
}
```

Some endpoints return an error object with a code:

```json
{
  "success": false,
  "error": {
    "code": "NOT_FOUND",
    "message": "Site not found: example.com"
  }
}
```

### Async job (HTTP 202)

```json
{
  "success": true,
  "data": {
    "job_id": 42,
    "status": "pending",
    "message": "Action queued for site 3"
  }
}
```

---

## 4. HTTP status codes

| Code | Usage |
|------|-------|
| 200 | Successful read / sync write |
| 202 | Accepted — async job created |
| 400 | Bad request — invalid input, missing fields, invalid action |
| 404 | Resource not found |
| 500 | Internal server error |

The legacy `POST /api/ssl/enable` and similar flat endpoints return 200
even for asynchronous operations.  New endpoints should use 202 for
async jobs.

---

## 5. Adding a new endpoint — procedure

1. **Read this document** — check whether an endpoint already exists.
2. **Identify the owning subsystem** — every capability has one owner
   (see `docs/development/single-source-of-truth.md`).
3. **Add business logic to the owning layer** — API handler must be
   thin.  Add methods to the appropriate manager / executor first.
4. **Register the route** in `ApiServer.cpp` using `router_.add()`
   or `router_.add_prefix()`.  Keep the handler short.
5. **Use the standard response format** (section 3 above).  Use
   appropriate HTTP status code (section 4).
6. **Add tests** — test through the owning subsystem's public API.
7. **Update Web UI / CLI** only after the API exists.
8. **Update this document** (`docs/api/API_REFERENCE.md`).
9. **Update CHANGELOG.md**.

---

## 6. Ownership rules (Single Source of Truth)

| Endpoint group | Owned by | Must not duplicate |
|----------------|----------|-------------------|
| `/api/runtime/*` | Runtime subsystem (`RuntimeActionExecutor`) | Docker logic, compose commands |
| `/api/sites/*` | Sites subsystem (`SiteManager`) | Runtime status, SSL status |
| `/api/ssl/*` | SSL subsystem (`CertificateStore`) | Metadata file I/O |
| `/api/jobs/*` | Jobs subsystem (`JobManager`) | Async execution |
| `/api/backups/*` | Backups subsystem (`BackupManager`, `BackupProvider`) | Archive logic |
| `/api/settings/*` | Config (`Config`) | File I/O, persistence |
| `/api/mail/*` | Mail resource management (`MailDomainManager`, `MailboxManager`, `MailAliasManager`) | Mail server config, Docker logic, Postfix/Dovecot (future stages) |

API handlers must never:
- Run `docker` commands directly
- Read `metadata.json` or other data files directly
- Duplicate business logic from the owning subsystem
- Make assumptions about compose service names (use `ServiceRole`)

---

### 2.19 Migration — myVestaCP import

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/migration/vesta/backups` | List available backup files from allowed directories | `VestaSiteImporter` |
| POST | `/api/migration/vesta/inspect` | Read-only analysis of a backup for a specific domain | `VestaSiteImporter` |
| POST | `/api/migration/vesta/create-site` | Stage 1: create site + Docker stack from backup metadata | `SiteCreateOperation` |
| POST | `/api/migration/vesta/import-files` | Stage 2: import web files from backup into site document root | `VestaSiteImporter` |
| POST | `/api/migration/vesta/import-sql` | Stage 3: import SQL dump, update wp-config.php, complete migration | `VestaSiteImporter` |

**Security:**
- Backup files are only accepted from `/backup` and `<data_root>/backups/`
- Canonical path validation via `realpath()`, symlinks rejected via `lstat()`
- Only `.tar` and `.tar.gz` extensions allowed
- Basename must not contain `/`
- POST endpoints re-run `inspect()` before any operation (don't trust browser data)

**GET /api/migration/vesta/backups** — returns list of `.tar`/`.tar.gz` files:

```json
{
  "success": true,
  "data": [
    { "name": "admin.2026-07-01.tar", "size": 245000000, "mtime": 1783866000 }
  ]
}
```

**POST /api/migration/vesta/inspect** — analyze backup structure:

Request:
```json
{
  "backup": "admin.2026-07-01.tar",
  "domain": "example.com",
  "owner": "admin",
  "database": "",
  "skip_db": false,
  "keep_staging": false
}
```

Response:
```json
{
  "success": true,
  "data": {
    "domain_found": true,
    "web_archive_path": "web/example.com/domain_data.tar.gz",
    "web_root_type": "public_html",
    "wp_config_found": true,
    "wp_config_parsed": true,
    "wp_db_ambiguous": false,
    "wp_db_name": "example_db",
    "wp_db_user": "example_user",
    "wp_db_host": "localhost",
    "db_dump_found": true,
    "db_dump_path": "db/example_db/example_db.mysql.sql.gz",
    "db_type": "mysql",
    "site_exists": false,
    "migration_marker_found": false,
    "migration_stage": 0,
    "files_pending": false,
    "files_imported": false,
    "sql_pending": false,
    "can_import_files": false,
    "can_import_sql": false,
    "migration_completed": false,
    "migration_site_id": 0,
    "migration_owner": "",
    "marker_error": "",
    "available_disk_mb": 10240,
    "all_databases": ["example_db"],
    "errors": [],
    "warnings": []
  }
}
```

If site already exists with a valid migration marker (Stage 1 completed), the response shows:

```json
{
  "success": true,
  "data": {
    "domain_found": true,
    "web_root_type": "public_html",
    "wp_config_found": true,
    "site_exists": true,
    "migration_marker_found": true,
    "migration_stage": 1,
    "files_pending": true,
    "files_imported": false,
    "sql_pending": true,
    "can_import_files": true,
    "can_import_sql": false,
    "migration_completed": false,
    "migration_site_id": 42,
    "migration_owner": "admin",
    "marker_error": ""
  }
}
```

Migration marker is stored at `<site_dir>/.containercp-migration.json` with format:
```json
{"domain":"example.com","owner":"admin","site_id":42,"stage":1,"files_pending":true,"files_imported":false,"sql_pending":true}
```

Stage 2 (`import-files`) validates:
- Marker exists and contains `site_id` matching `SiteRecord.id`
- `stage` is 1 and `files_pending` is true
- `DomainRecord.site_id` matches `SiteRecord.id`
- If any check fails, `migration_ready_for_files` is `false` and `marker_error` explains why

**POST /api/migration/vesta/create-site** — Stage 1: create empty site:

Request: same as inspect.

Response:
```json
{
  "success": true,
  "data": {
    "message": "Stage 1 completed — site created",
    "site_id": 42,
    "domain": "example.com",
    "database_name": "example_com_db",
    "database_user": "example_com_user",
    "document_root": "/srv/containercp/sites/example.com/public",
    "status": {
      "site": "created",
      "database": "created",
      "docker_stack": "created",
      "files_import": "pending",
      "sql_import": "pending"
    }
  }
}
```

**POST /api/migration/vesta/import-files** — Stage 2: import web files:

Request: simplified version of inspect — `backup`, `domain`, `owner`, `keep_staging`.

Response:
```json
{
  "success": true,
  "data": {
    "message": "Stage 2 completed — files imported",
    "web_root": "public_html",
    "destination": "/srv/containercp/sites/example.com/public",
    "files_count": 1240,
    "bytes_copied": 45000000,
    "warnings": [],
    "errors": [],
    "status": {
      "files": "imported",
      "sql_import": "pending",
      "wp_config_update": "pending"
    }
  }
}
```


**POST /api/migration/vesta/import-sql** — Stage 3: import SQL dump and complete migration:

Request: same as inspect (`backup`, `domain`, `owner`, `keep_staging`).

Flow:
1. Validates marker stage=2 and `can_import_sql=true`
2. Finds SQL dump in backup archive via `find_db_in_archive()`
3. Reads DB credentials from site `.env` (DB_NAME, DB_USER, DB_PASSWORD)
4. Creates safety backup via `mariadb-dump --single-transaction`
5. Drops existing tables (not the database itself)
6. Imports dump via `gunzip -c | docker exec mariadb`
7. Updates `wp-config.php` — replaces DB_NAME, DB_USER, DB_PASSWORD
8. Restarts PHP container, health check, HTTP check
9. Updates marker to stage=3 (`sql_imported=true`, `migration_completed=true`)

Response:
```json
{
  "success": true,
  "data": {
    "message": "Stage 3 completed — SQL imported",
    "database": "example_com_db",
    "safety_backup_created": true,
    "wp_config_updated": true,
    "warnings": [],
    "errors": [],
    "status": {
      "files": "imported",
      "sql": "imported",
      "wp_config": "updated"
    }
  }
}
```

Rollback: if SQL import fails, the safety backup is restored. Marker stays at stage=2.

**CLI: `--upgrade`** — Upgrade existing site runtime (no backup required):

```bash
containercp migrate-vesta-site --domain <domain> --owner <owner> --upgrade
```

Checks and fixes:
1. Apache mod_rewrite (adds rewrite_module to 00-load-modules.conf)
2. Trusted proxy block in wp-config.php (BEGIN CONTAINERCP TRUSTED PROXY)
3. PHP syntax check with backup/restore on failure

### Migration state machine

| Marker stage | Possible actions |
|---|---|
| No marker / stage 0 | Create site |
| Stage 1 | Import files |
| Stage 2 | Import SQL |
| Stage 3 | Migration completed |

Inspect response includes `can_import_files`, `can_import_sql`, and `migration_completed` to guide the UI.

---

## 7. Related documents

- `docs/development/single-source-of-truth.md` — SSOT ownership rules
- `docs/runtime-architecture.md` — runtime subsystem architecture
- `docs/development/coding-rules.md` — development principles
- `AGENTS.md` — main AI entry point
- `CHANGELOG.md` — recent changes
