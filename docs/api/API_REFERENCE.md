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

**GET /api/sites** — returns enriched site list. The admin-panel (site_id=0)
is synthesized as a virtual system site when `server_hostname` is configured.
Admin site fields: `system_role: "admin-panel"`, `proxy_upstream`, `web_status`,
`php_status: "N/A"`, `ssl_status`, `can_delete: false`, `can_manage_runtime: false`.
Normal sites are returned unchanged by `JsonFormatter::site()`.

**POST /api/sites/create** — body: `{"owner":"...","domain":"...","profile":"..."}`

**POST /api/sites/remove** — body: `{"domain":"..."}`. Returns 403 if domain equals
`server_hostname` (admin-panel system site cannot be removed).

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
(`primary`|`alias`|`redirect`|`wildcard`|`system`), `site_id`, `site_name`,
`site_domain`, `target`, `ssl_enabled`, `ssl_status`, `enabled`.

**System domain (site_id=0):** The admin-panel domain (`server_hostname`) is
synthesized by `DomainViewService` when no Domain record exists for the server
hostname. Additional fields: `system_role` (`"admin-panel"`), `proxy_upstream`,
`can_delete`, `can_manage_runtime`, `can_manage_ssl`, `can_manage_proxy`.
Normal domains receive `can_delete: true`, `can_manage_runtime: true`.

**SSL field semantics:**
- `ssl_status`: Display-friendly certificate status from `CertificateStore::https_display_status()`.
  Possible values: `Active`, `Expiring`, `Expired`, `Error`, `Issuing`, `Disabled`, `HTTP_ONLY`.
  This is the **single source of truth** for the certificate state.
- `ssl_enabled`: Boolean derived from `CertificateStore` metadata (`https_enabled` field).
  Previously derived from `Domain::ssl_enabled` (legacy field that could be out of sync).
  Now consistent with `ssl_status`: `true` when a valid certificate has HTTPS enabled.
  
  **Note:** When `ssl_status` is `"Active"` or `"Expiring"`, both `ssl_enabled` and `ssl_status`
  indicate a working HTTPS configuration. The two fields are always consistent — both
  sourced from `CertificateStore` metadata.

**New fields (v0.6):** `mail_domain_id` (0 if no mail domain),
`mail_domain_mode` (e.g. `"local-primary"` or empty), `dkim_generated`
(bool), `dkim_selector` (e.g. `"dkim"`), `dkim_public_key_dns` (the
TXT record value or empty).

The API handler enriches domain records with site info from
`SiteManager::find_by_id()`, SSL status from
`CertificateStore::load_metadata()` + `https_display_status()`,
and mail domain info from `MailDomainManager::find_by_domain()`.
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

### 2.11a WordPress Database Credentials

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/wordpress/database-credentials/status?site_id=N` | Public-safe WordPress credential status and backend-resolved database target | `WordPressDatabaseCredentialResolver` |
| POST | `/api/wordpress/database-credentials/rotate` | Queue WordPress database credential rotation | `DatabaseCredentialRotationJobService` |

**GET /api/wordpress/database-credentials/status?site_id=N** — returns public-safe credential status for Site Details UI:

```json
{
  "success": true,
  "data": {
    "available": true,
    "site_id": 1,
    "domain": "example.com",
    "status": "complete",
    "source": "direct_constant",
    "mutability": "mutable_direct_constant",
    "db_name": "wp_example",
    "db_user": "wp_user",
    "db_host": "mariadb",
    "db_password_present": true,
    "database_target_available": true,
    "database_id": 1,
    "database_target_status": "resolved",
    "database_target_message": "WordPress database credential target resolved",
    "issues": []
  }
}
```

The response never includes raw `DB_PASSWORD`, generated credentials, config paths, site roots, document roots, command output, or provider diagnostics. Rotation clients must use the returned `database_id`; the server rejects rotate requests whose `database_id` does not match the backend-resolved WordPress credential target.

**POST /api/wordpress/database-credentials/rotate** — body:

```json
{
  "site_id": 1,
  "database_id": 1,
  "confirmation": "example.com"
}
```

Returns HTTP `202 Accepted` with a job id only:

```json
{
  "success": true,
  "data": {
    "job_id": 42,
    "status": "pending",
    "message": "Credential rotation queued"
  }
}
```

The request never accepts current or replacement passwords. The response never includes `DB_PASSWORD`, generated credentials, config paths, command output, or provider diagnostics.

Operational notes:

- `confirmation` must exactly match the target site domain.
- The endpoint is API-first; CLI and Web UI clients delegate to this queueing boundary.
- Current v0.8 foundation builds queue jobs but fail closed until live rotation dependencies are explicitly wired and validated.
- A successful future rotation must verify MariaDB access with the new password, WordPress/PHP database access, runtime container availability, and metadata persistence before reporting completion. HTTP/application health validation remains a separate live-validation requirement.
- Post-mutation failures must compensate or report `manual_recovery_required`; partial rotation must never be reported as success.

See `docs/development/wordpress-credential-management.md` for supported config forms, threat model, operator workflow, and residual risks.

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

### 2.22 Site Mail — Enable, Disable, Status

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| POST | `/api/sites/<id>/enable-mail` | Enable mail for a site | `SiteMailOrchestrator` |
| POST | `/api/sites/<id>/disable-mail` | Disable mail for a site | `SiteMailOrchestrator` |
| GET | `/api/sites/<id>/mail-status` | Get mail status for a site | `SiteMailOrchestrator` |
| POST | `/api/sites/<id>/send-test-email` | Send a test email via msmtp in PHP container | `CommandExecutor` |

**POST /api/sites/<id>/enable-mail**

Creates per-site mail configuration:
- Dovecot SASL credentials in `/etc/dovecot/passwd-php` (separate from mailbox passwd)
- `msmtprc` with fixed envelope sender (`wordpress@domain`, `allow_from_override off`)
- Postfix `sender_login` exact-match entry (`wordpress@domain → site-{ID}@php.containercp.internal`)
- Site's `docker-compose.yml` updated: adds `containercp-mail` to PHP service networks + top-level networks section
- `docker compose up -d --force-recreate php` applied to activate volume mount + network
- PHP container connected to `containercp-mail` Docker network (runtime)
- Postfix + Dovecot reload to activate changes
- Sets `site.php_mail_enabled = true`

**Prerequisite:** A `MailDomain` must exist for the site's domain (created via `POST /api/mail/domains`).
PHP Mail does NOT create a MailDomain automatically.

After enable-mail, the `containercp-mail` network is persistent in compose. Any `docker compose up -d` or container recreate will automatically reconnect the PHP container to the mail network.

Idempotent — safe to call multiple times. No site files (WordPress, DB, proxy, SSL) are modified.

**Error responses:**
- 400 — `{"error":"mail_domain_missing","message":"Mail Domain not found. Create one via Mail → Domains first."}`
- 404 — Site not found
- 500 — Compose file update failed (malformed YAML, write error, docker compose up failure)

**Response:**
```json
{ "success": true, "data": { "message": "Mail enabled for site example.com" } }
```

**POST /api/sites/<id>/disable-mail**

Removes per-site mail configuration:
- Removes SASL credentials for EXACTLY this `site_id` from Dovecot `passwd-php` (other sites' credentials untouched)
- Removes Postfix `sender_login` entry for this `site_id`
- Deletes `msmtprc` file from site's `config/php/`
- Removes `containercp-mail` from site's `docker-compose.yml`
- Applies `docker compose up -d` to deactivate the network
- Disconnects PHP container from `containercp-mail` network (runtime)
- Sets `site.php_mail_enabled = false`

**Does NOT modify:** MailDomain, DKIM, mailboxes, aliases, DNS config, SPF, DMARC.
These are independent of PHP Mail.

**Response:**
```json
{ "success": true, "data": { "message": "Mail disabled for site example.com" } }
```

**Error responses:**
- 400 — Invalid site ID (non-numeric)
- 404 — Site not found
- 500 — Compose file update failed

**POST /api/sites/<id>/send-test-email**

Tests the **msmtp → Postfix submission** chain (NOT PHP `mail()` or `wp_mail()` directly).
Sends a pre-formed email via `msmtp` inside the PHP container using the site's SMTP credentials.

Optional body: `{"to": "admin@example.com"}` (defaults to `admin@<site-domain>`).

**What this tests:**
- msmtp can resolve `containercp-mail-postfix` via Docker DNS
- TLS connection to Postfix submission (port 587) succeeds
- SASL authentication with the site's credentials works
- Postfix accepts the message into its queue
- Sender restrictions (`reject_sender_login_mismatch`) pass

**What this does NOT test:**
- PHP `mail()` function itself (need a PHP script for that)
- WordPress `wp_mail()` (need WordPress for that)
- Final email delivery (Postfix will attempt delivery based on its config)

**Security:** Recipient email is validated (alphanumeric, `@`, `.`, `-`, `_`, `+` only)
and passed as a separate argument (not via shell string) to prevent command injection.
Email content is written to a temp file and piped via `run_with_stdin_file`.

**Response:**
```json
{ "success": true, "data": { "message": "Test email sent to admin@example.com" } }
```

**Error responses:**
- 400 — Invalid recipient email address
- 404 — Site not found
- 500 — PHP container not found, msmtp not installed, or email send failed

**GET /api/sites/<id>/mail-status**

Returns current PHP Mail state for a site.

**Response:**
```json
{
  "success": true,
  "data": {
    "site_id": 42,
    "domain": "example.com",
    "enabled": true,
    "mail_domain": true,
    "credential_exists": true,
    "msmtprc": true,
    "network": true
  }
}
```

Fields:
- `enabled` — `Site.php_mail_enabled` (independent of MailDomain)
- `mail_domain` — `MailDomain` record exists for this domain (informational, created via Mail → Domains)
- `credential_exists` — Dovecot `passwd-php` has entry for this site
- `msmtprc` — `config/php/msmtprc` file exists on disk
- `network` — PHP container is connected to `containercp-mail` network

**Error responses:**
- 400 — Invalid site ID (non-numeric)
- 404 — Site not found
- 500 — Orchestration error (e.g., Docker network not available, mail module inactive)

---

### 2.23 DNS Check

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/domains/<domain>/dns-check` | Live DNS resolution for a domain | `DnsCheckService` (`libs/dns/`) |

Performs live DNS resolution using the **c-ares** library (no shell commands, no dig). Returns structured results with per-type status, all found records, and SOA data.

**Path parameter:** The `<domain>` parameter is a DNS owner name (FQDN), which may include
underscore labels used in service records. Examples of valid values:
- `example.com` — apex domain
- `_dmarc.example.com` — DMARC report record
- `dkim._domainkey.example.com` — DKIM public key record
- `_mta-sts.example.com` — MTA-STS policy record
- `_smtp._tls.example.com` — TLS reporting record

The endpoint uses `validate_dns_name()` which allows underscore but still rejects shell
characters, spaces, path separators, and invalid DNS labels. This is separate from
`validate_domain()` which is stricter (used for ContainerCP Domain resources).

**Query parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `types` | string | `A,AAAA,MX,TXT,NS,SOA,CAA` | Comma-separated list of record types to query. Supported: `A`, `AAAA`, `MX`, `TXT`, `CNAME`, `NS`, `SOA`, `CAA`. |
| `refresh` | boolean | `false` | If `1`, bypasses the 60-second cache and performs a fresh DNS lookup. |

**Success response (complete):**

```json
{
  "success": true,
  "data": {
    "domain": "example.com",
    "resolved_at": "2026-07-15T12:00:00Z",
    "cached": false,
    "overall_status": "complete",
    "per_type": [
      {
        "type": "A",
        "status_code": "NOERROR",
        "error": "",
        "records": [
          {
            "type": "A",
            "name": "example.com",
            "value": "192.168.1.1",
            "ttl": 3600,
            "priority": 0,
            "dns_response_details": "A 192.168.1.1 (ttl=3600)"
          }
        ]
      },
      {
        "type": "MX",
        "status_code": "NOERROR",
        "error": "",
        "records": [
          {
            "type": "MX",
            "name": "example.com",
            "value": "mail.example.com",
            "ttl": 3600,
            "priority": 10,
            "dns_response_details": "MX 10 mail.example.com (ttl=3600)"
          }
        ]
      }
    ],
    "expected_ipv4": "116.202.231.94",
    "expected_ipv6": "",
    "expected_ipv4_source": "external_dns",
    "expected_ipv6_source": "",
    "expected_ip_detected_at": "2026-07-15T12:00:00Z",
    "expected_ip_stale": false,
    "soa": {
      "mname": "ns1.example.com",
      "rname": "admin.example.com",
      "serial": 2026071501
    },
    "error": ""
  }
}
```

**Success response (partial — some record types failed):**

```json
{
  "success": true,
  "data": {
    "domain": "example.com",
    "resolved_at": "2026-07-15T12:00:00Z",
    "cached": false,
    "overall_status": "partial",
    "per_type": [
      {"type": "A", "status_code": "NOERROR", "error": "", "records": [...]},
      {"type": "AAAA", "status_code": "TIMEOUT", "error": "TIMEOUT", "records": []}
    ],
    "soa": {"mname": "", "rname": "", "serial": 0},
    "error": "TIMEOUT"
  }
}
```

**NXDOMAIN response (valid DNS diagnostic result, HTTP 200):**

```json
{
  "success": true,
  "data": {
    "domain": "nonexistent.example.com",
    "resolved_at": "2026-07-15T12:00:00Z",
    "cached": false,
    "overall_status": "complete",
    "per_type": [
      {"type": "A", "status_code": "NXDOMAIN", "error": "NXDOMAIN", "records": []}
    ],
    "soa": {"mname": "", "rname": "", "serial": 0},
    "error": ""
  }
}
```

**Important:** NXDOMAIN and NODATA are valid DNS diagnostic results, NOT transport errors. They return:
- HTTP 200 (not 502)
- `success: true` (the diagnostic completed successfully)
- `overall_status: "complete"` (the DNS response was valid)
- `per_type[].status_code: "NXDOMAIN"` (specific result)

This allows the frontend to display NXDOMAIN with structured evidence and recommendations, rather than treating it as a transport failure.

**Resolver failure (HTTP 502):**

```json
{
  "success": false,
  "data": {
    "domain": "example.com",
    "resolved_at": "2026-07-15T12:00:00Z",
    "cached": false,
    "overall_status": "failed",
    "per_type": [
      {"type": "A", "status_code": "SERVFAIL", "error": "SERVFAIL", "records": []}
    ],
    "soa": {"mname": "", "rname": "", "serial": 0},
    "error": "SERVFAIL"
  }
}
```

**HTTP status codes:**

| Code | Condition |
|------|-----------|
| 200 | All valid DNS diagnostic results: NOERROR, NODATA, NXDOMAIN, or partial results (some valid, some resolver failures). NXDOMAIN is a valid DNS response, NOT a transport error. |
| 400 | Invalid domain format, unsupported DNS record type, invalid query parameters |
| 502 | All requested record types returned resolver failures (SERVFAIL, TIMEOUT, ERROR) — no useful DNS data available. Only applies when `success=false`. |

**overall_status values:**

| Value | Meaning |
|-------|---------|
| `complete` | All requested record types returned valid DNS responses (NOERROR, NODATA, or NXDOMAIN — all are valid diagnostic results) |
| `partial` | Some record types returned valid responses, some had resolver failures (SERVFAIL, TIMEOUT, ERROR) |
| `failed` | All requested record types had resolver failures — no diagnostic data available |

**per_type[].status_code values:**

| Value | Category | Meaning |
|-------|----------|---------|
| `NOERROR` | Valid | Query succeeded, records may be present |
| `NODATA` | Valid | Domain exists but has no records of this type |
| `NXDOMAIN` | Valid | Domain does not exist — valid DNS response, not a failure |
| `SERVFAIL` | Resolver failure | DNS server failure — no useful data |
| `TIMEOUT` | Resolver failure | Query timed out — no useful data |
| `ERROR` | Resolver failure | Other DNS error — no useful data |

**`success` field semantics:**

| Value | Meaning |
|-------|---------|
| `true` | DNS diagnostic completed successfully. Data includes valid responses (NOERROR, NODATA, NXDOMAIN) even if some types had resolver failures (partial). The frontend can display `per_type` results with evidence. |
| `false` | DNS diagnostic failed entirely — all queried types returned resolver failures (SERVFAIL, TIMEOUT, ERROR). No useful DNS data available. |

**Expected IP fields:**

| Field | Description |
|-------|-------------|
| `expected_ipv4` | Auto-detected public IPv4 of the server (from `NetworkService`). Empty string if undetermined. |
| `expected_ipv6` | Auto-detected public IPv6 of the server. Empty string if unavailable or not configured. |
| `expected_ipv4_source` | Detection source: `routing_table`, `external_dns`, `hostname_dns_fallback`, or empty. |
| `expected_ipv6_source` | Same as above for IPv6. |
| `expected_ip_detected_at` | ISO 8601 timestamp of when detection was last performed. |
| `expected_ip_stale` | `true` if the value is from a cache that has exceeded its 5-minute TTL but was loaded from persisted storage. |

**SPF analysis fields:**

| Field | Description |
|-------|-------------|
| `spf_analysis.status` | Overall status: `"ok"`, `"error"`, `"not_found"` |
| `spf_analysis.match` | Match result: `"match"`, `"mismatch"`, `"not_published"`, `"error"` |
| `spf_analysis.expected_ip_allowed` | Boolean — whether the server's public IPv4/IPv6 is allowed by the SPF record |
| `spf_analysis.record` | Raw SPF record text (e.g. `v=spf1 ip4:116.202.231.94 -all`) |
| `spf_analysis.all_qualifier` | All qualifier: `"-"`, `"~"`, `"?"`, `"+"` |
| `spf_analysis.lookup_count` | Number of DNS lookups performed during SPF evaluation |
| `spf_analysis.mechanism_matched` | The mechanism that matched the expected IP (e.g. `ip4:116.202.231.94`) |
| `spf_analysis.errors[]` | Array of error strings (syntax, loop, lookup limit, duplicate) |
| `spf_analysis.warnings[]` | Array of warning strings (softfail, neutral, lookup count near limit) |
| `spf_analysis.checks[]` | Array of check objects: `{code, status, reason}` |

**SPF match semantics:**

| `match` value | Meaning |
|---------------|---------|
| `"match"` | SPF record exists, syntax valid, expected sender IP is allowed |
| `"mismatch"` | SPF record exists, expected sender IP is NOT allowed |
| `"not_published"` | No SPF record found |
| `"error"` | SPF syntax error, duplicate records, include loop, lookup limit exceeded |

The SPF analysis is performed by `SpfAnalyzer` (`libs/dns/SpfAnalyzer.h/.cpp`) which implements
RFC 7208 semantics. Supported mechanisms: `ip4`, `ip6`, `a`, `mx`, `include`, `redirect`, `all`.
The analyzer uses c-ares (via `DnsCheckService`) for DNS resolution — no shell commands or dig.

**Example JSON with SPF analysis:**
```json
{
  "success": true,
  "data": {
    "domain": "maillab.softi.co",
    "expected_ipv4": "116.202.231.94",
    "spf_analysis": {
      "status": "ok",
      "match": "match",
      "expected_ip_allowed": true,
      "record": "v=spf1 ip4:116.202.231.94 -all",
      "all_qualifier": "-",
      "lookup_count": 1,
      "mechanism_matched": "ip4:116.202.231.94",
      "errors": [],
      "warnings": [],
      "checks": [
        {"code": "spf_syntax", "status": "ok", "reason": "SPF syntax is valid"},
        {"code": "spf_expected_ip", "status": "ok", "reason": "Expected IP is allowed by ip4:116.202.231.94"}
      ]
    },
    "per_type": []
  }
}
```

These fields are populated by `NetworkService` (auto-detection, NOT user-editable). Detection order:
1. System routing table (`ip route get`)
2. External DNS helper (`myip.opendns.com` via c-ares)
3. Server hostname DNS resolution (least preferred — circular dependency risk)
4. Fallback to stored cached value
5. Empty string (unknown)

The frontend uses `expected_ipv4` and `expected_ipv6` to compare against the domain's published
A/AAAA records, showing Match / Mismatch / Not Published / Unknown status.

**Implementation:** Uses `DnsCheckService` from `libs/dns/` and `NetworkService` from `libs/network/`. The DNS resolution uses c-ares (`ares_query_dnsrec`) with a synchronous event loop. Results are cached in-memory for 60 seconds.

**Cache semantics:**
- First query for a domain → live lookup → `"cached": false`
- Repeated query within 60s → cache hit → `"cached": true`
- `refresh=1` → clears cache → live lookup → `"cached": false`
- Domain is normalized to lowercase before all cache operations (`GOOGLE.COM` → `google.com`)
- Cache key includes sorted record types, so different type combinations are cached independently

**Example:**

```bash
# Basic check
curl /api/domains/example.com/dns-check

# Check specific record types
curl "/api/domains/example.com/dns-check?types=A,AAAA,MX"

# Force refresh (bypass cache)
curl "/api/domains/example.com/dns-check?refresh=1"
```

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
