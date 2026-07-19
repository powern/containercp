# WordPress Database Password Rotation v0.8 Threat Model

## Status

Focused threat model for planned WordPress database credential inspection and rotation. This document must be reviewed before implementing config mutation, password rotation, imported database adoption, Databases GUI actions, or Adminer launch flows.

## Security Objective

WordPress database credential management must let ContainerCP inspect and rotate credentials without exposing secrets, breaking migrated sites, modifying unsupported configs, rotating the wrong database user, or leaving MariaDB, metadata, `.env`, and `wp-config.php` out of sync.

## Assets

| Asset | Impact if compromised |
|-------|-----------------------|
| WordPress database password | Full application database compromise |
| MariaDB user and grants | Unauthorized data access or destructive operations |
| MariaDB root password | Full per-site database compromise |
| ContainerCP database service account | Privilege escalation for lifecycle and rotation operations |
| `wp-config.php` | Credential disclosure and application takeover path |
| Site `.env` | DB, root DB, Redis, and site metadata disclosure |
| ContainerCP SQLite storage | Stored credential and metadata disclosure |
| Migration backups and rollback files | Secret disclosure and stale credential exposure |
| Job and daemon logs | Secondary secret leakage path |
| Temporary option files | Process-safe credential transport that can leak if permissions or cleanup fail |
| WP-CLI runner context | Code execution and secret exposure if enabled later |

## Trust Boundaries

| Boundary | Risk |
|----------|------|
| REST API to rotation service | Missing authorization, wrong database ID, thin handler bypass |
| Rotation service to WordPress config service | Unsafe mutation of ambiguous config |
| Rotation service to MariaDB provider | Wrong target user, password exposure, SQL injection |
| Host filesystem to site directory | Path traversal, symlink attack, wrong site root |
| ContainerCP storage to `.env` and `wp-config.php` | Secret drift between multiple credential copies |
| Runtime command execution | Password in argv, stderr leakage, wrong container |
| Migration/imported config resolver | Treating imported unmanaged credentials as managed |
| Web UI/browser | Secret exposure in response, DOM, storage, URL, or logs |

## Threat Actors

- Unauthenticated internet user reaching the admin panel or proxy route.
- Authenticated but careless administrator.
- Authenticated malicious administrator.
- Compromised WordPress application or plugin.
- Local unprivileged host user attempting to read files or process arguments.
- Attacker controlling a migrated backup archive.
- Attacker with access to backup or rollback files.

## Key Threats And Mitigations

| Threat | Required mitigation |
|--------|---------------------|
| Password leaks through API | Never serialize old/new password, root password, option-file contents, or one-time tokens |
| Password leaks through process list | Use option files, stdin, or equivalent safe transport; never pass passwords as argv |
| Password leaks through logs | Redact known secret values before logging provider errors or job messages |
| Wrong database rotated | Resolve by database ID, verify site relation, verify current login before mutation |
| Wrong WordPress config edited | Canonicalize site root, reject paths outside site directory, ignore backups/temp files |
| Ambiguous config overwritten | Classify duplicates, dynamic expressions, includes, and conditionals as non-writable |
| Site broken by partial rotation | Saga with rollback for MariaDB, metadata, `.env`, and config file |
| Imported DB treated as managed | Track management ownership and require explicit adoption before destructive operations |
| Old credential revoked too early | Verify new connection before revoking or discarding old credential |
| `.env` and `wp-config.php` drift | Update all credential projections in one job and verify final state |
| Config backup exposed publicly | Store backups outside web root where possible; otherwise use inactive suffix and restrictive permissions |
| Temporary files left behind | Cleanup on success and failure; tests cover failed cleanup warnings |
| PHP syntax broken | Run `php -l` after write and restore original on failure |
| Malicious backup config causes path traversal | Archive extraction and config discovery must reject unsafe paths and symlinks |
| WP-CLI evaluates application code unexpectedly | Do not use WP-CLI for core mutation until runner isolation and command scope are approved |

## Config Parsing Threats

WordPress configuration is PHP code, not a simple key-value file. Regex replacement can be dangerous when definitions are conditional, duplicated, dynamically generated, or included from another file.

Required controls:

- Inspect before mutate.
- Mutate only direct literal constants in the first implementation.
- Treat multiple definitions of the same DB constant as ambiguous.
- Treat `getenv()`, `$_ENV`, `$_SERVER`, variables, concatenation, function calls, and conditionals as read-only unless a specific source handler is implemented.
- Detect include/require statements but do not follow them unless the path is static, contained, and explicitly supported.
- Preserve unrelated content.
- Refuse to edit if rendering would change unrelated text.

Negative tests:

- Duplicate `DB_PASSWORD` is rejected.
- `define('DB_PASSWORD', getenv('DB_PASSWORD'))` is rejected for direct-constant mutation.
- `if (!defined('DB_PASSWORD'))` is rejected or classified as unsupported.
- `require dirname(__FILE__) . '/secret.php'` is detected but not mutated in WP-3.
- Symlinked config path outside site root is rejected.

## Rotation Threats

Password rotation crosses multiple mutable systems. The highest risk is a failure after MariaDB accepts the new password but before WordPress can use it.

Required controls:

- Lock per database/site during rotation.
- Verify old credential first.
- Keep old credential in memory for compensation until success is final.
- Do not update metadata before provider mutation succeeds.
- Do not mark job successful before final connection verification.
- Restore old password if downstream updates fail.
- Mark `manual_intervention_required` if compensation fails and include redacted recovery details.

## Imported myVestaCP Threats

Imported sites can have working credentials in `wp-config.php` while ContainerCP metadata is incomplete or stale.

Required controls:

- Do not infer lifecycle ownership from the presence of a working WordPress connection.
- Do not copy imported passwords into SQLite during read-only inventory.
- Do not rotate imported credentials without explicit adoption or an approved imported-rotation operation.
- Preserve nonstandard `DB_HOST` values unless an approved migration step changes them.
- Report `credentials_unavailable` separately from `connection_failed`.
- Leave site files byte-for-byte unchanged during read-only DB-1 inspection.

## Runtime And Command Execution Threats

Required controls:

- Use `CommandExecutor` argument vectors.
- Do not use `std::system` or shell concatenation for new password or config commands.
- Pin execution to the intended site directory or pass explicit Compose file paths.
- Verify container labels or names match the target `site_id` before mutation.
- Redact stderr before returning it to API or job consumers.
- Prefer temporary option files with mode `0600` for MariaDB credentials.
- Remove temporary files on success and failure.

## API And Web UI Threats

Future API and UI must follow these rules:

- Rotation endpoint returns only a job ID and redacted status.
- No old or new password in JSON, HTML, JavaScript, DOM attributes, local storage, session storage, URLs, browser history, or downloadable logs.
- UI must show capability and state, not credentials.
- Destructive/adoption operations require explicit confirmation.
- Authentication and authorization must be addressed before exposing external Web UI rotation controls.

## Residual Risks

- v0.8 may still store credentials in SQLite and `.env`; this is an inherited risk until a secret store is designed.
- Rollback can fail if MariaDB is unavailable after partial mutation.
- Dynamic WordPress configs may require manual operator handling in v0.8.
- Backups may include `.env` or config files unless backup redaction/encryption is implemented.
- A compromised WordPress application can read its own DB credentials by design.

## Required Validation

- Unit tests for all supported and rejected config patterns.
- Unit tests proving public models never include passwords.
- Provider tests proving command argv does not contain passwords.
- Integration tests for success and compensation paths.
- Validation VM run on a disposable WordPress site.
- Manual review of logs and job output for secret leakage.
