# Database Lifecycle Service

DB-3 adds safe physical MariaDB lifecycle operations behind the REST API.

## Ownership

- Read-only database list/detail views are owned by `DatabaseViewService`.
- Physical lifecycle orchestration is owned by `DatabaseLifecycleService`.
- Async queueing and job responses are owned by `DatabaseLifecycleJobService`.
- Engine-specific SQL and Docker command construction are owned by `MariaDBProvider` behind the `DatabaseProvider` interface.
- API handlers validate simple request shape only and delegate to the database services.

## Service Account

Newly generated Site stacks receive a per-site MariaDB service account in `.env`:

- `CONTAINERCP_DB_SERVICE_USER=containercp_service`
- `CONTAINERCP_DB_SERVICE_PASSWORD=<generated>`

`DockerComposeProvider` writes `config/mariadb/initdb/10-containercp-service-account.sh` for first MariaDB initialization. The bootstrap script uses `MYSQL_ROOT_PASSWORD` only during container entrypoint initialization. Normal DB-3 lifecycle operations read the service-account credential from the Site `.env` and do not fall back to root.

Older or imported stacks that do not have these keys return `service_account_unavailable`. ContainerCP does not automatically adopt or take ownership of imported databases.

## Provider Boundary

`MariaDBProvider` uses `CommandExecutor` argument vectors via `MariaDBProcessRunner`:

- no shell command strings;
- no `sh -c`, `bash -c`, `system()`, or `popen()`;
- credentials are written to owner-only temporary files;
- the temporary option file is copied into the MariaDB container and removed after the command;
- SQL is fed through an owner-only temporary stdin file;
- command argv never contains passwords.

The public lifecycle service depends only on `DatabaseProvider`, not MariaDB command details.

## Identifier Policy

`DatabaseIdentifierValidator` is the single validator for DB-3 MariaDB database and user names. Names must start with an ASCII letter and contain only ASCII letters, digits, and underscores. Database names are limited to 64 bytes. User names are limited to 32 bytes. Unsupported names are rejected instead of escaped broadly.

## Compensation

Create tracks which physical resources were created by the current job. On partial failure it removes only those resources and removes the metadata record. Compensation never drops pre-existing physical objects detected before the create mutation.

Drop revalidates the database/Site relation inside the job, requires exact typed confirmation, refuses imported or ownership-uncertain records, and removes metadata only after physical cleanup succeeds or the physical database is already absent and safe reconciliation can remove stale metadata.

## Legacy Removal

`POST /api/databases/remove` is deprecated and metadata-only. New clients must use `POST /api/databases/<id>/drop` for physical deletion or `POST /api/databases/<id>/forget-metadata` for explicit metadata recovery.
