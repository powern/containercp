# Epic: Complete Web Administration Panel

Status: Completed

**Goal:** Transform the Web UI from read-only dashboard into a
fully interactive administration panel where users can deploy and
manage websites entirely from the browser.

## Architecture-first approach

All operations go through REST API → Daemon → Service Layer.
The Web UI never talks directly to managers or storage.

## Scope

- REST API CRUD endpoints for site operations
- Background job tracking system
- Real backend calls in Web UI (replace mock data)
- Site creation wizard with deployment progress
- Resource detail pages with real data

## Affected subsystems

- REST API (libs/api/) — new POST/DELETE endpoints
- Jobs (libs/jobs/) — new job tracking subsystem
- Web UI (web/) — forms, wizards, progress display
- Daemon (libs/daemon/) — job execution

## Task breakdown

### API layer

API-001: POST /api/sites/create — create site via backend
API-002: POST /api/sites/remove — remove site via backend
API-003: POST /api/backups/create — create backup via backend
API-004: GET /api/jobs — list background jobs
API-005: GET /api/jobs/<id> — get job status

### Job system

JOB-001: In-memory job tracking with status, progress, steps

### Web UI

UI-001: Site creation wizard with progress display
UI-002: Site detail page with real data and tabs
UI-003: Backups page with create/restore actions
UI-004: Global navigation improvements

## Architecture impact

- Adds POST method support to Router
- Adds JSON body parsing to Request
- Introduces Job resource (not persisted, in-memory only)
- Establishes pattern for future async operations
