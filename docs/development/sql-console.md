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

Phase 5 added Adminer as the first replaceable `SqlConsoleProvider` and
introduced an on-demand Docker runtime boundary for temporary Adminer
containers. The Adminer provider starts containers on the selected site's
private Compose network only, does not publish host ports, and does not
place database credentials in Docker environment variables or command-line
arguments.

Phase 6 added admin-domain proxy route orchestration. API launch now starts
the Adminer provider container and installs a marked Nginx route for
`/sql-console/<launch_id>/`. The route uses Nginx `auth_request` against a
WebServer internal endpoint that validates the `HttpOnly` SQL Console launch
cookie. Direct browser navigation does not rely on the frontend
`X-Session-Token` header. Revoke removes the route and stops Adminer before
dropping the temporary MariaDB user.

The current implementation still does not expose Web UI controls for
Adminer.

## Ownership

| Capability | Owner |
|------------|-------|
| SQL Console launch session lifecycle | `libs/sqlconsole/SqlConsoleSessionManager` |
| SQL Console service boundary | `libs/sqlconsole/DatabaseSqlConsoleService` |
| SQL Console audit event formatting | `libs/sqlconsole/SqlConsoleAuditLogger` |
| SQL Console non-secret metadata | `libs/sqlconsole/SqlConsoleSessionStore` |
| SQL Console tool provider boundary | `libs/sqlconsole/SqlConsoleProvider` |
| Adminer temporary runtime lifecycle | `libs/sqlconsole/AdminerSqlConsoleProvider` |
| SQL Console admin-domain route config | `libs/proxy/NginxProxyProvider` |
| SQL Console launch-cookie route auth | `libs/api/WebServer` |
| Database lifecycle operations | `libs/database/DatabaseLifecycleService` |
| MariaDB operations | `libs/database/MariaDBProvider` |
| SQL Console temporary MariaDB users | `libs/database/MariaDBProvider` |
| ContainerCP admin authentication | `libs/auth/AuthService` |

SQL Console must not duplicate database lifecycle, dump/import,
credential rotation, core authentication, or runtime/proxy logic. Future
phases must extend the owning subsystem or provider instead of adding
logic to API handlers or Web UI code.

## Adminer Provider Runtime

`AdminerSqlConsoleProvider` is the first concrete `SqlConsoleProvider`.
It controls only the Adminer container lifecycle and does not own SQL
Console session state, MariaDB temporary users, reverse proxy routing, or
credential delivery.

Runtime rules:

- container names are derived from non-secret launch IDs as
  `ccp-sqlconsole-<launch-prefix>`,
- Adminer is started with vector-argv Docker execution,
- Adminer is attached to the target site private network
  `<compose-project>_containercp-site-<site_id>`,
- no `-p` or `--publish` host-port mapping is allowed,
- no database user, password, launch secret, service-account credential,
  or SQL content is passed through Docker environment variables,
  command-line arguments, or labels,
- the provider returns only a private upstream such as
  `http://ccp-sqlconsole-<launch-prefix>:8080` for future server-side
  proxy routing.

Credential handoff remains a later server-side proxy/internal-SSO phase.
The frontend must not receive credentials, and Adminer must not become
publicly reachable outside the authenticated admin-panel route.

## Proxy Route Authorization

SQL Console browser routing is authorized by the `ccp_sql_console_secret`
cookie that is set only after an authenticated launch API call. This is
intentional because top-level browser navigation to `/sql-console/<id>/`
does not carry the Web UI's `X-Session-Token` header.

The central Nginx proxy route:

- is installed under the configured admin-panel domain only,
- uses `auth_request` to call `/sql-console/internal/auth/<launch_id>` on
  the WebServer upstream,
- forwards only the SQL Console cookie to the auth endpoint,
- proxies the approved launch path to the private Adminer container
  upstream after auth succeeds,
- is removed during explicit revoke.

The WebServer auth endpoint only validates/touches the SQL Console launch
session. It does not return credentials, SQL content, provider diagnostics,
or public session JSON.

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
- Phase 5 added Adminer as the first `SqlConsoleProvider` and a private
  on-demand runtime boundary without host ports or credential exposure.
- Phase 6 added launch-cookie-authorized admin-domain reverse proxy
  routing and explicit route/container cleanup on revoke.
- Phase 7 adds Database GUI launch and revoke controls.

The frontend must never receive SQL Console credentials or launch
secrets. The public launch URL may contain only the non-secret launch ID.
