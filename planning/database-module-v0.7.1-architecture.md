# Databases Module v0.7.1 Architecture

## Status

Design proposal for post-v0.7.0 work. This document records the target architecture only. It does not authorize production code, schema changes, Docker Compose changes, Adminer deployment, API route creation, GUI changes, Phase 12 work, or changes to `web2.softico.ua`.

## Decision Summary

ContainerCP v0.7.1 should turn the current metadata-only Databases page into a safe MariaDB management module by introducing a database lifecycle service behind the REST API. The module should begin with MariaDB only, because the current generated site stack already provisions a per-site `mariadb:lts` service and all stored database records default to `engine = "mariadb"`.

The default web administration tool should be Adminer, but Adminer deployment must be deferred until the database lifecycle API, credential handling, backup/export behavior, and access-gating design are implemented and tested.

## Current Baseline

The current system already has a Database resource, but it does not manage physical MariaDB objects directly.

| Area | Current behavior | Evidence |
|------|------------------|----------|
| Data model | `Database` stores name, user, password, engine, version, owner, site relation, enabled flag | `libs/database/Database.h` |
| Metadata manager | `DatabaseManager::create()` and `remove()` update only the in-memory `databases_` vector | `libs/database/DatabaseManager.cpp` |
| Site creation | Site creation generates `safe_db`, `safe_user`, random password, then creates a database metadata record | `libs/operations/SiteCreateOperation.cpp` |
| Compose topology | Each site stack includes a `mariadb` service using `mariadb:lts` and `db-data:/var/lib/mysql` | `libs/docker/ComposeGenerator.cpp` |
| Environment secrets | `.env` stores `DB_NAME`, `DB_USER`, `DB_PASSWORD`, `MYSQL_ROOT_PASSWORD`, `REDIS_PASSWORD` | `libs/docker/EnvGenerator.cpp` |
| Storage | SQLite stores `db_password` in the `databases` table and round-trips it | `libs/storage/SchemaMigrations.cpp`, `libs/storage/SQLiteStorage.cpp` |
| API list | `GET /api/databases` returns redacted database metadata without password | `libs/api/ApiServer.cpp`, `libs/api/JsonFormatter.cpp` |
| API remove | `POST /api/databases/remove` removes only the metadata record through `remove_resource` | `libs/api/ApiServer.cpp` |
| Runtime | `ServiceRole::Database` maps to action suffix `db` and Compose service `mariadb` | `libs/runtime/ServiceRole.cpp` |
| Runtime API | `GET /api/runtime/<site_id>` includes `db`; `POST /api/runtime/<site_id>/restart-db` restarts `mariadb` | `libs/api/ApiServer.cpp`, `libs/runtime/SiteRuntimeManager.cpp` |
| Backup | Current backup archives the site directory with `tar`; it does not create a logical SQL dump | `libs/backup/TarBackupProvider.cpp`, `libs/api/ApiServer.cpp` |
| Web UI | Databases page lists metadata and exposes delete-only behavior | `web/app.js` |

## Product Scope

### v0.7.1 Goals

- Provide safe database inventory with site relationship, runtime state, engine, version, size placeholder, and last backup/export status.
- Create and remove MariaDB databases through the REST API with physical database/user/grant lifecycle managed by backend services.
- Preserve API-first behavior: CLI and Web UI remain clients, not owners of business logic.
- Add logical SQL export/import using MariaDB tools and the Job subsystem.
- Include database dumps in site backup workflow before claiming database backup support.
- Add an authenticated Adminer launch path only after database lifecycle and credential controls are in place.
- Improve credential handling enough that no new feature exposes secrets in API responses, URLs, browser history, logs, command-line process lists, or downloaded backup manifests.

### Non-Goals

- PostgreSQL, SQLite, Redis, MongoDB, or multi-engine management in v0.7.1.
- MariaDB clustering, replication, Galera, PITR, or online physical backups with `mariadb-backup`.
- Direct SQL console inside ContainerCP.
- Query performance monitoring, slow query analysis, and schema migration management.
- Public internet exposure of Adminer or any database admin surface.
- Automatic modification of existing production installations during design approval.

## Architecture Principles

- API first: every operation is exposed through REST before CLI or Web UI.
- Architecture order: Core, Resource, Manager, Storage, Provider, Daemon, REST API, Web UI, CLI, Tests.
- Business logic lives in `libs/database/` services and providers, not in `ApiServer.cpp`, CLI handlers, or `web/app.js`.
- Runtime commands are delegated to `RuntimeActionExecutor` and `CommandExecutor`; no duplicate Docker command logic in the Databases module.
- Storage remains the single source of truth for ContainerCP metadata, not for physical MariaDB state.
- Physical lifecycle must be idempotent where possible and must not silently leave metadata inconsistent with MariaDB.
- Long-running work uses `JobExecutor` with explicit progress and failure messages.
- Passwords and SQL imports are treated as security-sensitive inputs.

## Ownership Model

| Capability | Owner | Notes |
|------------|-------|-------|
| Database metadata | `DatabaseManager` | Owns ContainerCP record lifecycle and lookups |
| Physical MariaDB database/user/grants | `DatabaseLifecycleService` | New service in `libs/database/` |
| MariaDB command execution | `MariaDBProvider` | New provider wrapping `mariadb`, `mariadb-dump`, and safe option files |
| Runtime status/restart | `RuntimeActionExecutor` through a thin database view/runtime service | Existing runtime owner remains canonical |
| Credential generation | `PasswordGenerator`, later hardened or replaced | Must avoid logging and shell exposure |
| Credential persistence | Storage layer for metadata, site `.env` for stack bootstrapping | Requires threat-model follow-up before encryption |
| Backup records | `BackupManager` | Database dumps attach to backup workflow, not vice versa |
| Async operations | `JobExecutor` and `JobManager` | Required for create/drop/import/export/backup restore |
| Web administration surface | `DatabaseAdminService` | Launch/control Adminer after auth and lifecycle work |

## Proposed Components

### `DatabaseViewService`

Builds enriched read models for API responses. It should combine `DatabaseManager`, `SiteManager`, and runtime status without changing physical state.

Fields should include:

- `id`
- `name`
- `user`
- `engine`
- `version`
- `site_id`
- `site_domain`
- `enabled`
- `runtime_status`
- `can_create`
- `can_drop`
- `can_export`
- `can_import`
- `has_admin_tool`

The service must never include `db_password`, `MYSQL_ROOT_PASSWORD`, or one-time Adminer tokens.

### `DatabaseLifecycleService`

Owns create, drop, password rotation, and grants. It should not call Docker directly except through a provider abstraction that executes commands inside the existing per-site stack.

Required operations:

- `create_database(site_id, requested_name, requested_user)`
- `drop_database(database_id, DropMode mode)`
- `rotate_password(database_id)`
- `verify_database(database_id)`
- `repair_metadata(database_id)` as an explicit admin-only recovery operation

`DropMode` should make destructive behavior explicit:

- `metadata_only` for recovery of stale records only.
- `physical_and_metadata` for normal delete after confirmation.
- `physical_only` for repair of orphaned MariaDB objects only.

### `MariaDBProvider`

Executes MariaDB client tools safely. It should use argument-vector execution through `CommandExecutor`, not shell string concatenation.

Required rules:

- Use temporary option files with `0600` permissions for passwords instead of `--password=value` arguments.
- Use `docker compose --project-directory <site_dir> exec -T mariadb ...` for commands that must run inside the site stack.
- Validate database names and user names before constructing SQL.
- Quote SQL identifiers with a dedicated helper that rejects unsupported names instead of attempting broad escaping.
- Capture stderr but sanitize it before returning API errors.
- Return structured results to lifecycle/import/export services.

### `DatabaseDumpService`

Owns logical export/import and backup integration.

Export should use `mariadb-dump` with v0.7.1 defaults:

- `--single-transaction` for InnoDB consistency.
- `--quick` for large tables.
- `--routines` and `--events` only after privilege requirements are verified.
- Credentials via option file, not command line.
- Output written to a staging directory under ContainerCP control before publication.

Import should use the `mariadb` client with strict staging:

- Accept only uploaded `.sql` or compressed `.sql.gz` after content-type and size checks.
- Store uploaded files outside web roots.
- Run import as an async job.
- Require target database selection and explicit destructive confirmation if import may overwrite objects.
- Preserve the source file until job completion diagnostics are available.

### `DatabaseAdminService`

Owns Adminer integration. It must not be implemented as a public static route or unmanaged site file.

Recommended v0.7.1 posture:

- Adminer is disabled by default.
- Enabling Adminer is per-site and time-limited.
- Adminer runs in the same private site network as `mariadb`, not on a host port.
- The public reverse proxy only exposes Adminer through authenticated ContainerCP routes.
- Launch uses a short-lived server-side token, not credentials in URLs.
- Adminer container is stopped or removed when the token expires or the operator disables it.

## API Design

API details should be finalized in `docs/api/API_REFERENCE.md` during implementation. The initial endpoint shape should be resource-oriented and job-based for long-running operations.

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/databases` | Enriched list, no secrets |
| GET | `/api/databases/<id>` | Database details, no secrets |
| POST | `/api/databases` | Create database/user/grants for a site |
| POST | `/api/databases/<id>/drop` | Drop physical DB/user/grants and metadata with explicit confirmation |
| POST | `/api/databases/<id>/verify` | Compare metadata to physical state |
| POST | `/api/databases/<id>/rotate-password` | Rotate user password and update dependent files |
| POST | `/api/databases/<id>/export` | Queue SQL export job |
| POST | `/api/databases/<id>/import` | Queue SQL import job |
| POST | `/api/databases/<id>/admin-session` | Create short-lived Adminer launch token |
| POST | `/api/databases/<id>/admin-session/revoke` | Revoke Adminer launch token |

Compatibility note: the existing `POST /api/databases/remove` endpoint is metadata-only. v0.7.1 should either replace it with a safer endpoint or keep it as an admin-only recovery endpoint with a different name. It must not silently continue to look like a physical database drop.

## UI Design Requirements

The Web UI follows API completion. The Databases page should remain thin and call only API endpoints.

Required UI states:

- List all databases with site domain, engine, runtime, and enabled state.
- Show clear difference between `metadata only`, `verified`, `missing physical database`, and `orphan physical database` states.
- Require typed confirmation for physical drop.
- Show async job progress for create/drop/import/export/backup restore.
- Do not display passwords by default.
- If a reveal/reset flow is added, require explicit confirmation and audit logging.
- Adminer launch button appears only when the backend returns `has_admin_tool` and an active launch token can be created.

## Credential Architecture

The current system stores the database user password in SQLite and writes it to the site `.env`. v0.7.1 should not pretend this is fully solved by Adminer. The first implementation should reduce exposure while preserving operability.

Required minimum controls:

- No secrets in API list/detail responses.
- No secrets in URLs, browser history, logs, job messages, or command arguments.
- Temporary credential files created with owner-only permissions and deleted after use.
- Backup archives must document whether `.env` is included and whether database dumps include credentials.
- Adminer token stores credentials server-side only and expires quickly.
- Password rotation updates MariaDB user password, ContainerCP metadata, and site `.env` atomically enough to avoid permanent drift.

Future controls:

- Secret-at-rest encryption or sealed local secret store.
- Per-operation ephemeral database users for export/import/admin sessions.
- Break-glass recovery workflow for lost secrets.

## Backup Architecture

The current tar backup captures the site directory. A database module cannot claim backup support until it creates a logical SQL dump and records the dump in backup metadata.

v0.7.1 backup behavior:

- Site backup should queue a pre-backup database dump job for every enabled database attached to the site.
- Dumps should be written under a controlled backup staging path, then included in the final archive.
- Restore should restore files first, then import database dumps through `DatabaseDumpService`.
- Restore must never import into a running production database without explicit operator confirmation.
- Failed dump/import makes the backup/restore job fail, not partially succeed silently.

## Adminer Architecture

Adminer is the default candidate because it is a single PHP application, supports MariaDB, has a small footprint, and can run inside the existing site networking model.

Adminer must be integrated as a controlled service, not copied into customer web roots. The recommended deployment is a managed sidecar container on the site private network, exposed only through ContainerCP-authenticated proxy routing.

Adminer must be considered equivalent to direct database access. If an attacker reaches an authenticated Adminer session, the attacker can read and modify application data. Therefore Adminer launch must be auditable, time-limited, revocable, and unavailable to unauthenticated users.

## Validation Requirements

v0.7.1 implementation is not complete until these checks pass:

- Unit tests for database name/user validation and SQL identifier rejection.
- Unit tests for `DatabaseLifecycleService` state transitions and rollback behavior.
- Unit tests proving API/database JSON never includes passwords.
- Integration tests against a disposable MariaDB Compose stack for create, verify, drop, export, import, and password rotation.
- Backup tests proving SQL dump inclusion and restore import behavior.
- Runtime tests proving existing `restart-db` behavior remains unchanged.
- Web UI smoke test or DOM-level test for destructive confirmation and job polling.
- Negative security tests for path traversal, oversized import, invalid SQL file path, and command failure sanitization.
- Full doctest/CTest pass and zero compiler warnings before commit.
- VM validation on a non-production host before any production rollout.

## Open Questions

- Should password reveal ever be allowed, or should v0.7.1 support only reset/rotate?
- Should database creation be limited to one database per site in v0.7.1, matching current site creation behavior?
- Should `MYSQL_ROOT_PASSWORD` remain in site `.env`, or should lifecycle commands use a dedicated admin account with narrower privileges?
- Should Adminer be per-site sidecar, shared control-plane service, or generated only on demand?
- What maximum import size should be supported in v0.7.1?
- Should database dumps be included inside the existing site tar archive or stored as sibling artifacts referenced by the backup record?

## Related Documents

- `planning/database-module-v0.7.1-implementation-plan.md`
- `planning/database-module-v0.7.1-open-source-review.md`
- `planning/database-module-v0.7.1-threat-model.md`
- `planning/database-module-architecture.md`
- `docs/development/api-rules.md`
- `docs/development/single-source-of-truth.md`
- `docs/runtime-architecture.md`
- `docs/api/API_REFERENCE.md`
