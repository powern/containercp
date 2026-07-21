# Databases Module v0.8 Threat Model

## Status

Focused threat model for the planned Databases module. This document must be reviewed before implementing database lifecycle APIs, import/export, backups, or Adminer integration.

## Security Objective

The Databases module must let an authenticated administrator manage each Site's single managed MariaDB application database without accidentally exposing credentials, targeting the wrong Site, destroying data, opening database administration tools to the public internet, or creating misleading backup/restore guarantees.

## Assets

| Asset | Impact if compromised |
|-------|-----------------------|
| Customer database data | Full application data breach or corruption |
| Database user password | Application database compromise |
| MariaDB root password | Full per-site MariaDB compromise |
| ContainerCP database service account | Privilege escalation path for lifecycle operations |
| ContainerCP SQLite storage | Credential disclosure and metadata tampering |
| Site `.env` files | Credential disclosure for DB, root DB, Redis, and site metadata |
| Migrated application configuration | Source of imported DB connection metadata and possible secrets |
| SQL dump files | Full data disclosure and possible credential leakage |
| Import files | Code/data injection path into customer DB |
| Adminer session/token | Interactive database compromise |
| Backup archives | Full site and database disclosure |
| Job logs and daemon logs | Secondary secret leakage path |

## Trust Boundaries

| Boundary | Risk |
|----------|------|
| Browser to ContainerCP Web UI | CSRF, session theft, malicious admin workstation |
| Web UI to REST API | Missing auth/authorization, request tampering |
| REST API to database services | Business logic in API lambdas, inconsistent validation |
| Database service to `CommandExecutor` | Command injection, password exposure in process list |
| Host to Docker Compose stack | Wrong site directory or wrong Compose project |
| ContainerCP storage to site `.env` | Secret drift and file permission exposure |
| Upload/import staging | Path traversal, oversized files, malicious SQL |
| Backup staging/archive | Secret/data exposure and partial backup claims |
| Reverse proxy to Adminer | Public exposure or auth bypass |

## Threat Actors

- Unauthenticated internet user reaching the admin panel or proxy route.
- Authenticated but careless administrator.
- Authenticated malicious administrator.
- Compromised customer application container in a site network.
- Local unprivileged host user attempting to read files or process arguments.
- Attacker controlling an uploaded SQL file.
- Attacker with access to backup archives.

## Key Threats and Required Mitigations

| Threat | Required mitigation |
|--------|---------------------|
| Database password leaks through API | Never serialize `db_password`, root password, option files, or one-time tokens |
| Password leaks through process list | Use MariaDB option files or stdin, never `--password=value` |
| Password leaks through logs/jobs | Central redaction before logging backend errors or job messages |
| SQL command injection through database name | Strict identifier validation and rejection; no shell strings |
| Wrong site/database targeted | Resolve through `Site -> Managed Database`, verify any supplied database ID belongs to the selected Site, require typed confirmation for destructive actions, log operation |
| Metadata-only delete is mistaken for physical drop | Rename/deprecate current endpoint or label it recovery-only |
| Adminer exposed publicly | No host port; route only through authenticated ContainerCP proxy |
| Adminer token replay | Short TTL, server-side token storage, selected-site managed-database scope, explicit revoke |
| Adminer credential exposure in URL | Never put credentials in URL, query string, fragment, or generated HTML link |
| Adminer credential exposure in browser runtime | Never put credentials in JavaScript variables, DOM attributes, local storage, or session storage |
| Expired Adminer session remains usable | Expiry must revoke token, proxy route, and temporary container; cleanup failures must be logged and retried |
| Import file path traversal | Canonical path containment checks and staging outside web roots |
| Import overwrites production data | Require explicit target and destructive confirmation |
| Oversized import causes disk exhaustion | Size limit before accepting upload and before decompressing |
| Malicious compressed import expands excessively | Compression ratio and decompressed-size limits |
| Backup succeeds without DB dump | Fail backup job if expected database dump fails |
| Restore imports into wrong site | Bind backup manifest to site ID/domain and require confirmation on mismatch |
| Compromised site container reaches other site DB | Per-site Docker networks; no shared database service in v0.8 |
| Root DB password overuse | Prefer least-privilege admin account or document root use as temporary risk |
| Service account overprivileged | Define grants per operation and audit elevated operations |
| Imported database treated as managed | Track management ownership separately and block destructive operations until explicit adoption |
| Missing imported credentials treated as missing database | Report `credentials_unavailable` and preserve site operation |
| Read-only verification mutates legacy site | DB-1 verification must not rewrite config, rotate passwords, change grants, or recreate users |
| Stale `.env` after password rotation | Update metadata and `.env` in one operation and restart dependent services if required |
| Secrets included in downloadable backup metadata | Redact manifests and document `.env` inclusion explicitly |
| Duplicate WordPress config parser causes drift | Reuse `WordPressConfigService`; do not add another `wp-config.php` parser or writer |

## API Threats

The API must be the only interface for database management. CLI and Web UI must call the same API behavior.

Required API controls:

- Use authenticated routes before database actions are exposed outside local development mode.
- Validate request fields before calling services.
- Keep handlers thin and delegate to `DatabaseLifecycleService`, `DatabaseDumpService`, and `DatabaseAdminService`.
- Use job IDs for long-running actions.
- Return sanitized errors with useful but non-sensitive messages.
- Require explicit confirmation strings for destructive actions.
- Target mutations through `Site -> Managed Database`; when a database ID appears in the API, verify it belongs to the selected Site before acting.
- Update `docs/api/API_REFERENCE.md` for every endpoint.
- Do not expose credentials in JavaScript-visible response fields, even transiently.

Negative tests required:

- Password field absent from every database response.
- Drop without confirmation fails.
- Drop with mismatched confirmation fails.
- Invalid ID fails without touching physical state.
- Mismatched site/database relation fails without touching physical state.
- Backend command failure returns sanitized error.

## Command Execution Threats

MariaDB lifecycle and dump/import require command execution. This is the highest implementation-risk boundary.

Required controls:

- Use `CommandExecutor` argument vectors.
- Do not use `std::system` or shell string concatenation for database commands.
- Do not pass passwords in argv.
- Create temporary option files with owner-only permissions.
- Delete option files on success and failure.
- Pin command working directory to the intended site directory or use explicit project directory arguments.
- Verify the target site directory belongs to the intended site.
- Sanitize stderr before returning it to API callers.

`MYSQL_ROOT_PASSWORD` is bootstrap-only. Runtime commands must authenticate with the dedicated service account or scoped database user. Any root/elevated credential use after bootstrap is break-glass and must be explicit, logged, and approval-gated.

DB-3 service-account policy for MariaDB 12.x is deliberately narrower than `ALL PRIVILEGES ON *.*`. The account may create schemas, create users, inspect `mysql.user`/`mysql.db`, and grant only the approved application privilege set on the Site's single managed schema. It must not receive `RELOAD`; DB-3 provider SQL must avoid unnecessary `FLUSH PRIVILEGES` after `GRANT`, `REVOKE`, `CREATE USER`, `ALTER USER`, or `DROP USER` statements.

DB-4 export/import uses the managed application user for data movement and keeps the service account for verification/metadata checks. `mariadb-dump` and `mariadb` receive credentials only through owner-only option files copied into the selected Site's MariaDB container. Import disables local infile with `--local-infile=0`.

Negative tests required:

- Database name with shell metacharacters is rejected.
- Username with SQL metacharacters is rejected.
- Command arguments do not include password values.
- Option file is removed after failed command.
- Provider errors classify safe privilege failures without exposing SQL password literals or option-file contents.

## Credential Storage Threats

Current state stores `db_password` in SQLite and writes credentials into `.env`. This is acceptable only as a known inherited risk for v0.7.0, not as a final security posture.

v0.8 minimum:

- Do not increase credential exposure.
- Do not add password reveal unless explicitly approved.
- Add password rotation before Adminer is considered complete.
- Document `.env` and SQLite credential storage clearly for operators.
- Ensure database backup archives do not accidentally create additional plaintext copies beyond the documented `.env` and dump artifacts.
- Treat credential lifetime as bounded by operation, token, or rotation policy. Long-lived credentials must be documented as residual risk.

Future hardening:

- Secret-at-rest encryption.
- Dedicated admin account instead of root for lifecycle operations.
- Per-operation temporary credentials.
- File permission validation for site `.env` and backup staging.

Imported myVestaCP credentials add a separate boundary. The resolver may inspect migrated site configuration to perform verification for the selected Site's application database, but normal API responses receive only availability and verification states. If credentials cannot be recovered safely, the API reports `credentials_unavailable`; it must not infer that the physical database is missing.

For WordPress sites, `WordPressConfigService` is the single owner of `wp-config.php` parsing and mutation. Database inventory, verification, adoption, and rotation must reuse that service and must not introduce duplicate config parsing or writing logic.

DB-1 imported database verification must be non-destructive. It may open a connection and run a safe read-only probe when credentials are available. It must not rotate passwords, modify grants, create users, revoke users, rewrite site configuration, or normalize nonstandard `user@host` values.

## Adminer Threats

Adminer must be treated as full database access.

Required controls before enabling:

- Disabled by default.
- Time-limited launch sessions.
- Server-side token maps to database credentials; browser receives only token or proxied session.
- Token scoped to user/session, node, nonce, database ID, site ID, and the selected Site's managed database.
- Token expiry and revoke path.
- Replay protection through nonce, TTL, and one active session policy per user/database where practical.
- No host port exposure.
- Reverse proxy route requires ContainerCP authentication.
- Audit log for launch, access, expiry, and revoke.
- Adminer image/file version is pinned and updateable.
- Cleanup after expiration removes token state, proxy route, and on-demand container.

Not allowed:

- Public `/adminer.php` in customer document root.
- Credentials in URL parameters.
- Credentials in browser history, JavaScript variables, local storage, session storage, DOM attributes, logs, or job messages.
- Shared long-lived Adminer login across sites.
- Database selection UI for multiple managed databases in v0.8.
- Adminer access for deleted or disabled databases.

## Import/Export Threats

Export risks:

- SQL dump discloses all database data.
- Dump command can lock or slow a busy database.
- Dump may be inconsistent if options are wrong.
- Dump artifact can remain in staging after failure.

Export mitigations:

- Use `mariadb-dump --single-transaction --quick` by default for InnoDB.
- Store artifacts outside web roots.
- Set restrictive file permissions.
- Clean staging on success/failure according to a documented retention policy.
- Require authenticated download with path containment checks.

Import risks:

- SQL file can drop tables or create malicious routines.
- Upload can exhaust disk.
- Import can target the wrong database.
- Error output can reveal secrets or paths.

Import mitigations:

- Size limits before upload acceptance and decompression.
- Explicit selected Site and managed database target.
- Destructive confirmation if overwrite/drop statements are detected or if policy requires it for all imports.
- Async job with sanitized diagnostics.
- Optional dry-run/inspect step in a future release.

DB-4 initial import policy accepts only uncompressed ContainerCP-generated `.sql` exports up to 5 MiB. Known database-switching, account-management, grant, definer, and local-infile constructs are rejected before execution. Import mode is execute/import into the existing database with a pre-import recovery export; partial MariaDB DDL failures are reported as manual recovery instead of claiming rollback.

## Backup/Restore Threats

DB-5 backup jobs create logical SQL dumps before archive finalization. Backup jobs fail when expected database dumps fail; restore jobs fail when database import fails or post-import verification fails.

Required controls:

- Backup manifest records database dump status.
- Backup job status reflects database dump failures.
- Site Backup flows as `Site Backup -> Managed Database Dump -> Archive`.
- Restore requires explicit confirmation before database import.
- Restore verifies backup payload checksums and target Site/database eligibility before import.
- Restore failure leaves diagnostics and does not silently mark success.
- Backup archive permissions are restrictive.

## Validation Checklist

- [ ] API responses contain no secrets.
- [ ] Command argv contains no passwords.
- [ ] Option files use restrictive permissions and are deleted.
- [ ] Invalid names are rejected before SQL construction.
- [ ] Drop requires typed confirmation and correct selected Site/managed database relation.
- [ ] Metadata-only removal is clearly separated from physical drop.
- [ ] Import staging rejects path traversal and oversized files.
- [ ] Export/download paths are contained under approved directories.
- [x] Backup fails if database dump fails.
- [ ] Adminer has no public host port.
- [ ] Adminer token expires and can be revoked.
- [ ] Adminer cleanup removes expired token, proxy route, and temporary container.
- [ ] Audit logs cover Adminer launch, access, expiry, revoke, cleanup failure, and cleanup retry.
- [ ] Full tests pass and VM validation is complete before production rollout.
- [ ] Imported database with valid existing credentials verifies without exposing the password.
- [ ] Imported database with missing credentials reports `credentials_unavailable`.
- [ ] Imported database with invalid credentials reports `connection_failed`.
- [ ] Imported database verification leaves site configuration unchanged.
- [ ] WordPress database inspection and rotation reuse `WordPressConfigService` and do not add another config parser.

## Residual Risks

- SQLite and `.env` still contain plaintext database credentials until a secret store or encryption design is implemented.
- Adminer remains a high-value target even when gated.
- Logical dump consistency depends on engine behavior and concurrent DDL.
- A malicious administrator can intentionally destroy data; v0.8 controls mainly prevent accidental or unauthenticated damage.
- Recovery from partial password rotation or import failure needs strong operator documentation.

## Required Review Before Implementation

- Architecture review.
- Security review of command execution and Adminer flow.
- Backup/restore review.
- API contract review.
- Validation VM plan review.
