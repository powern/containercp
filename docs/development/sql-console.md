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

The current implementation still does not expose REST APIs, Adminer
runtime, reverse proxy routes, persistent session metadata, or Web UI
controls.

## Ownership

| Capability | Owner |
|------------|-------|
| SQL Console launch session lifecycle | `libs/sqlconsole/SqlConsoleSessionManager` |
| SQL Console service boundary | `libs/sqlconsole/DatabaseSqlConsoleService` |
| SQL Console audit event formatting | `libs/sqlconsole/SqlConsoleAuditLogger` |
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
- Phase 3 persists only non-secret session metadata.
- Phase 4 adds thin REST API handlers.
- Phase 5 adds Adminer as the first `SqlConsoleProvider`.
- Phase 6 adds admin-domain reverse proxy routing.
- Phase 7 adds Database GUI launch and revoke controls.

The frontend must never receive SQL Console credentials or launch
secrets. The public launch URL may contain only the non-secret launch ID.
