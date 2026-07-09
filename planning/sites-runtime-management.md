# Sites Runtime Management

## Objective

Add runtime health status and control for site containers
(web, PHP) to ContainerCP's Sites module, without turning
it into a generic Docker management page.

## Principles

1. **Sites manages only web runtime** — not SSL, databases,
   Redis, or backups.
2. **Every site has one Docker Compose project** — the compose
   project registered for the site is always the source of truth.
3. **No global Docker container scanning** — all operations
   target the site's specific compose project.
4. **Admin-friendly, not Docker-internals** — display statuses
   like "Running", "Stopped", "Unhealthy", not container IDs.

## Architecture

```
Site
  └── Compose project (site-N)
        ├── web  (Apache/Nginx)
        ├── php  (PHP-FPM)
        ├── db   (MariaDB — managed by Databases module)
        └── redis (future Redis module)

Sites module manages:
  - web container status + control
  - php container status + control
  - per-site HTTPS status (read-only from SSL metadata)

Databases module manages:
  - db container status + control (future)

Redis module (future):
  - redis container status + control (future)
```

## Phase 1 — Read-only runtime information

### Backend
- Add `RuntimeManager` or extend existing runtime to provide
  per-container status for a site:
  - `site_runtime_status(site_id)` → returns web/php statuses
  - Uses `docker compose --project-name site-<id> ps --format json`
  - Maps Docker statuses to: Running, Stopped, Unhealthy, Unknown
- Add `GET /api/sites/<id>/runtime` endpoint
- Each site response in `GET /api/sites` includes runtime summary

### Frontend
- Sites table: add columns "Web", "PHP", "HTTPS" with status badges
- Colors: Running=green, Stopped=red, Unhealthy=yellow, Unknown=gray
- No action buttons yet

## Phase 2 — Backend architecture for future actions

### Backend
- Add `RuntimeAction` concept: restart, stop, start per container
- Route: `POST /api/sites/<id>/runtime/<action>`
  where action = `restart-web`, `restart-php`, `restart-all`
- Implementation is stub (returns success) — no actual Docker exec
- Architecture prepared but not activated from UI

## Phase 3 — Restart runtime

### Backend
- Implement actual `docker compose restart` for:
  - `restart-web`: `docker compose --project-name site-<id> restart web`
  - `restart-php`: `docker compose --project-name site-<id> restart php`
  - `restart-all`: restarts both web and php
- Never restart db or redis from Sites module
- Execute via async Job (reuse JobExecutor)
- Return job_id for progress tracking

### Frontend
- Add "Restart Web", "Restart PHP", "Restart" buttons on site detail
- Confirmation dialog before restart
- Job progress indicator

## Phase 4 — Runtime details

### Backend
- Extend runtime status to include:
  - Container name
  - Health status
  - Uptime
  - Image
  - Restart count (optional)
- New endpoint: `GET /api/sites/<id>/runtime/containers`

### Frontend
- Site detail page: new "Runtime" tab or section
- Show web + PHP containers with metadata
- No raw Docker output — formatted admin-friendly table

## Phase 5 — Future-ready architecture

Prepare for future operations without redesign:
- Recreate container
- Pull latest image
- Rolling restart
- Health diagnostics
- These operations are NOT implemented now — only the
  routing and interface structure supports them.

## Non-goals (explicitly excluded)

- Database container management (belongs to Databases module)
- Redis container management (future module)
- SSL renewal (belongs to SSL module)
- Global Docker container listing
- Container logs streaming
- Container exec/shell access

## Files to create/modify

### Phase 1
- `libs/runtime/SiteRuntimeManager.h/.cpp` — per-site container status
- `libs/api/ApiServer.cpp` — GET /api/sites/<id>/runtime endpoint
- `libs/core/ServiceRegistry.h/.cpp` — wire SiteRuntimeManager
- `web/app.js` — status columns on Sites page
- `tests/test_runtime.cpp` — unit tests

### Phase 2
- `libs/api/ApiServer.cpp` — POST endpoint (stub)
- No UI changes yet

### Phase 3
- `libs/api/ApiServer.cpp` — real docker compose exec
- `web/app.js` — restart buttons + job polling

### Phase 4
- `libs/api/ApiServer.cpp` — detailed container endpoint
- `web/app.js` — runtime details section

## API design

```
GET  /api/sites/<id>/runtime
  → { "web": "running", "php": "running", "https": "valid" }

GET  /api/sites/<id>/runtime/containers
  → [ { "name": "site-4-web", "status": "running",
         "health": "healthy", "uptime": "2h", "image": "nginx:alpine" },
       { "name": "site-4-php", ... } ]

POST /api/sites/<id>/runtime/restart-web     → { "job_id": ... }
POST /api/sites/<id>/runtime/restart-php     → { "job_id": ... }
POST /api/sites/<id>/runtime/restart-all     → { "job_id": ... }
```

## Status mapping

| Docker status | Display status |
|--------------|----------------|
| running | Running |
| exited | Stopped |
| paused | Stopped |
| restarting | Starting |
| unhealthy | Unhealthy |
| (any other) | Unknown |

## HTTPS status mapping

| SSL metadata state | Display |
|-------------------|---------|
| active + https_enabled | Valid |
| active + !https_enabled | Disabled |
| expired | Expired |
| error | Error |
| http_only / no metadata | Disabled |
