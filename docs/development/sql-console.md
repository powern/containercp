# SQL Console Development Notes

## Purpose

The SQL Console subsystem owns interactive database-console launch
sessions. It is intentionally named around SQL Console, not Adminer,
because Adminer is only the first planned provider and must remain
replaceable by a future native SQL editor.

## Current Status

Phase 1 implemented the generic in-memory session foundation:

- `SqlConsoleSession`
- `SqlConsoleSessionManager`
- `DatabaseSqlConsoleService`
- `SqlConsoleAuditEvent`
- `SqlConsoleAuditLogger`

Phase 2 added temporary MariaDB user lifecycle support for SQL Console
launch sessions. The database provider owns the MariaDB user/grant/drop
operations, while `DatabaseSqlConsoleService` coordinates session
creation, temporary user provisioning, and explicit revoke cleanup.

Phase 3 added a SQL Console-owned non-secret metadata store for restart
cleanup. The store records launch/session identifiers, selected database
IDs, status, timestamps, database name, and temporary username only. It
does not persist launch secrets, secret digests, temporary passwords,
service-account credentials, SQL content, or provider diagnostics.

Phase 4 added thin REST API endpoints for SQL Console launch/status/revoke
and a token-guarded internal redeem endpoint for future providers. Public
launch responses contain only `launch_id`, `launch_url`, and public-safe
session metadata. The launch secret is delivered only as an `HttpOnly`,
`Secure`, `SameSite=Strict` cookie scoped to `/sql-console`.

The current implementation still does not expose REST APIs, Adminer
runtime, reverse proxy routes, or Web UI controls.

## Ownership

| Capability | Owner |
|------------|-------|
| SQL Console launch session lifecycle | `libs/sqlconsole/SqlConsoleSessionManager` |
| SQL Console service boundary | `libs/sqlconsole/DatabaseSqlConsoleService` |
| SQL Console audit event formatting | `libs/sqlconsole/SqlConsoleAuditLogger` |
| SQL Console non-secret metadata | `libs/sqlconsole/SqlConsoleSessionStore` |
| Database lifecycle operations | `libs/database/DatabaseLifecycleService` |
| MariaDB operations | `libs/database/MariaDBProvider` |
| SQL Console temporary MariaDB users | `libs/database/MariaDBProvider` |
| ContainerCP admin authentication | `libs/auth/AuthService` |

SQL Console must not duplicate database lifecycle, dump/import,
credential rotation, core authentication, or runtime/proxy logic. Future
phases must extend the owning subsystem or provider instead of adding
logic to API handlers or Web UI code.

## Session Model

Each launch session has:

- non-secret `launch_id`,
- hashed launch secret stored only server-side,
- selected `database_id` and `site_id`,
- administrator username and role,
- provider name,
- creation, redemption, idle-expiry, absolute-expiry, and revocation
  timestamps,
- status: `created`, `redeemed`, `expired`, or `revoked`.

The launch secret is returned only at creation time for future
server-only cookie transport. Public serialization never includes the
secret or its digest.

Temporary MariaDB usernames are generated as `ccp_sql_<launch-prefix>`
and are scoped to the selected database. Temporary passwords are
generated with `SecureRandom`, stored only in the in-memory server-side
session, and cleared from the session after successful explicit cleanup.
They must never be serialized to frontend JSON or logs.

## Persistent Metadata

`SqlConsoleSessionStore` persists restart-cleanup metadata in a
SQL Console-owned file format. This is intentionally separate from the
current SQLite business schema so the active storage migration gate and
schema version remain unchanged. The store exists only to support fail-
closed restart behavior and orphan cleanup of temporary MariaDB users.

Persisted fields are non-secret:

- `launch_id`, `database_id`, and `site_id`,
- administrator username and role,
- provider and status,
- database name and temporary username,
- creation, redemption, idle-expiry, absolute-expiry, last-seen, and
  revocation timestamps.

On daemon restart, active persisted sessions must not be restored as
usable browser sessions because launch secrets are not persisted. Recovery
loads the non-secret metadata, asks the caller to resolve the current
MariaDB target/service account from canonical site state, drops the
temporary MariaDB user, marks the session revoked, and clears temporary
user metadata from the store. If cleanup cannot complete, recovery fails
closed and reports `recovery_incomplete`.

## Expiry Rules

Phase 1 supports two expiry policies:

- absolute TTL: hard maximum lifetime from creation,
- idle TTL: maximum inactive lifetime after redemption.

Redemption is single-use by default. A redeemed session can be touched to
record activity and extend idle expiry, capped by the absolute expiry.

## Audit Rules

Audit events are formatted as safe key-value records under the
`sql_console` prefix. Sensitive fragments such as `password=...`,
`secret=...`, `token=...`, `credential=...`, and `cookie=...` are
redacted before logging.

Audit events must not contain database passwords, launch secrets,
temporary MariaDB passwords, cookie values, SQL contents, or raw provider
diagnostics.

## Future Phases

Future phases must preserve these boundaries:

- Phase 2 added temporary MariaDB user lifecycle in the MariaDB provider
  layer.
- Phase 3 added non-secret persisted metadata and restart cleanup.
- Phase 4 added thin REST API handlers and internal provider redemption.
- Phase 5 adds Adminer as the first `SqlConsoleProvider`.
- Phase 6 adds admin-domain reverse proxy routing.
- Phase 7 adds Database GUI launch and revoke controls.

The frontend must never receive SQL Console credentials or launch
secrets. The public launch URL may contain only the non-secret launch ID.
