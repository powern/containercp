# Databases Module v0.7.1 Threat Model

## Status

Focused threat model for the planned Databases module. This document must be reviewed before implementing database lifecycle APIs, import/export, backups, or Adminer integration.

## Security Objective

The Databases module must let an authenticated administrator manage per-site MariaDB databases without accidentally exposing credentials, destroying the wrong data, opening database administration tools to the public internet, or creating misleading backup/restore guarantees.

## Assets

| Asset | Impact if compromised |
|-------|-----------------------|
| Customer database data | Full application data breach or corruption |
| Database user password | Application database compromise |
| MariaDB root password | Full per-site MariaDB compromise |
| ContainerCP SQLite storage | Credential disclosure and metadata tampering |
| Site `.env` files | Credential disclosure for DB, root DB, Redis, and site metadata |
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
| Wrong database dropped | Resolve by database ID, verify site relation, require typed confirmation, log operation |
| Metadata-only delete is mistaken for physical drop | Rename/deprecate current endpoint or label it recovery-only |
| Adminer exposed publicly | No host port; route only through authenticated ContainerCP proxy |
| Adminer token replay | Short TTL, server-side token storage, one-site scope, explicit revoke |
| Adminer credential exposure in URL | Never put credentials in URL, query string, fragment, or generated HTML link |
| Import file path traversal | Canonical path containment checks and staging outside web roots |
| Import overwrites production data | Require explicit target and destructive confirmation |
| Oversized import causes disk exhaustion | Size limit before accepting upload and before decompressing |
| Malicious compressed import expands excessively | Compression ratio and decompressed-size limits |
| Backup succeeds without DB dump | Fail backup job if expected database dump fails |
| Restore imports into wrong site | Bind backup manifest to site ID/domain and require confirmation on mismatch |
| Compromised site container reaches other site DB | Per-site Docker networks; no shared MariaDB service in v0.7.1 |
| Root DB password overuse | Prefer least-privilege admin account or document root use as temporary risk |
| Stale `.env` after password rotation | Update metadata and `.env` in one operation and restart dependent services if required |
| Secrets included in downloadable backup metadata | Redact manifests and document `.env` inclusion explicitly |

## API Threats

The API must be the only interface for database management. CLI and Web UI must call the same API behavior.

Required API controls:

- Use authenticated routes before database actions are exposed outside local development mode.
- Validate request fields before calling services.
- Keep handlers thin and delegate to `DatabaseLifecycleService`, `DatabaseDumpService`, and `DatabaseAdminService`.
- Use job IDs for long-running actions.
- Return sanitized errors with useful but non-sensitive messages.
- Require explicit confirmation strings for destructive actions.
- Use database IDs for mutation endpoints and include site relation checks.
- Update `docs/api/API_REFERENCE.md` for every endpoint.

Negative tests required:

- Password field absent from every database response.
- Drop without confirmation fails.
- Drop with mismatched confirmation fails.
- Invalid ID fails without touching physical state.
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

Negative tests required:

- Database name with shell metacharacters is rejected.
- Username with SQL metacharacters is rejected.
- Command arguments do not include password values.
- Option file is removed after failed command.

## Credential Storage Threats

Current state stores `db_password` in SQLite and writes credentials into `.env`. This is acceptable only as a known inherited risk for v0.7.0, not as a final security posture.

v0.7.1 minimum:

- Do not increase credential exposure.
- Do not add password reveal unless explicitly approved.
- Add password rotation before Adminer is considered complete.
- Document `.env` and SQLite credential storage clearly for operators.
- Ensure database backup archives do not accidentally create additional plaintext copies beyond the documented `.env` and dump artifacts.

Future hardening:

- Secret-at-rest encryption.
- Dedicated admin account instead of root for lifecycle operations.
- Per-operation temporary credentials.
- File permission validation for site `.env` and backup staging.

## Adminer Threats

Adminer must be treated as full database access.

Required controls before enabling:

- Disabled by default.
- Time-limited launch sessions.
- Server-side token maps to database credentials; browser receives only token or proxied session.
- Token scoped to database ID and site ID.
- Token expiry and revoke path.
- No host port exposure.
- Reverse proxy route requires ContainerCP authentication.
- Audit log for launch, access, expiry, and revoke.
- Adminer image/file version is pinned and updateable.

Not allowed:

- Public `/adminer.php` in customer document root.
- Credentials in URL parameters.
- Shared long-lived Adminer login across sites.
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
- Explicit target database.
- Destructive confirmation if overwrite/drop statements are detected or if policy requires it for all imports.
- Async job with sanitized diagnostics.
- Optional dry-run/inspect step in a future release.

## Backup/Restore Threats

The current tar backup does not create logical SQL dumps. If v0.7.1 presents backups as database-aware, backup jobs must fail when DB dumps fail.

Required controls:

- Backup manifest records database dump status.
- Backup job status reflects database dump failures.
- Restore requires explicit confirmation before database import.
- Restore verifies backup/site identity before import.
- Restore failure leaves diagnostics and does not silently mark success.
- Backup archive permissions are restrictive.

## Validation Checklist

- [ ] API responses contain no secrets.
- [ ] Command argv contains no passwords.
- [ ] Option files use restrictive permissions and are deleted.
- [ ] Invalid names are rejected before SQL construction.
- [ ] Drop requires typed confirmation and correct site relation.
- [ ] Metadata-only removal is clearly separated from physical drop.
- [ ] Import staging rejects path traversal and oversized files.
- [ ] Export/download paths are contained under approved directories.
- [ ] Backup fails if database dump fails.
- [ ] Adminer has no public host port.
- [ ] Adminer token expires and can be revoked.
- [ ] Full tests pass and VM validation is complete before production rollout.

## Residual Risks

- SQLite and `.env` still contain plaintext database credentials until a secret store or encryption design is implemented.
- Adminer remains a high-value target even when gated.
- Logical dump consistency depends on engine behavior and concurrent DDL.
- A malicious administrator can intentionally destroy data; v0.7.1 controls mainly prevent accidental or unauthenticated damage.
- Recovery from partial password rotation or import failure needs strong operator documentation.

## Required Review Before Implementation

- Architecture review.
- Security review of command execution and Adminer flow.
- Backup/restore review.
- API contract review.
- Validation VM plan review.
