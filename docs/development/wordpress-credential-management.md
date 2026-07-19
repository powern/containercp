# WordPress Credential Management

## Purpose

This document describes the WordPress database credential-management foundation added for ContainerCP v0.8. It defines the supported configuration model, security boundaries, operator workflow, and residual risks for database password rotation.

The foundation is intentionally conservative: status inspection and rotation requests are available through REST API, CLI, and Site Details UI, but queued live rotation still fails closed until the remaining production wiring and validation are explicitly approved.

## Supported Scope

Supported for inspection:

- Existing ContainerCP sites with a resolvable site identity, including `site_id=0` system-site records when present.
- WordPress sites whose active `wp-config.php` is a regular file inside the resolved site root.
- Direct literal definitions for `DB_NAME`, `DB_USER`, `DB_PASSWORD`, and `DB_HOST`.
- Apache and nginx document-root mappings handled by `WordPressConfigService`.
- Public-safe API/UI display of source, mutability, status, DB name, DB user, DB host, password-present flag, and sanitized issues.

Supported for credential update by the shared updater:

- Exactly one direct literal definition for each targeted credential constant.
- Atomic same-directory replacement with protected temporary files.
- Mode preservation and root-only ownership preservation where supported.
- PHP syntax validation through an injected validator or runtime boundary.
- Rollback through the updater rollback handle on validation failure.

Unsupported and fail-closed:

- System/admin-panel sites whose resolved capabilities do not include an eligible WordPress database credential target.
- Missing, symlinked, backup, temporary, non-regular, or path-escaping `wp-config.php` files.
- Environment-backed, `$_ENV`, `$_SERVER`, variable, concatenated, helper-call, included, duplicate, conditional, or otherwise ambiguous credential definitions.
- Shared database users unless a future operator-approved policy explicitly handles the impact.
- Production credential mutation before explicit validation approval.

## Single Source Of Truth

Credential ownership is split by concern:

- Database metadata is owned by `DatabaseManager` and storage.
- WordPress config parsing, public status, path safety, and verifier request projection are owned by `WordPressConfigService` and `WordPressConfigDetector`.
- WordPress credential file mutation is owned by `WordPressConfigUpdater`.
- MariaDB credential operations are owned by `MariaDBCredentialProvider`.
- Rotation ordering, compensation, manual-recovery state, and redacted events are owned by `DatabaseCredentialRotationService`.
- API/CLI/Web UI are clients only and must not contain rotation business logic.

The migration importer now delegates WordPress credential parsing and credential replacement to the shared WordPress subsystem. Remaining migration-specific `wp-config.php` code handles archive discovery, backup/rollback, container path mapping, and trusted-proxy insertion; it is not a credential parser or credential replacement owner.

## Operator Workflow

Use only an approved migrated test site until live rotation is explicitly enabled.

1. Inspect the site in the Site Details page or call `GET /api/wordpress/database-credentials/status?site_id=N`.
2. Confirm the response reports a supported direct-constant source and mutable status.
3. Confirm the database record belongs to the same site and that the database user is not shared with another application.
4. Queue rotation through `POST /api/wordpress/database-credentials/rotate` with `site_id`, `database_id`, and typed domain confirmation, or through `containercp wordpress rotate-db-password <site_id> <database_id> --confirm <domain>`.
5. Track the returned job id through `GET /api/jobs?id=N` or the Site Details job polling UI.
6. Treat `completed` as success only when the rotation service has verified MariaDB access with the new password, verified WordPress/PHP DB access, verified site health, and persisted metadata.
7. Treat `failed`, `compensated`, or `manual_recovery_required` as operator-review states; do not retry blindly.

Current v0.8 foundation behavior: API/CLI/UI can queue a job, but live dependencies remain intentionally unwired, so jobs fail safely rather than changing production credentials.

## Compensation And Manual Recovery

The rotation saga mutates state only after preflight inspection and old-credential verification succeed. If a later step fails after MariaDB mutation, it performs one compensation attempt:

- restore the old MariaDB password;
- restore WordPress config if it was changed;
- reapply or restore runtime state when needed;
- verify old database access again;
- report `compensated` only when rollback succeeds.

If compensation cannot complete, the service reports `manual_recovery_required` with redacted diagnostics. Operators must then inspect the approved test site directly, compare MariaDB user state, WordPress config contents, and site runtime state, and restore the old known-good credential from approved secret sources. ContainerCP must not silently report success for partial rotations.

## Secret Handling Rules

The following values must never appear in API responses, CLI output, Web UI DOM/storage, URLs, logs, job messages, command argv, or diagnostic strings:

- raw `DB_PASSWORD`;
- generated replacement passwords;
- MariaDB root password;
- provider defaults-file contents;
- SQL containing credentials;
- command stderr/stdout that may contain secrets.

Password presence may be reported as a boolean. DB name, DB user, and DB host may be shown because they are operational metadata, not password values.

MariaDB provider secret transport uses protected stdin bundles and in-container temporary option/SQL files. Passwords are not placed in shell command strings or process argv.

## Threat Model

Primary threats addressed:

- Path traversal or symlink attacks against `wp-config.php`.
- Accidental mutation of backup/temp/ambiguous config files.
- Secret disclosure through logs, jobs, API, CLI, browser state, command argv, or provider diagnostics.
- Partial rotation that leaves MariaDB and WordPress out of sync.
- Duplicate credential parser/update logic diverging between migration and rotation paths.
- Operator mistakes through missing typed confirmation or unsupported config states.

Residual risks:

- Current storage still persists database passwords in plaintext SQLite/TXT metadata and site `.env` files.
- Existing site `.env` bootstrap secrets remain outside this rotation foundation until a dedicated secret store and `.env` owner are approved.
- Exact narrow MariaDB grant requirements require validation against the deployed MariaDB version before live rotation.
- No production browser automation exists for the Site Details workflow; coverage is unit/static validation plus manual operator review.
- Queueing exists before live dependency wiring, so current jobs fail closed by design.

## Validation Requirements Before Live Enablement

Before any real production rotation is enabled, validate on one approved migrated test site:

- clean configure and rebuild with zero compiler warnings;
- full doctest suite and CTest;
- focused WordPress, database, API, command, daemon, and migration tests;
- static secret-leak checks for new API/CLI/UI/job surfaces;
- manual API status inspection proving no password fields are exposed;
- manual Site Details inspection proving no passwords are visible or stored in browser state;
- one controlled test rotation with MariaDB, WordPress/PHP, and HTTP/site health verification;
- one forced failure after mutation proving compensation or manual-recovery reporting.

Do not run this validation against production sites, production databases, production `wp-config.php`, or production credentials without explicit operator approval.
