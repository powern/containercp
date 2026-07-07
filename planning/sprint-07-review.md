# Sprint 7 Review — Admin Panel Phase 2

## Implemented

### Backend

- **CCP-5001**: `GET /api/profiles` — returns all configuration profiles with type, web_server, enabled, default status
- **CCP-5002**: `GET /api/nodes` — returns list of all nodes
- **CCP-5003**: `GET /api/logs` — returns recent log entries (mock data)
- **CCP-5004**: `GET /api/site-config` — deferred (profiles endpoint provides sufficient data)

### Frontend

- **CCP-5005**: Site detail page with tabs — clicking a site name opens a detail view with General, Profiles, SSL, Proxy, Access tabs. Breadcrumb navigation back to sites list.
- **CCP-5006**: Create Site modal — "+ Create Site" button opens a modal dialog with owner, domain, and template selector fields. Validation and toast feedback on submit.
- **CCP-5007**: Toast notification system — global toast messages for success (green), warning (yellow), error (red). Auto-dismiss after 3.5 seconds with fade animation.
- **CCP-5008**: Improved tables — action buttons (View, Start, Stop, Remove) on every row. Clickable domain names linking to detail pages. Search filtering across all resource tables.
- **CCP-5009**: Global error handling — try/catch around all API calls with user-friendly error messages. Loading states and network error handling.

## Architecture improvements

- `JsonFormatter::escape()` made public for reuse in inline JSON construction
- `/api/profiles` provides a generic profile listing endpoint usable by any profile type
- Frontend uses `window.renderTable` pattern for reactive search filtering
- Modal and toast systems are reusable across all pages

## Technical debt

1. Logs endpoint returns mock data — no real log aggregation
2. Create Site modal validates client-side only — no backend create endpoint
3. No persistent theme preference storage
4. No pagination for large datasets
5. Site detail tabs beyond General show placeholder content
6. No keyboard shortcuts or accessibility features

## Recommendations for Sprint 8

1. Add backend CRUD endpoints for sites (create, update, delete)
2. Implement real log capture and retrieval
3. Add persistent theme preference (localStorage)
4. Add confirm dialog component for destructive actions
5. Implement site start/stop/restart through the UI
6. Add pagination to resource tables
7. Add keyboard navigation support
