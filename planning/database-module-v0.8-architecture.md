# Databases Module v0.8 Architecture

## Status

Design proposal for post-v0.7.0 work. This document records the target architecture only. It does not authorize production code, schema changes, Docker Compose changes, Adminer deployment, API route creation, GUI changes, Phase 12 work, or changes to `web2.softico.ua`.

## Decision Summary

ContainerCP v0.8 should turn the current metadata-only Databases page into a safe database management subsystem by introducing database lifecycle services behind the REST API. The first implementation supports MariaDB only, because the current generated site stack already provisions one per-site `mariadb:lts` service and all stored database records default to `engine = "mariadb"`. ContainerCP v0.8 manages exactly one application database for each Site. The architecture must still preserve provider boundaries so future engines extend the database provider layer instead of forcing an API, storage, or service rewrite.

The WordPress credential-management foundation is now complete and must be reused by the Databases module. `WordPressConfigService`, the structural `wp-config.php` parser, safe config writer, password rotation, runtime verification, compensation, secure temporary credential transport, and structured audit logging are the approved foundation for WordPress database credential coordination. The Databases module must not introduce another WordPress configuration parser or duplicate WordPress configuration mutation logic.

The default web administration tool should be Adminer, but Adminer deployment must be deferred until the database lifecycle API, credential handling, backup/export behavior, and access-gating design are implemented and tested.

## DB-3 Implementation Clarification

DB-3 implements the safe physical MariaDB lifecycle subset of this architecture:

- `DatabaseLifecycleService` owns create, verify, and drop orchestration for the selected Site's single managed database.
- `DatabaseProvider` is the public lifecycle boundary; v0.8 wires `MariaDBProvider` only.
- `DatabaseLifecycleJobService` owns asynchronous job queueing and public-safe job responses.
- `DatabaseIdentifierValidator` is the single validator for MariaDB database and user identifiers.
- `MariaDBSecureTempFile` owns host temporary credential/query files with owner-only permissions and cleanup on success, failure, and exception unwinding.
- New Site stacks receive a `containercp_service` MariaDB service account through `.env` and a first-boot MariaDB init script. `MYSQL_ROOT_PASSWORD` is used only by that bootstrap script during MariaDB entrypoint initialization; normal DB-3 operations do not fall back to root.
- The generated Compose `mariadb` service passes `CONTAINERCP_DB_SERVICE_USER` and `CONTAINERCP_DB_SERVICE_PASSWORD` into the container so the first-boot init script can consume them.
- The service account receives only global `CREATE`, global `CREATE USER`, read-only inspection of `mysql.user`/`mysql.db`, and database-scoped application privileges with `GRANT OPTION` on the selected Site database. It does not receive `ALL PRIVILEGES ON *.*`, `RELOAD`, or root fallback. MariaDB 12 grant-table statements take effect without `FLUSH PRIVILEGES`, so provider grant/revoke SQL must not require `RELOAD`.
- Repeated drop/recreate of the same managed database name is supported because MariaDB retains the database-scoped service-account grant row across `DROP DATABASE` and lets it apply again when the schema is recreated.
- Site database volumes are treated as Site-owned destructive resources only when exact ownership is proven. New volumes carry `containercp.site.id` and `containercp.domain` labels. Legacy unlabeled volumes may be cleaned only when the target MariaDB container mount proves ownership. Site creation fails closed if the expected volume already exists.
- Older/imported stacks without `CONTAINERCP_DB_SERVICE_USER` and `CONTAINERCP_DB_SERVICE_PASSWORD` report `service_account_unavailable` rather than silently using root or adopting the database.
- `POST /api/databases/remove` remains deprecated metadata-only compatibility behavior. Physical deletion is available only through `POST /api/databases/<id>/drop`; explicit recovery removal is `POST /api/databases/<id>/forget-metadata`.

DB-3 does not implement Adminer, import/export, database-aware backups, imported database adoption, password reveal, multiple databases per Site, or non-MariaDB providers.

## DB-4 Implementation Clarification

DB-4 implements job-backed logical SQL export/import for `ownership_state=managed` MariaDB databases only:

- `DatabaseDumpService` owns target validation, staging, artifact metadata, expiry cleanup, upload policy, import/export orchestration, and recovery diagnostics.
- `DatabaseDumpJobService` owns async export/import jobs and public-safe job responses.
- `MariaDBProvider` owns `mariadb-dump` and `mariadb` execution using argument vectors, owner-only option files, stdout-to-file execution, selected database targeting, and `--local-infile=0` for import.
- Artifact metadata is file-backed under the ContainerCP data root in `database-artifacts/`, outside web roots. Metadata persists across daemon restart and startup cleanup removes expired/revoked artifacts.
- DB-4 supports uncompressed `.sql` only, with a 5 MiB upload limit. The current HTTP framework uses bounded raw `application/sql` uploads; multipart streaming is deferred until implemented as a foundational HTTP improvement.
- Import mode is execute/import into the existing database, not guaranteed restore. A pre-import recovery export is created before import, but failures after MariaDB starts are reported as manual recovery because DDL cannot be transactionally rolled back.
- Uploaded imports are restricted to ContainerCP-generated DB-4 exports and rejected when known account, grant, database-switching, `DEFINER`, or local infile constructs are present.

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

### v0.8 Goals

- Provide safe database inventory with the selected Site's managed database relationship, runtime state, engine, version, size placeholder, and last backup/export status.
- Support one Site with exactly one managed application database in v0.8.
- Create and remove the managed application database through the REST API with physical database/user/grant lifecycle managed by backend services.
- Preserve API-first behavior: CLI and Web UI remain clients, not owners of business logic.
- Add logical SQL export/import using MariaDB tools and the Job subsystem.
- Include database dumps in site backup workflow before claiming database backup support.
- Add an authenticated Adminer launch path only after database lifecycle and credential controls are in place.
- Improve credential handling enough that no new feature exposes secrets in API responses, URLs, browser history, logs, command-line process lists, or downloaded backup manifests.

### Non-Goals

- PostgreSQL, SQLite, Redis, MongoDB, or multi-engine physical management in v0.8.
- MariaDB clustering, replication, Galera, PITR, or online physical backups with `mariadb-backup`.
- Direct SQL console inside ContainerCP.
- Query performance monitoring, slow query analysis, and schema migration management.
- Public internet exposure of Adminer or any database admin surface.
- Automatic modification of existing production installations during design approval.
- Multiple managed databases per site. The MariaDB server may technically host multiple databases, but ContainerCP v0.8 manages exactly one application database per Site.

## Approved Architecture Decisions

| Decision | Final position |
|----------|----------------|
| Version | Databases is a v0.8 major subsystem, not a v0.7.x patch |
| Cardinality | One Site owns exactly one managed application database in v0.8 |
| Engine model | Use a Database Profile abstraction; v0.8 implements MariaDB only |
| Root password | `MYSQL_ROOT_PASSWORD` is bootstrap-only, not normal runtime auth |
| Runtime DB auth | Use a dedicated minimum-privilege service account for ContainerCP operations |
| Password workflow | Prefer generate, rotate, replace; normal admin flow must not depend on reveal |
| Adminer lifecycle | Prefer on-demand temporary Adminer to minimize attack surface |
| First implementation phase | DB-1 is read-only inventory only; no physical lifecycle changes |
| Legacy imports | myVestaCP/imported site databases must be represented without assuming ContainerCP created or owns the physical database, user, or password |
| Immediate dependency | WordPress credential-management foundation is complete and must be reused; DB-1 may proceed read-only without duplicating WordPress config parsing or mutation logic |

## Site And Database Model

The v0.8 managed database model is one-to-one from Site to managed application database:

```text
Site
  -> Managed Application Database
```

The selected Site identifies the Docker Compose stack, the `mariadb` service, and the one managed application database. Database records may remain individually addressable by database ID for API compatibility and detail views, but lifecycle decisions target `Site -> Managed Database`. Services must verify that any database ID belongs to the selected Site before acting.

The MariaDB server may technically contain additional databases, especially on imported or manually modified installations. ContainerCP v0.8 must not expose those as multiple managed databases. Discovery may report imported/verification state for the selected Site's application database only. Multiple managed databases per Site are intentionally postponed to a future major version.

## Database Profiles

Introduce a Database Profile concept for engine/version/runtime capabilities.

Examples:

- MariaDB 11
- MySQL 8
- PostgreSQL 17

v0.8 implements only the MariaDB provider. PostgreSQL and MySQL support are not part of v0.8 implementation. The architecture should still avoid hardcoding MariaDB across API, storage, and service names where a provider abstraction is the correct boundary.

Database Profile responsibilities:

- Engine identifier, such as `mariadb`, `mysql`, or `postgresql`.
- Version label, such as `11`, `8`, or `17`.
- Default image and service name mapping.
- Supported lifecycle operations.
- Supported dump/import tools.
- Required service-account privileges.
- Admin-tool compatibility.

The v0.8 MariaDB profile maps to the existing `mariadb` Compose service. Future engines should add providers and profiles without rewriting the public database inventory API or database record ownership model.

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
| Database profile metadata | `DatabaseProfileRegistry` or equivalent future service | Owns supported engine/version capabilities |
| Physical database/user/grants | `DatabaseLifecycleService` | New service in `libs/database/`, delegates engine-specific work to providers |
| Engine command execution | `DatabaseProvider` implementations | v0.8 ships MariaDB provider wrapping `mariadb`, `mariadb-dump`, and safe option files |
| Runtime status/restart | `RuntimeActionExecutor` through a thin database view/runtime service | Existing runtime owner remains canonical |
| Credential generation | `PasswordGenerator`, later hardened or replaced | Must avoid logging and shell exposure |
| Credential persistence | Storage layer for metadata, site `.env` for stack bootstrapping | Requires threat-model follow-up before encryption |
| Backup records | `BackupManager` | Database dumps attach to backup workflow, not vice versa |
| Async operations | `JobExecutor` and `JobManager` | Required for create/drop/import/export/backup restore |
| Web administration surface | `DatabaseAdminService` | Launch/control Adminer after auth and lifecycle work |

## Credential Ownership And Duplication

| Data | Single Source of Truth | Current duplicated copies | Synchronization strategy |
|------|------------------------|---------------------------|--------------------------|
| Database name | `DatabaseManager` record | Site `.env` as `DB_NAME`; physical database object | Lifecycle service updates metadata first in a transaction-like workflow, then provider creates/renames physical object where supported, then `.env` is regenerated |
| Database user | `DatabaseManager` record | Site `.env` as `DB_USER`; physical database user | Lifecycle service owns user changes and reconciles metadata, physical grants, and `.env` |
| Database password | Secret owner behind database service; current storage field is transitional | SQLite `databases.db_password`; site `.env` as `DB_PASSWORD`; temporary provider option files | Normal workflow is rotate/replace, not reveal. Temporary files are created per operation and deleted. Future secret store can replace plaintext persistence without changing API semantics |
| Engine/profile | Database record plus Database Profile registry | Compose service/image template | Profile registry defines capabilities; provider generates or validates runtime mapping |
| Site relationship | `DatabaseManager` record through `site_id` | API view joins to `SiteManager`; backup/admin sessions reference both IDs | Site is the primary lifecycle target; database ID is verified as that Site's managed database before runtime, backup, Adminer, or deletion actions |

Hidden duplication is not allowed. Any future copy of database credentials, engine state, or site relationship must be documented as either a cache, runtime projection, or secret transport with explicit synchronization and cleanup rules.

### Imported myVestaCP Credential Boundary

Migrated myVestaCP sites can already contain a working application database connection even when ContainerCP did not create the physical database, did not create the database user, and does not have a stored password in SQLite.

DB-1 must resolve imported connection metadata through `WordPressConfigService` or another approved secret-handling boundary, not by exposing or permanently copying secrets into API responses. The boundary may inspect migrated site configuration such as application config files or generated environment files, but it must return only structured availability and verification results to the API layer. For WordPress sites, `WordPressConfigService` remains the single owner of `wp-config.php` parsing and mutation.

Rules for imported databases:

- Do not assume ContainerCP created the physical database.
- Do not assume ContainerCP created the database user.
- Do not assume the password exists in ContainerCP SQLite.
- Do not treat missing recoverable credentials as a missing physical database.
- Never return the password in normal API responses.
- Keep any recovered credential in memory only for the verification operation unless a later approved adoption workflow stores or rotates it.
- Do not rewrite application configuration, grants, users, or passwords during DB-1.

The imported credential resolver should report credential state independently from runtime and connection state. If credentials cannot be recovered safely, the database view should clearly show `credentials_unavailable` instead of hiding the database or marking it as absent.

## Database State Model

DB-1 must expose independent states instead of collapsing all database health into one status string.

| State dimension | Meaning | Example values |
|-----------------|---------|----------------|
| Runtime status | Container/service state from existing runtime integration | `Running`, `Stopped`, `Starting`, `Unhealthy`, `Unknown` |
| Connection verification | Result of a non-destructive login/select verification | `not_checked`, `verified`, `connection_failed`, `verification_required` |
| Credential availability | Whether ContainerCP can safely obtain credentials for verification or later actions | `available`, `credentials_unavailable`, `stored`, `resolved_from_site_config`, `not_required` |
| Management ownership | Whether ContainerCP owns lifecycle authority for the physical DB/user/grants | `managed`, `imported`, `verification_required`, `credentials_unavailable`, `connection_failed` |

Management states:

- `managed`: ContainerCP created or adopted the database/user and owns lifecycle operations.
- `imported`: Database was discovered or migrated from myVestaCP/site configuration and is represented read-only.
- `verification_required`: Metadata exists but the physical connection has not been verified yet.
- `credentials_unavailable`: The database may exist, but credentials are not safely available to ContainerCP.
- `connection_failed`: Credentials were available, but non-destructive verification failed.

These states are independent. For example, runtime can be `Running` while credential availability is `credentials_unavailable`, or management ownership can be `imported` while connection verification is `verified`.

## Proposed Components

### `WordPressConfigService` Dependency

Database inventory, adoption, and rotation must not duplicate WordPress config parsing. `WordPressConfigService` is the completed owner for detecting `wp-config.php`, classifying credential sources, reading supported non-secret metadata, updating supported direct constants atomically, and validating/rolling back config changes.

Databases DB-1 may consume only read-only inspection results from this service. Later lifecycle phases must call it for WordPress application credential changes instead of writing `wp-config.php` directly.

### `DatabaseViewService`

Builds enriched read models for API responses. It should combine `DatabaseManager`, `SiteManager`, and runtime status without changing physical state.

Fields should include:

- `id`
- `name`
- `user`
- `engine`
- `version`
- `profile`
- `site_id`
- `site_domain`
- `enabled`
- `runtime_status`
- `connection_verification_status`
- `credential_availability_status`
- `management_ownership_status`
- `size_bytes`
- `last_backup_status`
- `detail_url` or detail route identifier
- `can_create`
- `can_drop`
- `can_export`
- `can_import`
- `has_admin_tool`

The service must never include `db_password`, `MYSQL_ROOT_PASSWORD`, or one-time Adminer tokens.

For imported myVestaCP databases, the view service should show the database as imported even if ContainerCP metadata is incomplete, provided the migrated site configuration exposes enough non-secret metadata to identify the connection. Password values remain hidden and are used only inside the approved verification boundary.

### `DatabaseLifecycleService`

Owns create, drop, password rotation, and grants for the selected Site's managed application database. It should not call Docker directly except through a provider abstraction that executes commands inside the existing per-site stack.

Required operations:

- `create_managed_database(site_id, requested_name, requested_user)`
- `drop_managed_database(site_id, DropMode mode)`
- `rotate_password(site_id)`
- `verify_managed_database(site_id)`
- `repair_metadata(site_id)` as an explicit admin-only recovery operation

`DropMode` should make destructive behavior explicit:

- `metadata_only` for recovery of stale records only.
- `physical_and_metadata` for normal delete after confirmation.
- `physical_only` for repair of orphaned MariaDB objects only.

The service must not search among multiple managed databases for a Site. It must resolve the selected Site's single managed database, verify the stored database record relation when a database ID is supplied, and fail closed if the Site has zero or more than one managed record until an explicit repair/adoption workflow resolves the inconsistency.

### `DatabaseProvider`

Abstract provider boundary for engine-specific lifecycle, dump/import, user, grant, and verification behavior. v0.8 implements a MariaDB provider only.

Provider responsibilities:

- Create/drop database.
- Create/drop users.
- Grant/revoke privileges.
- Verify database/user/grant existence.
- Rotate passwords.
- Export/import using engine-appropriate tools.
- Report engine version and capability metadata.

Future PostgreSQL or MySQL support should extend this provider boundary and add a Database Profile. It should not require rewriting the database API or view service.

### `MariaDBProvider`

Executes MariaDB client tools safely. It should use argument-vector execution through `CommandExecutor`, not shell string concatenation.

Required rules:

- Use temporary option files with `0600` permissions for passwords instead of `--password=value` arguments.
- Use `docker compose --project-directory <site_dir> exec -T mariadb ...` for commands that must run inside the site stack.
- Validate database names and user names before constructing SQL.
- Quote SQL identifiers with a dedicated helper that rejects unsupported names instead of attempting broad escaping.
- Capture stderr but sanitize it before returning API errors.
- Return structured results to lifecycle/import/export services.

MariaDB provider authentication is split into bootstrap and runtime responsibilities:

| Phase | Credential | Responsibility |
|-------|------------|----------------|
| Bootstrap | Root password generated for initial container initialization | Create initial database, initial application user, and the ContainerCP service account if needed |
| Runtime | Dedicated ContainerCP service account | Perform normal create/drop/verify/rotate/export/import operations with minimum required privileges |
| Emergency recovery | Elevated credential or manual operator action | Optional break-glass only; disabled by default and explicitly approved per installation |

`MYSQL_ROOT_PASSWORD` must not be used for normal runtime operations. The service account should have only the grants required for approved lifecycle actions. Operations that require elevated privileges must be listed, logged, and treated as exceptional.

Minimum MariaDB service-account grants require final implementation validation, but the architecture expects separation by operation:

- Inventory/verify: metadata reads and limited `SHOW`/information-schema access.
- Create database/user/grants: database and account-management privileges scoped as tightly as MariaDB permits.
- Drop database/user/grants: explicit destructive privileges, always behind confirmation and audit.
- Password rotation: account alteration for owned database users.
- Export/import: data privileges on the target database, preferably using the database's own user or a scoped service account.

Required grants should be documented per provider before implementation. The expected MariaDB grant model is:

| Operation | Expected privilege class | Elevated? |
|-----------|--------------------------|-----------|
| Inventory/version/status | Read-only metadata visibility, such as limited `SHOW` and information-schema reads | No |
| Verify owned database/user/grants | Metadata visibility for owned databases and users | No |
| Export owned database | `SELECT`, `SHOW VIEW`, `TRIGGER` where dump options require it | No, if scoped per database |
| Import into owned database | `CREATE`, `ALTER`, `DROP`, `INSERT`, `UPDATE`, `DELETE`, `INDEX`, `REFERENCES`, `TRIGGER` as needed by accepted SQL policy | Potentially destructive but scoped |
| Create database and application user | Database creation and account/grant management | Yes, service-account controlled |
| Drop database and application user | Database drop and account/grant management | Yes, destructive and confirmation-gated |
| Rotate application user password | Account alteration for the owned user | Yes, but narrower than root |
| Service account creation or repair | Root/bootstrap or manual operator credential | Break-glass only |

Elevated operations are limited to create/drop user, create/drop database, grant/revoke privileges, service-account repair, and emergency recovery. They must be audited and must not use `MYSQL_ROOT_PASSWORD` during normal runtime.

## DB-1 Read-Only Phase

DB-1 is the first implementation phase and remains read-only with respect to physical database state. It must not create databases, drop physical databases, deploy Adminer, import SQL, export SQL, rotate passwords, change grants, recreate users, or rewrite site/application configuration.

DB-1 delivers:

- `DatabaseViewService`.
- Enriched `GET /api/databases` response.
- Database detail view data.
- Site domain in responses instead of exposing only raw `site_id`.
- Runtime status through existing runtime services.
- Engine profile/version display.
- Database size field, even if initially `unknown` or provider-derived later.
- Imported myVestaCP database inventory.
- Non-destructive connection verification for imported databases when credentials are safely available.
- Independent runtime, connection verification, credential availability, and management ownership states.

DB-1 must not change delete, restart, lifecycle, import/export, password rotation, Adminer, Docker Compose, or physical MariaDB state. Existing metadata-only delete behavior may remain as-is, but DB-1 must not expand or rebrand it as physical lifecycle management.

For imported databases, DB-1 verification is limited to a non-destructive connection check such as opening a connection and running a safe read-only probe. DB-1 must not rotate passwords, change grants, create users, revoke users, recreate the database, or write application configuration.

## Imported Database Adoption Workflow

Adoption is a later explicit workflow, not DB-1. It converts an imported database into a managed database only after safe verification and compensation planning.

Adopt database workflow:

1. Verify the existing connection using the approved secret boundary.
2. Create a new managed user or select an existing managed user according to provider policy.
3. Grant only the required privileges for the target database.
4. Update the application secret/configuration through the owning configuration service.
5. Reload or restart only the affected service required to pick up the new credential.
6. Verify the application/database connection with the new credential.
7. Revoke old credentials only after successful verification.
8. If any step fails, compensate safely by preserving the original credential/configuration and restoring service operability.

Adoption must produce audit events for verification, user creation/selection, configuration update, service reload, final verification, old credential revoke, and compensation.

### `DatabaseDumpService`

Owns logical export/import and backup integration.

Export should use `mariadb-dump` with v0.8 defaults:

- `--single-transaction` for InnoDB consistency.
- `--quick` for large tables.
- `--routines` and `--events` only after privilege requirements are verified.
- Credentials via option file, not command line.
- Output written to a staging directory under ContainerCP control before publication.

Import should use the `mariadb` client with strict staging:

- Accept only uploaded `.sql` or compressed `.sql.gz` after content-type and size checks.
- Store uploaded files outside web roots.
- Run import as an async job.
- Target the selected Site's managed application database and require explicit destructive confirmation if import may overwrite objects.
- Preserve the source file until job completion diagnostics are available.

DB-4 initial implementation chooses non-destructive execute/import semantics with a pre-import recovery export rather than claiming full replacement restore. Replace-style restore remains future work unless it can provide safe recovery guarantees.

### `DatabaseAdminService`

Owns Adminer integration. It must not be implemented as a public static route or unmanaged site file.

Recommended v0.8 posture:

- Adminer is disabled by default.
- Adminer is launched for the selected Site's managed application database.
- The preferred model is an on-demand temporary Adminer container.
- Adminer runs in the same private site network as the target database service, not on a host port.
- The public reverse proxy only exposes Adminer through authenticated ContainerCP routes.
- Launch uses a short-lived server-side token, not credentials in URLs or JavaScript-visible state.
- Adminer container and token state are removed when the session expires or the operator revokes it.
- Adminer opens the selected Site's managed database directly; v0.8 does not require a database selection UI.

## API Design

API details should be finalized in `docs/api/API_REFERENCE.md` during implementation. The initial endpoint shape should be resource-oriented and job-based for long-running operations.

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/databases` | Enriched list, no secrets |
| GET | `/api/databases/<id>` | Database details, no secrets |
| POST | `/api/databases` | Create the managed database/user/grants for a site |
| POST | `/api/databases/<id>/drop` | Drop physical DB/user/grants and metadata with explicit confirmation |
| POST | `/api/databases/<id>/verify` | Compare metadata to physical state |
| POST | `/api/databases/<id>/rotate-password` | Rotate user password and update dependent files |
| POST | `/api/databases/<id>/export` | Queue SQL export job for the selected site's managed database |
| POST | `/api/databases/<id>/import` | Queue SQL import job for the selected site's managed database |
| POST | `/api/databases/<id>/admin-session` | Create short-lived Adminer launch token |
| POST | `/api/databases/<id>/admin-session/revoke` | Revoke Adminer launch token |

Compatibility note: the existing `POST /api/databases/remove` endpoint is metadata-only. v0.8 should either replace it with a safer endpoint or keep it as an admin-only recovery endpoint with a different name. It must not silently continue to look like a physical database drop.

## UI Design Requirements

The Web UI follows API completion. The Databases page should remain thin and call only API endpoints.

Required UI states:

- List managed databases with site domain, engine, runtime, and enabled state.
- Show clear difference between `metadata only`, `verified`, `missing physical database`, and `orphan physical database` states.
- Require typed confirmation for physical drop.
- Show async job progress for create/drop/import/export/backup restore.
- Do not display passwords by default.
- If a reveal/reset flow is added, require explicit confirmation and audit logging.
- Adminer launch button appears only when the backend returns `has_admin_tool` and an active launch token can be created.

DB-1 UI scope is intentionally smaller. It must remain a read-only view over inventory, detail, runtime state, credential availability, connection verification, and ownership state. Delete, restart, Adminer, import/export, password rotation, and other lifecycle actions remain out of DB-1.

## Credential Architecture

The current system stores the database user password in SQLite and writes it to the site `.env`. v0.8 should not pretend this is fully solved by Adminer. The first implementation should reduce exposure while preserving operability.

Required minimum controls:

- No secrets in API list/detail responses.
- No secrets in URLs, browser history, logs, job messages, or command arguments.
- No secrets in JavaScript variables, local storage, session storage, DOM attributes, or generated client-side links.
- Temporary credential files created with owner-only permissions and deleted after use.
- Backup archives must document whether `.env` is included and whether database dumps include credentials.
- Adminer token stores credentials server-side only and expires quickly.
- Password rotation updates MariaDB user password, ContainerCP metadata, and site `.env` atomically enough to avoid permanent drift.
- Normal administrator workflow is rotate password, not reveal password.
- Plaintext recovery must not be assumed as a permanent capability.

Future controls:

- Secret-at-rest encryption or sealed local secret store.
- Per-operation ephemeral database users for export/import/admin sessions.
- Break-glass recovery workflow for lost secrets.

Break-glass password recovery, if retained, is optional, disabled by default, and requires explicit approval. It must be audited and must not be part of the normal administrator workflow.

## Backup Architecture

DB-5 makes the existing Backups subsystem database-aware by adding a backup orchestration service that reuses DB-4 logical dump/import primitives.

v0.8 backup behavior:

- `BackupService` owns archive staging, manifest creation, backup metadata, listing/download/remove, and restore orchestration.
- `BackupJobService` queues backup create/restore work through `JobExecutor`.
- `DatabaseDumpService` remains the single owner of logical SQL dump/import execution and MariaDB credential handling.
- Dumps are written under a controlled backup staging path, then included in the final archive as `backup-root/database/managed.sql`.
- Restore supports `full`, `files_only`, and `database_only`; full/database restore validates the SQL payload checksum and requires exact typed confirmation before import.
- Failed dump/import makes the backup/restore job fail, not partially succeed silently.

## Adminer Architecture

Adminer is the default candidate because it is a single PHP application, supports MariaDB, has a small footprint, and can run inside the existing site networking model.

Adminer must be integrated as a controlled service, not copied into customer web roots. The deployment model is explicitly on-demand Adminer unless implementation evidence shows a permanent sidecar is safer.

Adminer must be considered equivalent to direct database access. If an attacker reaches an authenticated Adminer session, the attacker can read and modify application data. Therefore Adminer launch must be auditable, time-limited, revocable, and unavailable to unauthenticated users.

| Model | Attack surface | RAM usage | Startup latency | Network isolation | Cleanup complexity | Credential lifetime | Maintenance | Multi-node compatibility |
|-------|----------------|-----------|-----------------|-------------------|--------------------|---------------------|-------------|--------------------------|
| Permanent sidecar | Higher because Adminer is always running and reachable from its private route if misconfigured | Higher baseline per site | None after startup | Good if per-site network only | Lower runtime cleanup, higher long-term lifecycle management | Longer-lived integration credentials or sessions are more likely | More containers to patch and monitor continuously | Harder at scale because every node/site may carry idle Adminer containers |
| On-demand temporary container | Lower because Adminer exists only during approved sessions | Near zero when inactive | Higher because launch must start a container | Good if attached only to target site network | Higher because token, route, and container cleanup must be reliable | Shortest; credentials can be scoped to session TTL | Fewer always-running components, but launch image must still be patched | Better default because sessions can be scheduled on the target node only when needed |

Recommended model: on-demand temporary Adminer. It minimizes idle attack surface, memory use, and credential lifetime. The implementation must invest in reliable cleanup, token expiry, and route revocation.

Adminer session lifecycle:

1. Administrator requests an Adminer session for the selected Site's managed database.
2. `DatabaseAdminService` verifies auth, site relation, runtime status, and database profile support.
3. A short-lived server-side token is created and bound to user, database ID, site ID, node ID, expiry, and nonce.
4. On-demand Adminer container is started on the target site network without host ports.
5. Reverse proxy route is enabled only for the token/session scope.
6. Credentials remain server-side and are never emitted into JavaScript variables or URLs.
7. Session expiry or revoke removes proxy route, token state, and temporary container.
8. Cleanup failures are logged and retried by a recovery job.

## Validation Requirements

v0.8 implementation is not complete until these checks pass:

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

- What exact MariaDB grants should the runtime service account receive for each operation?
- Should DB-1 expose logs from Docker Compose only, MariaDB logs only, or both?
- What maximum import size should be supported in v0.8?
- DB-5 decision: the managed database dump is included inside the backup archive under `backup-root/database/managed.sql`, with public-safe metadata in `backup-root/manifest.json`.

## Related Documents

- `planning/database-module-v0.8-implementation-plan.md`
- `planning/database-module-v0.8-open-source-review.md`
- `planning/database-module-v0.8-threat-model.md`
- `planning/wordpress-config-management-v0.8-architecture.md`
- `planning/wordpress-db-password-rotation-v0.8-plan.md`
- `planning/wordpress-db-password-rotation-v0.8-threat-model.md`
- `planning/wp-cli-integration-v0.8-review.md`
- `planning/database-module-architecture.md`
- `docs/development/api-rules.md`
- `docs/development/single-source-of-truth.md`
- `docs/runtime-architecture.md`
- `docs/api/API_REFERENCE.md`
