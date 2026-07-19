# WordPress Database Password Rotation v0.8 Plan

## Status

Planning document only. Do not implement this plan until the WordPress config architecture and database lifecycle architecture are reviewed and approved.

## Purpose

WordPress database password rotation must update MariaDB, ContainerCP metadata, runtime environment, and WordPress application configuration without leaving the site broken or exposing the new password. This is a prerequisite for safe imported database adoption, future Databases GUI actions, Adminer launch design, and future ContainerCP-created WordPress provisioning.

## Scope

In scope:

- Rotate the database password for a WordPress site whose active config source is supported.
- Coordinate MariaDB password change, metadata update, `.env` update where applicable, `wp-config.php` update, service restart/reload, and verification.
- Compensate safely on failure.
- Return job status without returning the password.

Out of scope:

- Databases GUI implementation.
- Adminer deployment.
- WP-CLI deployment.
- WordPress core installation or updates.
- Rotation for arbitrary non-WordPress PHP apps.
- Secret-at-rest encryption.
- Automatic production rotation during planning.

## Proposed Component

### `DatabaseCredentialRotationService`

Proposed location: `libs/database/` with calls into `libs/wordpress/`.

Responsibilities:

- Resolve target database by database ID and verify site relation.
- Generate a new password using the approved generator.
- Ask the database provider to apply the new password.
- Update ContainerCP database metadata.
- Update site `.env` if it is part of the active runtime credential projection.
- Ask `WordPressConfigService` to update supported WordPress config sources.
- Restart or reload only services that must pick up the new credential.
- Verify application database connectivity.
- Compensate with the previous working credential if a later step fails.
- Emit redacted audit/job events.

Non-responsibilities:

- Rendering Web UI controls.
- Returning or revealing passwords.
- Parsing WordPress files directly.
- Implementing MariaDB SQL commands directly without provider boundary.

## Dependencies

Required before implementation:

- `WordPressConfigService` read-only inspection and source classification.
- `WordPressConfigService` atomic update for direct constants.
- MariaDB provider password-change operation using safe credential transport.
- Database view state model that distinguishes managed, imported, verification-required, and credentials-unavailable databases.
- Job execution path for long-running mutation.
- Redaction helper for logs, API errors, and job messages.

## Rotation Preconditions

The service must fail closed unless all required preconditions are true:

- Database ID exists.
- Database belongs to the expected site.
- Site directory resolves under `Config::sites_dir()`.
- MariaDB service for the site is running or can be reached by the provider.
- Current credential is available through managed metadata or an approved imported credential resolver.
- WordPress config inspection returns a supported mutable source.
- `DB_NAME`, `DB_USER`, `DB_PASSWORD`, and `DB_HOST` targets are unambiguous.
- A verification plan exists before mutation starts.
- A rollback plan has the old credential and config backup available in memory or controlled staging.

## Rotation Saga

The rotation should be implemented as a saga with explicit step state.

```text
prepare
  -> inspect_current_config
  -> verify_old_connection
  -> generate_new_password
  -> backup_application_config
  -> change_mariadb_password
  -> update_metadata
  -> update_env_projection
  -> update_wordpress_config
  -> restart_or_reload_runtime
  -> verify_new_connection
  -> finalize
```

Compensation path:

```text
on failure after change_mariadb_password
  -> restore_mariadb_old_password
  -> restore_metadata_old_password_if_changed
  -> restore_env_old_password_if_changed
  -> restore_wordpress_config_backup_if_changed
  -> restart_or_reload_runtime_if_needed
  -> verify_old_connection
  -> report_recovered_or_manual_intervention_required
```

## Step Details

### 1. Prepare

- Lock the database/site rotation target to prevent concurrent rotation or import operations.
- Resolve database ID, site ID, domain, site directory, and runtime service names.
- Start a job with redacted progress messages.

### 2. Inspect Current Config

- Call `WordPressConfigService.inspect(site_id)`.
- Reject `ambiguous`, `unsupported`, and `unknown` sources.
- Reject imported config unless the operation is part of an explicit adoption or rotation workflow with approved credential availability.

### 3. Verify Old Connection

- Use `MariaDBProvider.verify_connection()` with current credential.
- Use option files or equivalent safe credential transport.
- Do not pass passwords in argv.

### 4. Generate New Password

- Generate a new random password.
- Keep it in memory only until all projections are updated.
- Never log it.

### 5. Backup Application Config

- Ask `WordPressConfigService` to create or reserve a rollback backup.
- Store backup path only in job-private data.

### 6. Change MariaDB Password

- Apply password through `MariaDBProvider`.
- Avoid root in normal flow; use the approved database service account when available.
- If v0.8 initially needs root as a temporary bridge, it must be documented as break-glass risk and removed before Adminer is considered complete.

### 7. Update Metadata

- Update `DatabaseManager`/storage password field only after MariaDB accepts the new password.
- Keep old value available inside the job for rollback until final verification succeeds.

### 8. Update Env Projection

- Update site `.env` when that file is a current credential projection for the stack.
- Preserve unrelated environment values.
- Use atomic write and restrictive permissions.
- Do not regenerate unrelated Compose output.

### 9. Update WordPress Config

- Call `WordPressConfigService.update_database_credentials()`.
- The service performs atomic file replacement and PHP syntax validation.
- It must return a recoverable state if syntax validation fails.

### 10. Restart Or Reload Runtime

- Restart PHP when the application needs to pick up file or environment changes.
- Restart MariaDB only if provider evidence shows it is required for the password change.
- Use existing runtime abstractions instead of ad hoc Docker command strings.

### 11. Verify New Connection

- Verify login with the new database password through MariaDB provider.
- Verify WordPress/application view when available with a non-destructive probe.
- Do not use `wp db check` as the only verification in v0.8 because it can rely on external utilities and the loaded config path; it can be optional after WP-CLI integration is approved.

### 12. Finalize

- Remove temporary option files and staged secret files.
- Clear old password from transient state where practical.
- Mark job success.
- Emit audit event with database ID, site ID, actor, and result, but no credential values.

## Imported myVestaCP Sites

Imported sites require extra caution:

- Imported database records may not be managed by ContainerCP.
- The existing `wp-config.php` may contain credentials not stored in SQLite.
- Missing credentials must be reported as `credentials_unavailable`, not as a missing database.
- Rotation is allowed only after explicit adoption or an approved imported-rotation path.
- Old credentials must not be revoked until the new credential is verified through both MariaDB and WordPress config.

## Failure States

| Failure point | Required behavior |
|---------------|-------------------|
| Inspection fails | No mutation; return structured reason |
| Old connection fails | No mutation; report verification failure |
| MariaDB password change fails | No metadata or config changes; report sanitized provider error |
| Metadata update fails | Attempt to restore old MariaDB password; report recovered/manual state |
| `.env` update fails | Restore old MariaDB password and metadata |
| `wp-config.php` update fails | Restore old MariaDB password, metadata, and `.env` |
| PHP syntax check fails | Restore old config and credential projections |
| New connection verification fails | Restore old credential path and verify old connection |
| Compensation fails | Mark `manual_intervention_required` with redacted diagnostics |

## API Shape For Future Task

The future API should be job-based:

```text
POST /api/databases/<id>/rotate-password
```

Response:

```json
{
  "success": true,
  "data": {
    "job_id": 123,
    "status": "queued"
  }
}
```

The response must not include the old password, new password, option-file paths, or raw provider command output.

## Tests

Unit tests:

- Rotation rejects unknown database ID.
- Rotation rejects site/database mismatch.
- Rotation rejects unsupported WordPress config source.
- Rotation does not serialize passwords in job/API models.
- Rotation redacts provider errors.
- Saga records step states in order.
- Compensation runs only for steps that mutated state.

Integration tests:

- Successful rotation updates MariaDB login, metadata, `.env`, and `wp-config.php`.
- Old credential no longer works after success if policy expects revocation.
- Failure after MariaDB change restores old login.
- Failure after config update restores original `wp-config.php`.
- PHP restart is limited to the affected site.
- Imported site without available credentials returns `credentials_unavailable` and leaves files unchanged.

Validation:

- Full doctest suite.
- CTest.
- Zero compiler warnings.
- Disposable local MariaDB validation.
- Validation VM run on a non-production WordPress site.
