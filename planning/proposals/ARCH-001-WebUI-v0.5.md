# ARCH-001: Complete Web UI for Version 0.5

Status: Implemented

## Problem

ContainerCP's Web UI is functional but incomplete. Administrators
cannot perform the full site management workflow from the browser.
Key gaps:

- Site creation shows a modal but no deployment progress
- Resource detail pages show placeholder content for most tabs
- No edit/delete operations from the UI
- Dashboard lacks monitoring data
- No client-side form validation

## Motivation

The Product Vision defines v1.0 success as: "The administrator can
create a website, point a domain, provision SSL, create an SFTP user,
and upload files — all from the Web UI." Without completing the Web UI,
the product cannot be validated or released.

Which Product Vision goal it supports: Version 1.0 readiness.

## Current Architecture

The Web UI is a single-page application served by `containercpd`'s
built-in HTTP server. It uses vanilla HTML/CSS/JS with no frameworks.
The REST API provides GET endpoints for all resources and POST
endpoints for site create/remove and backup create.

The site creation modal collects owner and domain, then calls
`POST /api/sites/create`. The API responds synchronously after the
entire creation process completes.

## Proposed Architecture

### Site Creation Wizard

Replace the single-modal site creation with a multi-step wizard:

1. **Owner step** — select or create owner user
2. **Domain step** — enter domain with validation
3. **Template step** — select web server template (from profiles API)
4. **Review step** — summary of selections
5. **Deploy step** — call API, show progress via job polling

The wizard is a single-page overlay with steps, not a multi-page flow.

### Deployment Progress

After calling `POST /api/sites/create`, the UI polls
`GET /api/jobs/<id>` every second and displays:

- Progress bar with percentage
- Current step description
- Status indicator (running/completed/failed)

### Resource Detail Pages

Each resource page (sites, domains, databases, SSL, proxy, access)
gets a detail panel that opens when clicking a row. The detail panel
shows:

- All resource fields
- Related resources (e.g., site detail shows domains, databases,
  SSL, proxy, backups)
- Action buttons (edit, delete, enable/disable)

### CRUD Operations

Add backend DELETE endpoints for resources that are missing them:

- `POST /api/domains/remove`
- `POST /api/databases/remove`
- `POST /api/ssl/remove`
- `POST /api/proxy/remove`
- `POST /api/access-users/remove`

Enable/disable operations:
- `POST /api/ssl/enable`
- `POST /api/ssl/disable`

### Dashboard Improvements

Add to the dashboard:
- Recent jobs list with status
- System health with more detail
- Quick action buttons

### Form Validation

Add client-side validation to all forms:
- Domain format validation
- Username/owner validation
- Required field checks
- Inline error messages

## New Resources

None. All resources already exist.

## Managers

No new managers. Existing managers already support all operations
through the daemon.

## Storage

No changes to storage.

## Providers

No new providers.

## REST API

### New endpoints

```
POST /api/domains/remove    — remove domain by fqdn
POST /api/databases/remove  — remove database by name
POST /api/ssl/remove        — remove SSL certificate
POST /api/proxy/remove      — remove proxy config
POST /api/access-users/remove — remove access user
POST /api/ssl/enable        — enable SSL cert
POST /api/ssl/disable       — disable SSL cert
```

### Modified endpoints

```
POST /api/sites/create  — returns job_id for progress tracking
GET  /api/jobs/<id>      — detailed job progress with steps
```

## Web UI

### New files

None. All changes are within existing `web/app.js`.

### Modified pages

- Dashboard — recent jobs, health details
- Sites — create wizard, detail panel, delete action
- Domains — detail panel, delete action
- Databases — detail panel, delete action
- SSL — enable/disable/delete actions, detail panel
- Proxy — delete action, detail panel
- Access — delete action, detail panel
- Backups — restore action

## CLI

No CLI changes needed. All operations are already available through
the daemon protocol.

## Configuration

No configuration changes.

## Migration Strategy

No migration needed. All existing data remains compatible.

## Backward Compatibility

All existing API endpoints continue working. The new endpoints do
not change existing behaviour.

## Rejected Alternatives

- **Using a frontend framework (React/Vue)** — rejected because it
  would add build tooling, npm dependencies, and break the zero-dep
  requirement. Vanilla JS is sufficient.

- **Server-side rendering** — rejected because the API-first
  architecture intentionally separates presentation from logic.

- **WebSocket for progress** — rejected because HTTP polling is
  simpler and sufficient for the MVP. WebSockets can be added later.

## Risks

- The single-page app may become large. Mitigated by keeping each
  page function focused and under 50 lines.

- Polling `/api/jobs` every second could add load. Mitigated by the
  small expected number of concurrent jobs.

## Validation Plan

1. Manual walkthrough of the full site creation wizard
2. Verify progress display updates correctly
3. Verify each CRUD endpoint with curl
4. Run existing test suite to confirm no regressions
5. Verify detail pages show correct data for all resource types
