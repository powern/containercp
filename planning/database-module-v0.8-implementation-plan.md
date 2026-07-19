# Databases Module v0.8 Implementation Plan

## Status

Planning document only. Do not implement this plan until the architecture is reviewed and approved.

## Constraints

- Do not modify production installations as part of architecture approval.
- Do not change SQLite schema until a concrete implementation task is opened.
- Do not modify generated Docker Compose output until lifecycle/API work requires it and tests are ready.
- Do not deploy Adminer before the authenticated launch and threat model controls exist.
- Do not add GUI behavior before REST API behavior exists.
- Do not place database business logic in CLI handlers, Web UI code, or API lambdas.

## Phase 0: Approval Gate

- [ ] Review `planning/database-module-v0.8-architecture.md`.
- [ ] Review `planning/database-module-v0.8-open-source-review.md`.
- [ ] Review `planning/database-module-v0.8-threat-model.md`.
- [x] Support one site with many databases; never assume exactly one database per site.
- [x] Use rotate/replace as the normal password workflow; password reveal is not required for normal administration.
- [ ] Decide whether existing `POST /api/databases/remove` is deprecated, repurposed as metadata-only recovery, or replaced.
- [ ] Create an Architecture Proposal if this work is treated as an Epic.

Exit criteria:

- Architecture owner approves scope.
- Security decisions are recorded.
- VM validation plan is identified.

## Phase 1: Read-Only Foundation

Purpose: enrich inventory without changing physical database state.

Backend tasks:

- [ ] Add `DatabaseView` model under `libs/database/` or `libs/api/` according to existing view-service conventions.
- [ ] Add `DatabaseViewService` that joins database records to sites by `site_id`.
- [ ] Reuse `SiteRuntimeManager` or `RuntimeActionExecutor` for MariaDB runtime status.
- [ ] Update `GET /api/databases` to return enriched records through the view service.
- [ ] Add database detail response data without exposing secrets.
- [ ] Return site domain alongside internal `site_id`.
- [ ] Return engine profile/version and size fields.
- [ ] Surface restart capability through existing `restart-db` runtime behavior.
- [ ] Surface database logs capability through existing runtime/log behavior where available.
- [ ] Keep `db_password` out of every response.
- [ ] Update `docs/api/API_REFERENCE.md` for the response contract.

Web UI tasks:

- [ ] Show domain instead of only raw `site_id`.
- [ ] Add database detail view.
- [ ] Show runtime status, engine profile/version, and size.
- [ ] Add restart button using the existing runtime restart DB API.
- [ ] Add logs link/view only through existing backend log APIs.
- [ ] Add safe delete confirmation UI while preserving current metadata-only delete semantics.
- [ ] Label delete behavior clearly as metadata-only until physical lifecycle is implemented.

Tests:

- [ ] Unit test enriched response with known site relation.
- [ ] Unit test missing site relation returns a clear state.
- [ ] Unit test JSON output does not contain `db_password`, `DB_PASSWORD`, `MYSQL_ROOT_PASSWORD`, or password values.
- [ ] Runtime mapping tests remain green for `ServiceRole::Database` and `restart-db`.
- [ ] API/UI tests prove DB-1 does not create, drop physically, import, export, rotate passwords, or deploy Adminer.

Validation:

- [ ] Full doctest suite passes.
- [ ] CTest passes.
- [ ] No compiler warnings.

Exit criteria:

- Databases API is richer but still read-only for physical state.
- Web UI only consumes DB-1 read-only API/runtime behavior and does not change physical delete semantics.

## Phase 2: Safe Physical Lifecycle Service

Purpose: create/drop/verify MariaDB objects from backend services.

Backend tasks:

- [ ] Add name validation for MariaDB database names and user names.
- [ ] Add SQL identifier helper that rejects unsupported names instead of broad escaping.
- [ ] Add `MariaDBProvider` command wrapper using `CommandExecutor` argument vectors.
- [ ] Add provider/profile boundary so future engines extend providers instead of rewriting API or storage.
- [ ] Add ContainerCP database service account bootstrap and runtime-auth design.
- [ ] Add temporary option-file handling for credentials with owner-only permissions.
- [ ] Add `DatabaseLifecycleService` with create, verify, and drop operations.
- [ ] Make normal drop remove physical database, user/grants, then metadata.
- [ ] Preserve metadata-only removal as a clearly named recovery operation if needed.
- [ ] Add rollback for partial create failures.
- [ ] Add audit log entries for create, drop, verify, and repair.
- [ ] Ensure `MYSQL_ROOT_PASSWORD` is bootstrap-only and not used for normal runtime operations.

REST API tasks:

- [ ] Add `POST /api/databases` for create.
- [ ] Add `POST /api/databases/<id>/drop` for destructive drop with confirmation.
- [ ] Add `POST /api/databases/<id>/verify` for metadata/physical state checks.
- [ ] Return `202 Accepted` with job ID for operations that touch MariaDB.
- [ ] Return sanitized backend errors.
- [ ] Update `docs/api/API_REFERENCE.md`.

Tests:

- [ ] Unit test validation rejects dangerous names.
- [ ] Unit test provider constructs no shell strings.
- [ ] Unit test option-file lifecycle creates and removes files safely.
- [ ] Integration test create creates database, user, and grants in disposable MariaDB.
- [ ] Integration test drop removes physical objects and metadata.
- [ ] Integration test partial create failure rolls back physical objects and metadata.
- [ ] API test verifies destructive drop requires confirmation.

Validation:

- [ ] Disposable local Compose/MariaDB validation passes.
- [ ] Full doctest suite passes.
- [ ] CTest passes.
- [ ] No compiler warnings.

Exit criteria:

- The API can safely create, verify, and drop MariaDB databases.
- Existing site create/remove behavior is not regressed.

## Phase 3: Password Rotation and Credential Hygiene

Purpose: reduce the risk of static credentials and avoid accidental exposure.

Backend tasks:

- [ ] Add `rotate_password(database_id)` in `DatabaseLifecycleService`.
- [ ] Update MariaDB user password, `DatabaseManager` metadata, and site `.env` consistently.
- [ ] Restart only required containers after `.env` update if needed.
- [ ] Redact passwords from logs, job messages, and API errors.
- [ ] Ensure service-account grants are sufficient for rotation without normal root authentication.
- [ ] Keep break-glass password recovery optional, disabled by default, and approval-gated if implemented.

REST API tasks:

- [ ] Add `POST /api/databases/<id>/rotate-password`.
- [ ] Return job ID and rotation status, never the new password unless a break-glass policy is approved.

Tests:

- [ ] Unit test password redaction helper.
- [ ] Integration test rotation updates MariaDB login.
- [ ] Integration test rotation updates metadata and `.env`.
- [ ] Negative test rotation failure leaves a recoverable state and logs clear diagnostics.

Exit criteria:

- Password rotation is safe enough for Adminer launch design.

## Phase 4: Logical Export and Import

Purpose: provide portable SQL dump workflows and prepare backup integration.

Backend tasks:

- [ ] Add `DatabaseDumpService`.
- [ ] Export with `mariadb-dump` through `MariaDBProvider`.
- [ ] Use `--single-transaction` and `--quick` by default.
- [ ] Avoid command-line passwords by using option files.
- [ ] Write dumps to staging paths outside web roots.
- [ ] Add import staging with file extension, size, path, and ownership checks.
- [ ] Import with `mariadb` client through `MariaDBProvider`.
- [ ] Persist job diagnostics without including secrets.

REST API tasks:

- [ ] Add `POST /api/databases/<id>/export`.
- [ ] Add `POST /api/databases/<id>/import`.
- [ ] Add download endpoint only after authentication and path containment checks are in place.
- [ ] Update `docs/api/API_REFERENCE.md`.

Tests:

- [ ] Integration test export creates a valid SQL dump.
- [ ] Integration test import restores expected rows into a disposable database.
- [ ] Negative test rejects path traversal import paths.
- [ ] Negative test rejects oversized import files.
- [ ] Negative test sanitizes MariaDB client errors.

Exit criteria:

- SQL export/import works without exposing passwords or writing under public paths.

## Phase 5: Backup Integration

Purpose: make site backups database-aware.

Backend tasks:

- [ ] Extend backup workflow to call `DatabaseDumpService` before tar archive creation.
- [ ] Store database dump artifacts under backup staging.
- [ ] Include dump metadata in backup record or manifest.
- [ ] On restore, import SQL dumps only after explicit confirmation.
- [ ] Fail the job if any database dump/import fails.
- [ ] Document whether `.env` is included and how secrets are handled.

Tests:

- [ ] Backup test includes database dump for site databases.
- [ ] Restore test imports database dump successfully.
- [ ] Negative test failed dump marks backup failed.
- [ ] Negative test failed import marks restore failed.

Exit criteria:

- Backup claims are accurate: site backup includes logical database dump and restore can import it.

## Phase 6: Adminer Controlled Launch

Purpose: add database admin UI without public exposure or credential leakage.

Backend tasks:

- [ ] Add `DatabaseAdminService`.
- [ ] Add per-site Adminer enable/disable state if approved.
- [ ] Add short-lived admin-session token storage.
- [ ] Document Adminer deployment alternatives in implementation notes, with on-demand temporary container as the default.
- [ ] Implement on-demand temporary Adminer unless implementation evidence justifies a safer alternative.
- [ ] Expose Adminer only through authenticated ContainerCP proxy routes.
- [ ] Revoke and clean up Adminer sessions after expiry.
- [ ] Log session creation, access, and revocation.
- [ ] Ensure credentials never enter URLs, browser history, JavaScript variables, local storage, logs, or job messages.

REST API tasks:

- [ ] Add `POST /api/databases/<id>/admin-session`.
- [ ] Add revoke endpoint.
- [ ] Add status endpoint for Adminer availability.
- [ ] Update `docs/api/API_REFERENCE.md`.

Web UI tasks:

- [ ] Add Adminer launch button only after API returns capability.
- [ ] Display expiry and revoke button.
- [ ] Avoid embedding credentials in generated links.
- [ ] Handle expired sessions cleanly.

Tests:

- [ ] API test unauthenticated launch is rejected after auth policy exists.
- [ ] API test token expiry prevents reuse.
- [ ] API test launch response contains no database password.
- [ ] Integration test Adminer service is not exposed on a host port.

Exit criteria:

- Adminer is available only through authenticated, time-limited, auditable, on-demand launch flow.

## Phase 7: Web UI Completion

Purpose: replace the metadata-only Databases page with safe API clients.

UI tasks:

- [ ] Show enriched database list with site domain, runtime status, engine, enabled state, and verification state.
- [ ] Add create flow calling `POST /api/databases`.
- [ ] Add drop flow with typed confirmation and job polling.
- [ ] Add verify flow.
- [ ] Add export/import flows with job progress.
- [ ] Add password rotation flow if approved.
- [ ] Add Adminer launch/revoke flow if Phase 6 is complete.

Tests:

- [ ] UI smoke test load databases page.
- [ ] UI test destructive drop requires typed confirmation.
- [ ] UI test import/export job polling states.
- [ ] Manual mobile and desktop validation.

Exit criteria:

- GUI follows backend capabilities and does not create independent business logic.

## Phase 8: Documentation and Release Validation

Documentation tasks:

- [ ] Update `docs/api/API_REFERENCE.md`.
- [ ] Update runtime/development docs if ownership changes.
- [ ] Update `planning/project-status.md`.
- [ ] Update `CHANGELOG.md` with commit hash, validation, user-visible behavior, and known risks.
- [ ] Add operator guide for database import/export, backups, and Adminer launch.

Validation tasks:

- [ ] Clean configure.
- [ ] Clean build with zero compiler warnings.
- [ ] Full doctest suite.
- [ ] CTest.
- [ ] Disposable local MariaDB lifecycle validation.
- [ ] Validation VM deployment.
- [ ] Real product validation on a non-production site.
- [ ] Code review and security review.

Exit criteria:

- v0.8 Databases module can be released without misleading users about backup, delete, or admin-tool safety.

## Implementation Risks

- Physical drop can destroy customer data if confirmation, site relation, or database lookup is wrong.
- Passwords can leak through process lists if MariaDB tools receive password arguments.
- Adminer can become a full database compromise path if exposed publicly.
- Import can corrupt application data if restored into the wrong target.
- Current tar backup can give false confidence unless logical database dumps are added.
- Updating `.env` without restarting dependent services can leave application credentials stale.
- Existing metadata-only endpoint semantics can surprise users if not renamed or clearly constrained.

## Recommended First Task

Start with Phase 1 only: add `DatabaseViewService`, update `GET /api/databases`, add no physical lifecycle behavior, and prove no secrets are returned. This gives immediate UI/API improvement with low data-loss risk and establishes the service pattern for later phases.
