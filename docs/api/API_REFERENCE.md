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
| GET | `/api/domains` | List all domains | `DomainManager` |
| POST | `/api/domains/remove` | Remove a domain record | `DomainManager` |

### 2.10 Proxy

| Method | Path | Purpose | Owner |
|--------|------|---------|-------|
| GET | `/api/proxy` | List proxy configs | `ReverseProxyManager` |
| POST | `/api/proxy/remove` | Remove a proxy entry | `ReverseProxyManager` |

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

### 2.15 Logs

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

API handlers must never:
- Run `docker` commands directly
- Read `metadata.json` or other data files directly
- Duplicate business logic from the owning subsystem
- Make assumptions about compose service names (use `ServiceRole`)

---

## 7. Related documents

- `docs/development/single-source-of-truth.md` — SSOT ownership rules
- `docs/runtime-architecture.md` — runtime subsystem architecture
- `docs/development/coding-rules.md` — development principles
- `AGENTS.md` — main AI entry point
- `CHANGELOG.md` — recent changes
