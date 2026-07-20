# API Rules

Before modifying or adding any API endpoint, read this document first.

---

## 1. API-first rule

Every operation must be available through the REST API before any other
interface.  Web UI and CLI are only clients of the API.

- No business logic in UI or CLI handlers.
- UI and CLI call the same API endpoints.
- API is the authoritative interface — test it directly.

---

## 2. API entry point

All REST API routes are implemented in:

- `libs/api/ApiServer.cpp` — route registration and handler lambdas
- `libs/api/ApiServer.h` — class declaration
- `libs/api/Router.h/.cpp` — prefix and exact-match routing
- `libs/api/Response.h` — HTTP response struct
- `libs/api/Request.h` — HTTP request struct
- `libs/api/JsonFormatter.h/.cpp` — JSON response builders

### Request flow

```
HTTP request
  → ApiServer::handle()           → dispatch to Router
    → Router::dispatch()           → match method + path
      → Route handler lambda       → validate, call services
        → Service / Manager layer  → business logic
          → Storage / Provider     → persistence / external
        ← result
      ← Response struct            → JSON body + status code
    ← serialized HTTP response
```

API handlers must be **thin** — they validate input, call the owning
subsystem, and format the response.  Business logic belongs in service
or manager classes.

---

## 3. API procedure for adding a new endpoint

1. **Check existing modules first** — search the codebase for similar
   functionality.  Never duplicate what already exists.

2. **Identify the owning subsystem** — every capability has one owner
   (see `docs/development/single-source-of-truth.md`).

3. **Add business logic to the owning layer** — the API handler must
   not contain business logic.  Add methods to the appropriate service,
   manager, or executor class first.

4. **Add/extend the REST endpoint** — register the route in
   `ApiServer.cpp` using `router_.add()` (exact match) or
   `router_.add_prefix()` (prefix match).  Keep the handler lambda
   short — delegate to the owning subsystem.

5. **Return a consistent JSON response** — use the standard format
   (see section 4 below).

6. **Update Web UI or CLI only after the API exists** — never build
   UI first.

7. **Add tests** — test the API endpoint through the owning
   subsystem's public API.  Add integration tests if possible.

8. **Update CHANGELOG.md** — every new or changed endpoint gets an
   entry.

---

## 4. Standard response format

### Success

```json
{
  "success": true,
  "data": { ... }
}
```

### Success with job (async)

```json
{
  "success": true,
  "data": {
    "job_id": 42,
    "status": "pending",
    "message": "Action queued"
  }
}
```

HTTP status: **202 Accepted** for async jobs.

### Error

```json
{
  "success": false,
  "error": "Human-readable error message"
}
```

HTTP status: appropriate 4xx or 5xx code.

### Error with code

Some endpoints return a machine-readable error code:

```json
{
  "success": false,
  "error": {
    "code": "NOT_FOUND",
    "message": "Site not found: example.com"
  }
}
```

### List responses

Arrays are wrapped in `data`:

```json
{
  "success": true,
  "data": [ ... ]
}
```

### Simple success (no data)

```json
{
  "success": true,
  "message": "Operation completed"
}
```

---

## 5. Error handling rules

- Use correct HTTP status codes:
  - `200` — success
  - `202` — accepted (async job)
  - `400` — bad request / invalid input
  - `404` — resource not found
  - `500` — internal server error
- Do not silently convert backend failures into success.
- Return useful error messages that help the caller understand what
  went wrong.
- Do not leak unsafe internal details (stack traces, Docker engine
  stderr as-is, database queries).
- Validate input before calling the owning subsystem — return clear
  validation errors.

---

## 6. Async / job-based operations

Long-running operations must use the JobExecutor:

1. Create a job via `s.jobs().create(type, steps)`.
2. Update job status to `"pending"`, progress `0`.
3. Submit a task to `s.job_executor().submit(job_id, lambda)`.
4. Inside the lambda, update job status to `"running"` and report
   progress.
5. On completion, set status to `"completed"` or `"failed"`.
6. Return `{ "success": true, "data": { "job_id": ..., "status": "pending" } }`
   with HTTP 202.

Jobs are suitable for:
- SSL certificate issuance / renewal
- Docker Compose restart actions
- Backup create / restore
- Site create / remove (currently synchronous, may move to async)

Do not block the API for long-running operations.  The UI should poll
the jobs API or display the job_id for user reference.

---

## 7. Single Source of Truth

The API must call the owning subsystem — never duplicate its logic.

- **SSL** — call `CertificateStore` and `SslCertificateManager`.
  Do not read `metadata.json` directly.
- **Runtime/Docker** — call `SiteRuntimeManager` and
  `RuntimeActionExecutor`.  Do not run `docker` commands from the API
  handler.
- **Backups** — call `BackupManager` and `BackupProvider`.
- **Databases** — call `DatabaseViewService` for read-only inventory/detail
  views and `DatabaseManager` only for database metadata lifecycle operations.
- **Proxy** — call `ReverseProxyManager` and `ProxyProvider`.

API handlers should be **thin dispatchers** — validate, delegate,
format.  No business logic, no direct I/O, no Docker commands.

---

## 8. Current API groups

| Group | Prefix | Key actions | Owner subsystem |
|-------|--------|-------------|-----------------|
| Sites | `GET /api/sites` | list, create, remove | `SiteManager` |
| Runtime | `GET /api/runtime/<id>` | status | `SiteRuntimeManager` |
| Runtime | `POST /api/runtime/<id>/<action>` | restart-web, restart-php, restart-db, restart-redis, restart-all | `RuntimeActionExecutor` |
| SSL | `GET /api/ssl` | list all, per-domain details | `CertificateStore` |
| SSL | `POST /api/ssl/<domain>/<action>` | issue, renew, enable, disable, redirect | `CertificateProvider`, `ProxyProvider` |
| Jobs | `GET /api/jobs` | list, status | `JobManager` |
| Settings | `GET /api/settings`, `POST /api/settings` | hostname, config | `Config` |
| Backups | `GET /api/backups`, `POST /api/backups/create\|remove\|restore` | manage backups | `BackupManager` |
| Users | `GET /api/users` | user management | `UserManager` |
| Domains | `GET /api/domains` | domain listing | `DomainManager` |
| Proxy | `GET /api/proxy` | proxy listing | `ReverseProxyManager` |
| Auth | `POST /api/auth/login\|logout\|check` | session auth | `AuthService` |
| Health | `GET /api/health` | daemon health | — |
| Databases | `GET /api/databases`, `GET /api/databases/<id>` | read-only enriched database inventory | `DatabaseViewService` |
| WordPress | `GET /api/wordpress/database-credentials/status`, `POST /api/wordpress/database-credentials/rotate` | credential status, queue DB credential rotation | `WordPressConfigService`, `DatabaseCredentialRotationJobService` |
| Access | `GET /api/access-users` | SFTP access users | `AccessUserManager` |

---

## 9. Debugging UI API failures

When a page shows "Failed to load" or similar errors:

1. **Check the browser Network response body first** — open DevTools →
   Network tab → find the failing request → inspect the Response tab.
2. **HTTP 200 does not mean the frontend parsed successfully.**
   A 200 with malformed JSON will fail at `response.json()` in the
   frontend, triggering the catch block.
3. **Validate generated JSON before investigating proxy/routing.**
   Proxy, auth, and routing issues typically produce HTTP 4xx/5xx or
   connection errors.  A 200 with invalid JSON is almost always a
   backend serialization bug.
4. **Avoid manual JSON string concatenation where possible.**
   Use `JsonFormatter`, helper functions, or DTO-based serialization.
   If manual concatenation is unavoidable, add regression tests for:
   - empty strings
   - quoted strings (`"`)
   - commas and backslashes
   - URLs and special characters
   - missing optional fields

## 10. Required rule for future agents

> **Before modifying or adding any API endpoint, read this document first.**

## 11. Related documents

- `docs/api/API_REFERENCE.md` — authoritative endpoint index
- `docs/development/single-source-of-truth.md` — SSOT ownership rules
- `docs/runtime-architecture.md` — runtime subsystem
- `AGENTS.md` — main AI entry point
