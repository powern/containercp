# Database Module Architecture

## Status: Postponed

This module is intentionally postponed.

> **v0.8 design update:** This historical postponed document is superseded
> for post-v0.7.0 planning by:
> `planning/database-module-v0.8-architecture.md`,
> `planning/database-module-v0.8-implementation-plan.md`,
> `planning/database-module-v0.8-open-source-review.md`, and
> `planning/database-module-v0.8-threat-model.md`.
> The final v0.8 cardinality decision is: one Site owns exactly one
> managed application database. A site Docker Compose stack contains one
> MariaDB container, and ContainerCP v0.8 manages one application
> database in that container. Multi-database management is intentionally
> postponed to a future major version.
> No implementation is approved until those documents are reviewed.

The current implementation provides a minimal Database resource (CRUD
records in `libs/database/`) sufficient for site creation (auto-creates
a database record and writes credentials to `.env`).  The Databases
page in the Web UI shows a placeholder listing with delete-only actions.

**No new database functionality will be implemented until this
architecture document is reviewed and the implementation plan is
approved.**

---

## Why postponed

1. **Runtime architecture came first** — The Runtime subsystem
   (`RuntimeActionExecutor`, `ServiceRole`, `SiteRuntimeManager`) was
   needed for all container operations including database restarts.
   That foundation is now complete.

2. **Domains module needed completion** — The Domains page redesign
   and JSON serialization fix took priority to establish clean
   patterns for enriched API responses.

3. **Database management is complex** — A complete database module
   requires secure credential management, embedded admin tools,
   import/export, backup integration, and user/privilege management.
   Rushing this would create technical debt.

4. **Underlying database engine decisions pending** — Whether to
   support only MariaDB or multiple engines (PostgreSQL, etc.)
   affects the compose generation, credential management, and
   admin tool integration.

---

## Goals

### Core functionality

- List each site's single managed application database with engine,
  version, status, and site relation
- Create and delete the managed database as part of explicit lifecycle
  phases only
- Show the one-to-one Site ↔ Managed Database relationship
- Secure credential storage and display
- Engine type and version tracking
- Container runtime status for database service

### Database admin tool (embedded)

- Adminer (preferred) — lightweight, single-file PHP, supports
  MySQL/MariaDB, PostgreSQL, SQLite
- phpMyAdmin (future option) — heavier, MySQL/MariaDB only, more
  features
- Must be isolated from ContainerCP auth
- Must be securely integrated — automatic login using stored
  credentials, or single-sign-on via ContainerCP session
- Must not expose database credentials in the URL or browser history

### Import / Export

- Export database to SQL dump
- Import from SQL dump
- Integration with Backup module for scheduled exports

### Backup / Restore

- Database dumps as part of site backup
- Restore database from backup
- Integration with existing `TarBackupProvider`

### User and privilege management

- Create the managed application database user for the selected site
- Grant/revoke privileges for that site's managed application database
- Track the single managed user/database relationship per site
- Secure password generation and rotation

### Audit logging

- Log database create, delete, import, export
- Log user creation and privilege changes
- Log access to admin tools
- Use existing `logger::Logger` infrastructure

---

## Architecture

### Ownership

```
Databases module (libs/database/)
  ├── DatabaseManager           — CRUD for database records (current)
  ├── DatabaseRuntimeBridge     — container status + restart (future)
  ├── DatabaseAdminService      — Adminer/phpMyAdmin integration (future)
  ├── DatabaseBackupService     — export/import/backup (future)
  └── DatabaseUserManager       — DB user accounts (future)
```

### Single Source of Truth

| Data | Owner | Must not duplicate |
|------|-------|-------------------|
| Database records | `DatabaseManager` | Storage layer |
| DB container status | `RuntimeActionExecutor` (via `DatabaseRuntimeBridge`) | Docker commands |
| DB container restart | `RuntimeActionExecutor` | Compose logic |
| Backups | `BackupManager` | Archive logic |
| SSL status | `CertificateStore` | Metadata file I/O |
| Site relation | `SiteManager` (via `site_id`) | Site data |

### Runtime integration

Database container runtime (status, restart, health) always starts from
the selected Site. The selected Site identifies the one Docker Compose
stack, and that stack contains the one managed MariaDB service for the
site:

```
DatabaseRuntimeBridge (thin)
  → ServiceRole::Database → "mariadb"
  → RuntimeActionExecutor::service_status(compose_dir, "mariadb")
  → RuntimeActionExecutor::restart_services(compose_dir, {"mariadb"})
```

No Docker logic in the Databases module.  Reuse `RuntimeActionExecutor`
and `CommandExecutor`.

### Admin tool integration

The embedded database admin tool (Adminer) must:

1. Run as a separate container in the selected site's Docker network
2. Authenticate through ContainerCP (session token or auto-login)
3. Be accessible only to authenticated admin users
4. Not require separate login credentials
5. Support at minimum MariaDB/MySQL
6. Open the selected site's managed application database directly; no
   database selection UI is required in v0.8

Integration pattern:
```
Admin panel → https://admin.domain/db/<site_id>/
  → nginx proxies to Adminer container
  → Adminer receives auto-login credentials via POST
    (credentials passed through ContainerCP session, never in URL)
```

### API design

Following the established pattern:

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/databases` | List all databases (enriched) |
| POST | `/api/databases/create` | Create the managed database for a site |
| POST | `/api/databases/remove` | Remove a database record |
| POST | `/api/databases/<id>/import` | Import SQL dump |
| GET | `/api/databases/<id>/export` | Export SQL dump |
| GET | `/api/databases/<id>/runtime` | DB container status |
| POST | `/api/databases/<id>/restart` | Restart DB container |

Enriched responses should use a `DatabaseViewService` pattern (like
`DomainViewService`) — not inline JSON in API handlers.

### Security

- Database credentials stored encrypted or at minimum in restricted
  files (not world-readable)
- Admin tool access gated by ContainerCP session
- No credentials in URLs, browser history, or logs
- SQL import validated before execution
- Export downloads gated by authentication

---

## Implementation order

0. **Reuse completed WordPress credential foundation** — consume
   `WordPressConfigService`, the structural `wp-config.php` parser, the
   safe config writer, password rotation, runtime verification,
   compensation, audit logging, and secure temporary credential transport.
   Do not introduce a second WordPress config parser.

1. **DatabaseViewService** — enriched read-only API response with site
   domain, runtime status, engine/version, credential availability,
   connection verification, ownership state, and imported database
   support for the one managed database per site

2. **DatabaseRuntimeBridge** — read-only container status for the
   site's MariaDB service, reusing `RuntimeActionExecutor`

3. **Database admin tool** — Adminer container integration with
   auto-login

4. **Import / Export** — SQL dump for the site's managed database via
   `mysqldump`/`mysql` commands

5. **User and privilege management** — managed application user only

6. **Backup integration** — Site Backup → Managed Database Dump →
   Archive

7. **Audit logging** — log all database operations

---

## Non-goals (explicitly excluded)

- **Database performance monitoring** — query profiling, slow query
  log, index analysis (belongs to a future Observability module)
- **Database clustering / replication** — belongs to advanced hosting
  tier
- **Schema migration management** — belongs to application deployment
  tooling
- **Multiple managed databases per site** — intentionally postponed to
  a future major version; v0.8 manages exactly one application database
  per site
- **Multi-engine management UI** — Adminer handles this, ContainerCP
  only configures the connection
- **Direct SQL execution panel** — security risk; use Adminer instead

---

## Related documents

- `docs/runtime-architecture.md` — Runtime subsystem (reused for DB
  container management)
- `docs/development/single-source-of-truth.md` — SSOT rules
- `docs/development/api-rules.md` — API design rules
- `planning/sites-runtime-management.md` — Site runtime pattern (DB
  follows the same pattern)
- `planning/product-roadmap.md` — version milestones
- `planning/backlog.md` — current priorities
