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

The generated Compose `mariadb` service must pass both service-account variables into the container environment. Having the keys only in `.env` is not sufficient because MariaDB init scripts execute inside the container. The init script fails clearly if either service-account variable is unset, does not enable shell tracing, and does not echo credential values.

The service account is created only by MariaDB's first-run `/docker-entrypoint-initdb.d` flow. If an existing data directory is reused, MariaDB skips init scripts; DB-3 must then report `service_account_unavailable` or connection failure instead of falling back to root.

Older or imported stacks that do not have these keys return `service_account_unavailable`. ContainerCP does not automatically adopt or take ownership of imported databases.

## Site Database Volume Ownership

Each generated Site stack owns one Compose named MariaDB volume: `<compose-project>_db-data`, mounted at `/var/lib/mysql` by the Site's `mariadb` service. New generated volumes carry ContainerCP ownership labels:

- `containercp.site.id=<site_id>`
- `containercp.domain=<domain>`
- Compose labels `com.docker.compose.project=<project>` and `com.docker.compose.volume=db-data`

Confirmed Site removal is a destructive lifecycle action and removes the exclusively owned MariaDB volume after the stack is stopped. Cleanup is allowed only when ownership is proven by exact labels, or for legacy unlabeled volumes when the volume is currently mounted by the exact target `site-<id>-db` container with matching Site ID, Compose project, service name `mariadb`, and destination `/var/lib/mysql`.

Cleanup refuses unknown, mismatched, or shared volumes. Failure to remove an owned volume is returned as a visible operation failure instead of silently leaving stale data behind.

Site creation also checks the expected database volume name before starting the stack. If a volume already exists, creation fails closed with a clear stale-volume collision error. It does not silently attach, delete, or adopt the volume during creation; operator recovery must use an explicit lifecycle path.

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

Site removal volume cleanup is separate from DB-3 database-object drop. Site removal cleans the Site's owned data volume after explicit Site deletion confirmation. DB-3 `drop` removes physical MariaDB objects inside a running, verified stack and then metadata.

## Legacy Removal

`POST /api/databases/remove` is deprecated and metadata-only. New clients must use `POST /api/databases/<id>/drop` for physical deletion or `POST /api/databases/<id>/forget-metadata` for explicit metadata recovery.
