# Sprint 7 — Admin Panel Phase 2

Status: Completed

**Goal:** Transform the admin panel from read-only dashboard into an
interactive management interface.

## Scope

- Backend: add missing REST endpoints for profiles, nodes, logs
- Frontend: site detail page with tabs
- Frontend: create site modal with form
- Frontend: toast notification system
- Frontend: improved tables with actions
- Frontend: global error handling

## Out of scope

- User authentication/login
- Real WebSocket events
- Multi-user sessions
- Production HTTPS for the UI
- Mobile native app

## Tasks

### Backend

CCP-5001: Add /api/profiles endpoint
CCP-5002: Add /api/nodes endpoint  
CCP-5003: Add /api/logs endpoint
CCP-5004: Add /api/site-config endpoint (per-site template/profile info)

### Frontend

CCP-5005: Site detail page with tabs (General, Profiles, SSL, Proxy, Access)
CCP-5006: Create Site modal/dialog
CCP-5007: Toast notification system
CCP-5008: Improved tables with row actions and search
CCP-5009: Global error handling and loading states

## Definition of done

1. All new API endpoints return JSON
2. Clicking a site row opens a detail page
3. "Create Site" button opens a modal with owner/domain/template fields
4. Toast messages appear for success/error/info
5. Tables show action buttons (View, Edit, Delete)
6. Network errors show user-friendly messages
7. All existing CLI commands and tests continue working
