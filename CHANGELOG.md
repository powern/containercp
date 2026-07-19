# Changelog

All notable changes to ContainerCP are documented here.

Format: date | commit | summary

---

## 2026-07-19 | `this commit` | Web — Harden WordPress credential rotation workflow

**Summary:** Added WP-R9 Site Details UI hardening for WordPress credential rotation. The WordPress Database Credentials card now displays separate config and backend target status badges, consumes the backend-resolved `database_id` only from the status response, removes first-database selection, disables rotation unless both credential inspection and target resolution are safe, and shows precise disabled reasons for unsupported configs or unresolved targets.

**Files changed:** `web/app.js`, `tests/test_api.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** Operators now see whether the WordPress config is supported separately from whether ContainerCP resolved an exact database target. The rotate button is disabled until both are safe, preventing accidental first-database rotation on multi-database sites.

**Validation:** `node --check web/app.js` passed. Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1` and no compiler warnings. Focused tests passed for `*API*` (`18` cases, `73` assertions), `*WordPress*` (`64` cases, `379` assertions), and `*DatabaseCredentialRotation*` (`30` cases, `280` assertions). Full CTest (`1/1`) and `git diff --check` passed.

**Known risks:** No browser automation or visual regression harness exists; coverage is JS syntax and static UI assertions. Documentation readiness cleanup and final validation remain scheduled for WP-R10 and WP-R11.

---

## 2026-07-19 | `this commit` | API — Harden credential rotation endpoints

**Summary:** Added WP-R8 queue and endpoint safety hardening for WordPress credential rotation. `DatabaseCredentialRotationJobService` now rejects empty, overlong, or control-character confirmation strings before resource lookup or job creation. Rotation jobs continue to use internally created immutable job ids and generic redacted job messages; new regression coverage verifies unsafe confirmations create no jobs and async rotation failures do not store secret-bearing dependency messages. The rotate endpoint keeps backend target resolution before queueing and returns bounded error codes/messages without echoing confirmations or credentials.

**Files changed:** `libs/database/DatabaseCredentialRotationJobService.cpp`, `tests/test_database_credential_rotation.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** Invalid confirmation strings are rejected earlier with `confirmation_invalid`. Failed credential rotation jobs continue to show generic redacted messages only.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1` and no compiler warnings. Focused tests passed for `*DatabaseCredentialRotationJobService*,*DatabaseCredentialRotation*` (`30` cases, `280` assertions), `*API*` (`18` cases, `73` assertions), `*database*` (`54` cases, `485` assertions), and `*WordPress*` (`64` cases, `376` assertions). Full CTest (`1/1`) and `git diff --check` passed.

**Known risks:** This stage does not introduce a new role/permission model; endpoints remain protected by the existing `ApiServer` authentication middleware. UI safety-state presentation, documentation readiness cleanup, and final validation remain scheduled for WP-R9 through WP-R11.

---

## 2026-07-19 | `this commit` | WordPress — Harden runtime credential verification

**Summary:** Added WP-R7 hardening for `WordPressRuntimeVerifier`. Runtime verification now rejects incomplete requests, relative host paths, document roots outside the compose project, unsafe PHP service names, unsafe container document roots, and config paths outside the document root before constructing any Docker command. Documentation now explicitly states that verifier execution still loads the selected site's active `wp-config.php`, so the trust boundary is scoped execution in the selected site container, not a PHP sandbox.

**Files changed:** `libs/wordpress/WordPressRuntimeVerifier.cpp`, `tests/test_wordpress_runtime_verifier.cpp`, `docs/development/wordpress-credential-management.md`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No new UI/API behavior. Unsupported or unsafe runtime verification requests fail closed with redacted diagnostics before command execution.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1` and no compiler warnings. Focused verifier tests passed with `build-wp0/tests/containercp_tests -tc="*WordPressRuntimeVerifier*"` (`8` cases, `37` assertions), `*WordPress*` (`64` cases, `376` assertions), `*DatabaseCredentialRotation*` (`28` cases, `270` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest (`1/1`) and `git diff --check` passed.

**Known risks:** The verifier intentionally executes site-controlled `wp-config.php` inside the selected PHP container to test real WordPress database access. Request validation narrows what can be executed, but compromised site PHP remains outside this verifier's trust model. Endpoint/job safety, UI polish, documentation readiness language, and final validation remain scheduled for WP-R8 through WP-R11.

---

## 2026-07-19 | `this commit` | WordPress — Harden credential rotation compensation

**Summary:** Added WP-R6 compensation hardening for WordPress database credential rotation. The rotation saga now verifies restored WordPress/PHP database access, restored site health, and restored credential metadata after rollback before reporting `rotation_compensated`. Failures in any restored-state verification now return `manual_recovery_required`. The concrete adapter now restores in-memory database metadata to the old password when metadata persistence fails and keeps compensation context until restored metadata consistency has been verified.

**Files changed:** `libs/database/DatabaseCredentialRotationService.h`, `libs/database/DatabaseCredentialRotationService.cpp`, `libs/database/DatabaseCredentialRotationAdapter.h`, `libs/database/DatabaseCredentialRotationAdapter.cpp`, `tests/test_database_credential_rotation.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** Failed rotations after mutation now report safe rollback only after MariaDB, WordPress/PHP, site health, and metadata consistency checks all pass. If restored-state verification fails, operators receive `manual_recovery_required` instead of a misleading compensated result.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1` and no compiler warnings. Focused tests passed for `*DatabaseCredentialRotation*` (`28` cases, `270` assertions), `*database*` (`52` cases, `475` assertions), `*WordPress*` (`61` cases, `365` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest (`1/1`) and `git diff --check` passed.

**Known risks:** Metadata persistence is still not a durable multi-resource transaction across MariaDB, filesystem, and storage; this stage makes rollback verification stricter and restores in-memory metadata on persistence failure, but real crash-recovery semantics remain outside this foundation. Runtime verifier trust-boundary hardening, endpoint/job safety, UI polish, documentation readiness language, and final validation remain scheduled for WP-R7 through WP-R11.

---

## 2026-07-19 | `this commit` | WordPress — Resolve exact database credential target

**Summary:** Added WP-R5 backend-owned target resolution for WordPress database credential rotation. New `WordPressDatabaseCredentialResolver` combines WordPress credential inspection with database metadata and resolves exactly one enabled database record for the same site whose `DB_NAME` and `DB_USER` match the active `wp-config.php` and whose host is the managed site MariaDB service. The status API now returns target availability, resolved `database_id`, target status, and target message. The rotate API rejects submitted `database_id` values that do not match the backend-resolved WordPress target. The rotation adapter now uses the same resolver before any mutation, and the Site Details UI no longer selects `siteDatabases[0]` for WordPress rotation.

**Files changed:** `libs/wordpress/WordPressDatabaseCredentialResolver.h`, `libs/wordpress/WordPressDatabaseCredentialResolver.cpp`, `libs/core/ServiceRegistry.h`, `libs/core/ServiceRegistry.cpp`, `libs/database/DatabaseCredentialRotationAdapter.h`, `libs/database/DatabaseCredentialRotationAdapter.cpp`, `libs/api/ApiServer.cpp`, `web/app.js`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/test_wordpress_config_service.cpp`, `tests/test_database_credential_rotation.cpp`, `tests/test_api.cpp`, `docs/api/API_REFERENCE.md`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** Site Details now rotates only the backend-resolved WordPress database target and disables rotation when no exact database record can be resolved. Multi-database sites no longer risk queuing rotation against the first database card entry when WordPress uses a different database.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1` and no compiler warnings. Focused tests passed for `*WordPressDatabaseCredentialResolver*,*WordPressConfigService*` (`12` cases, `78` assertions), `*DatabaseCredentialRotation*` (`26` cases, `259` assertions), `*API*` (`18` cases, `73` assertions), `*WordPress*` (`61` cases, `365` assertions), `*database*` (`50` cases, `464` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). `node --check web/app.js`, full CTest (`1/1`), and `git diff --check` passed.

**Known risks:** Full metadata atomicity and compensation consistency remain scheduled for WP-R6. Runtime verifier trust-boundary hardening, endpoint/job safety, UI safety-state polish, documentation readiness language, and final full validation remain scheduled for WP-R7 through WP-R11. Live rotation still has not been validated against a real Docker/MariaDB/WordPress site in this task.

---

## 2026-07-19 | `this commit` | WordPress — Wire credential rotation dependencies

**Summary:** Added WP-R4 production-shaped dependency wiring for WordPress database credential rotation. New `DatabaseCredentialRotationAdapter` implements `DatabaseCredentialRotationDependencies` and connects the rotation saga to site/database metadata, WordPress config inspection/update, MariaDB credential provider operations, WordPress/PHP runtime verification, runtime apply/site-health callbacks, metadata persistence, and compensation rollback handles. `ServiceRegistry` now constructs concrete command runners, providers, verifier/updater objects, the adapter, and a wired `DatabaseCredentialRotationService`, while API, CLI, Web UI, and job code remain clients of the service instead of owning rotation business logic.

**Files changed:** `libs/database/DatabaseCredentialRotationAdapter.h`, `libs/database/DatabaseCredentialRotationAdapter.cpp`, `libs/core/ServiceRegistry.h`, `libs/core/ServiceRegistry.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/test_database_credential_rotation.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** Rotation jobs now have concrete live dependencies behind the existing guarded workflow, but this change was not deployed or tested against a real site. Unsupported WordPress configs, missing site admin credential source, database/site mismatches, shared-user risks, provider failures, and verification failures continue to fail closed with redacted diagnostics.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1` and no compiler warnings. Focused tests passed for `*DatabaseCredentialRotationAdapter*` (`4` cases, `107` assertions), `*DatabaseCredentialRotation*` (`26` cases, `259` assertions), and `*DatabaseCredentialRotation*,*database*,*WordPress*` (`105` cases, `779` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** The adapter uses the current site-root `.env` and compose layout contracts and has not been exercised against Docker or a real MariaDB/WordPress site in this task. Multi-database backend target resolution, metadata atomicity/rollback, runtime verifier trust-boundary hardening, endpoint/job safety, UI exact-target behavior, documentation readiness wording, and final full validation remain scheduled for WP-R5 through WP-R11.

---

## 2026-07-19 | `this commit` | Database — Harden MariaDB secret transport

**Summary:** Added WP-R3 hardening for MariaDB credential transport. The provider now sends a length-framed `CONTAINERCP-MARIADB-FRAME-V1` stdin payload instead of splitting a combined stream with the legacy `--CONTAINERCP-SQL--` delimiter. The in-container script writes separate protected defaults and SQL files using explicit byte lengths. Provider inputs are validated before any secret file is written using a strict 1-256 byte safe alphabet for option-file credentials, target `User` + `Host`, and SQL password literals. Newline, carriage return, NUL, tabs/control characters, option-file metacharacters, SQL delimiter text, spaces, equals, backslash, quotes, and overlong values fail closed with redacted diagnostics.

**Files changed:** `libs/database/MariaDBCredentialProvider.cpp`, `tests/test_mariadb_credential_provider.cpp`, `docs/development/mariadb-credential-provider.md`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No live rotation is enabled. Future MariaDB credential operations now reject unsupported credential characters before command execution instead of risking option-file, SQL, or delimiter confusion.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1` and no compiler warnings. Focused tests passed for `*MariaDBCredentialProvider*` (`13` cases, `142` assertions), `*DatabaseCredentialRotation*` (`22` cases, `152` assertions), `*database*` (`44` cases, `344` assertions), `*WordPress*` (`58` cases, `344` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** The safe transport contract intentionally rejects imported credentials containing characters outside the reviewed alphabet; those sites will fail closed until a future protocol/prepared-statement provider supports a broader password set. Full centralized product-wide password generation policy remains part of later live dependency wiring and documentation consistency work.

---

## 2026-07-19 | `this commit` | WordPress — Preserve valid system site identity

**Summary:** Added WP-R2 `site_id=0` semantic fixes for WordPress credential operations. WordPress config inspection now resolves `site_id=0` through the site manager instead of rejecting it by numeric value; resolved system-site records without WordPress return `wordpress_not_detected`. Domain inspection no longer rejects a resolved site just because its id is `0`. Rotation and queue services no longer reject `site_id=0` before resource/dependency checks. The WordPress credential API now parses numeric identifiers strictly so literal `0` is distinct from missing, negative, or malformed IDs.

**Files changed:** `libs/wordpress/WordPressConfigService.cpp`, `libs/database/DatabaseCredentialRotationService.cpp`, `libs/database/DatabaseCredentialRotationJobService.cpp`, `libs/api/ApiServer.cpp`, `tests/test_wordpress_config_service.cpp`, `tests/test_database_credential_rotation.cpp`, `docs/development/wordpress-credential-management.md`, `docs/development/wordpress-credential-foundation-checklist.md`, `planning/project-status.md`, `CHANGELOG.md`

**User-visible behavior:** Public WordPress credential status for an existing system-site identity now reports unsupported WordPress capability (`wordpress_not_detected`) rather than treating `site_id=0` as an invalid identifier. Rotation still does not enable live credential mutation.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1` and no compiler warnings. Focused tests passed for `*WordPressConfigService*` (`10` cases, `65` assertions), `*DatabaseCredentialRotation*` (`22` cases, `152` assertions), `*API*` (`18` cases, `73` assertions), `*WordPress*` (`58` cases, `344` assertions), `*database*` (`44` cases, `344` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`).

**Known risks:** Existing runtime APIs outside WordPress credential operations still intentionally treat `site_id=0` as not applicable. Full endpoint authorization, overflow/negative parsing coverage, and job immutability hardening remain scheduled for WP-R8.

---

## 2026-07-19 | `this commit` | Database — Implement strict shared credential assessment

**Summary:** Added WP-R1 strict shared-user assessment for WordPress database credential rotation. `MariaDBCredentialProvider::detect_shared_user()` now returns a structured `MariaDBSharedCredentialAssessment` instead of relying on a boolean-only result, queries machine-readable batch output for exact `User` + `Host`, username host variants, and schema grant count, and fails closed on empty, malformed, duplicate, unexpected, ambiguous, or command-failure output. `DatabaseCredentialRotationService` now runs an explicit pre-mutation `assess_shared_user()` step after old-credential verification and before password generation; every state except `not_shared` blocks rotation before any password mutation.

**Files changed:** `libs/database/MariaDBCredentialProvider.h`, `libs/database/MariaDBCredentialProvider.cpp`, `libs/database/DatabaseCredentialRotationService.h`, `libs/database/DatabaseCredentialRotationService.cpp`, `tests/test_mariadb_credential_provider.cpp`, `tests/test_database_credential_rotation.cpp`, `docs/development/mariadb-credential-provider.md`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No live rotation is enabled. Future rotation attempts through the saga are now blocked before mutation when MariaDB shared-user assessment is `shared`, `unknown`, `identity_missing`, `multiple_host_identities`, or `metadata_conflict`.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1` and no compiler warnings. Focused tests passed for `*MariaDBCredentialProvider*` (`12` cases, `65` assertions), `*DatabaseCredentialRotationService*` (`17` cases, `132` assertions), `*DatabaseCredentialRotation*` (`21` cases, `149` assertions), `*database*` (`43` cases, `341` assertions), `*WordPress*` (`58` cases, `339` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed. Static secret-surface checks found only expected internal provider/saga variables and redaction-test literals.

**Known risks:** Provider-level assessment uses `mysql.user` and `mysql.db` runtime facts only. Full ContainerCP metadata and cross-site WordPress credential correlation is represented by the `metadata_conflict` state but will be completed in the real dependency adapter and exact database-target resolution phases. MariaDB secret transport hardening is still scheduled for WP-R3.

---

## 2026-07-19 | `this commit` | WordPress — Document credential operations and finalize WP-8 hardening

**Summary:** Added the WP-8 operator/security documentation for the WordPress database credential foundation. The new runbook documents supported direct-constant configs, unsupported fail-closed states, single-source ownership, API/CLI/GUI workflow, compensation and manual-recovery behavior, secret-handling rules, threat model, residual plaintext SQLite/`.env` risk, and live-validation requirements. The API reference now records rotation operational notes and the current fail-closed foundation behavior. The project tracker now lists the v0.8 WordPress Credential Foundation status. The final migration audit confirmed credential parsing and credential updates are already delegated to shared WordPress services; remaining migration `wp-config.php` handling is archive discovery, backup/rollback, container path mapping, and trusted-proxy insertion rather than duplicate credential ownership.

**Files changed:** `docs/development/wordpress-credential-management.md`, `docs/development/wordpress-credential-foundation-checklist.md`, `docs/api/API_REFERENCE.md`, `planning/project-status.md`, `CHANGELOG.md`

**User-visible behavior:** No runtime behavior change. Operators now have a documented WordPress credential-management workflow and threat model. Inspection and guarded queueing remain available, while live production credential rotation continues to fail closed until explicitly wired and validated. Databases GUI, Adminer, SQL import/export, and full WordPress site creation remain out of scope.

**Validation:** Configure passed with `cmake -S . -B build-wp0 -G Ninja -DCMAKE_BUILD_TYPE=Release`. Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1` (`ninja: no work to do`, no compiler warnings emitted). Focused tests passed for `*WordPress*` (`58` cases, `339` assertions), `*DatabaseCredentialRotation*` (`17` cases, `131` assertions), `*API*` (`18` cases, `73` assertions), `*database*` (`39` cases, `323` assertions), `*Command*` (`17` cases, `62` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full doctest passed with `build-wp0/tests/containercp_tests` (`749` cases, `4992` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `node --check web/app.js`, `build-wp0/containercp --version`, `build-wp0/containercpd --version`, and `git diff --check` passed. Static secret-surface checks found only expected existing auth password-change fields, internal provider/saga password variables, docs, and the public boolean `db_password_present`; no WordPress UI/API/CLI surface exposes generated or raw database passwords.

**Known risks:** Current storage still persists database passwords in plaintext SQLite/TXT metadata and site `.env` files. Live MariaDB grant requirements and real rotation remain unvalidated on a test site. Current queue jobs still fail closed by design because live dependencies are intentionally unwired.

---

## 2026-07-19 | `this commit` | Web — Add Site Details WordPress credential rotation UI

**Summary:** Added WP-7 Site Details integration for WordPress database credentials. The backend now exposes `GET /api/wordpress/database-credentials/status?site_id=N`, backed by `WordPressConfigService::public_view()`, and `ServiceRegistry` owns the shared WordPress config service. The Site Details page now shows a WordPress Database Credentials card with public-safe status/source/mutability, DB name/user/host, password-presence boolean, sanitized issues, typed domain confirmation, queue submission, job id toast, and job polling through the existing Jobs API.

**Files changed:** `libs/core/ServiceRegistry.h`, `libs/core/ServiceRegistry.cpp`, `libs/api/ApiServer.cpp`, `web/app.js`, `tests/test_api.cpp`, `docs/api/API_REFERENCE.md`, `docs/development/api-rules.md`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** Site Details now shows WordPress credential status and a guarded rotate action for supported sites. The Databases page, Adminer, SQL import/export, and full WordPress site creation remain unchanged and out of scope. Rotation jobs still fail safely until live rotation dependencies are wired.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused tests passed for `*API*` (`18` cases, `73` assertions), `*WordPress*` (`58` cases, `339` assertions), `*DatabaseCredentialRotation*` (`17` cases, `131` assertions), `*database*` (`39` cases, `323` assertions), `*Command*` (`17` cases, `62` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `node --check web/app.js` passed. `git diff --check` passed.

**Known risks:** The UI can queue jobs, but live rotation dependencies remain unwired, so actual jobs still fail closed. No browser-based visual test harness exists; coverage is by static UI tests, JS syntax check, backend unit tests, and CTest.

---

## 2026-07-19 | `this commit` | WordPress — Queue credential rotation jobs through API and CLI

**Summary:** Added WP-6 API/CLI job entrypoints for WordPress database credential rotation. New `DatabaseCredentialRotationJobService` validates site id, database id, typed domain confirmation, database ownership, duplicate queued rotations, and queue availability before creating an async job. `POST /api/wordpress/database-credentials/rotate` returns HTTP `202` with job id/status only. The daemon command `wordpress-rotate-db-password` and CLI command `containercp wordpress rotate-db-password <site_id> <database_id> --confirm <domain>` delegate to the same queue service.

**Files changed:** `libs/database/DatabaseCredentialRotationJobService.h`, `libs/database/DatabaseCredentialRotationJobService.cpp`, `libs/core/ServiceRegistry.h`, `libs/core/ServiceRegistry.cpp`, `libs/api/ApiServer.cpp`, `libs/daemon/DaemonApp.cpp`, `libs/cli/CommandDispatcher.cpp`, `tests/test_database_credential_rotation.cpp`, `tests/test_api.cpp`, `tests/test_daemon.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/api/API_REFERENCE.md`, `docs/development/api-rules.md`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** Operators can request a WordPress database credential rotation job through the REST API or CLI and receive a job id. The job currently fails safely until live rotation dependencies are wired; no production credential mutation is enabled by this stage.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused queue/rotation tests passed with `build-wp0/tests/containercp_tests -tc="*DatabaseCredentialRotation*"` (`17` cases, `131` assertions), covering queued jobs, confirmation mismatch, database ownership, duplicate queued rotations, redacted job messages, and rotation state regressions. Focused regressions passed for `*database*` (`39` cases, `323` assertions), `*API*` (`18` cases, `73` assertions), `*Command*` (`17` cases, `62` assertions), `*WordPress*` (`57` cases, `331` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). CLI help/version checks passed with `build-wp0/containercp --help` and `build-wp0/containercp --version`. `git diff --check` passed.

**Known risks:** The queue/API/CLI layer is now present, but live provider/runtime/storage dependencies are still not wired, so queued jobs fail closed instead of rotating real credentials. API authentication remains according to the existing API server auth model; no additional role model was added in this stage.

---

## 2026-07-19 | `this commit` | WordPress — Verify rotated credentials through PHP runtime

**Summary:** Added WP-5.4 WordPress/PHP runtime verification boundary. New `WordPressRuntimeVerifier` uses an injectable runner plus a `CommandExecutor` adapter to execute a fixed PHP script through vector-argv `docker compose exec -T php php -r ...`. The script loads `wp-config.php` inside the site PHP container, verifies required DB constants, and checks `mysqli` database access with `SELECT 1` without passing credentials through argv, shell strings, logs, or result messages. `WordPressConfigService` now records the PHP container document root and can produce verifier requests from safe inspection results, including default Apache and nginx mount paths.

**Files changed:** `libs/wordpress/WordPressRuntimeVerifier.h`, `libs/wordpress/WordPressRuntimeVerifier.cpp`, `libs/wordpress/WordPressConfigService.h`, `libs/wordpress/WordPressConfigService.cpp`, `tests/test_wordpress_runtime_verifier.cpp`, `tests/test_wordpress_config_service.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The verifier is internal and fake-tested only; it is not exposed through REST API, CLI, Web UI, jobs, production MariaDB, or production WordPress sites.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused verifier tests passed with `build-wp0/tests/containercp_tests -tc="*WordPressRuntimeVerifier*"` (`5` cases, `26` assertions), covering scoped argv construction, unsafe config-path rejection, stderr redaction, no credential literals, and PHP path escaping. Focused service tests passed with `*WordPressConfigService*` (`10` cases, `60` assertions). Broader regressions passed for `*WordPress*` (`55` cases, `320` assertions), `*DatabaseCredentialRotationService*` (`13` cases, `114` assertions), `*database*` (`35` cases, `306` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** Live PHP execution remains unwired to jobs/API/CLI and must be connected through the rotation dependencies in a later wiring stage. Runtime verification assumes the site Compose project and PHP service are healthy enough to run `php`; operational retries/backoff are deferred.

---

## 2026-07-19 | `this commit` | WordPress — Add credential rotation compensation

**Summary:** Added WP-5.3 compensation and manual-recovery handling to `DatabaseCredentialRotationService`. Failures after MariaDB password mutation now enter a single rollback attempt that restores the MariaDB password, restores WordPress config when it was changed, reapplies/restores runtime when required, verifies the old credential again, and reports `compensated` only when rollback completes. Failed rollback steps now return `manual_recovery_required` with generic redacted diagnostics.

**Files changed:** `libs/database/DatabaseCredentialRotationService.h`, `libs/database/DatabaseCredentialRotationService.cpp`, `tests/test_database_credential_rotation.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. Compensation is still internal and fake-driven; real provider/runtime/storage/API/CLI/Web UI wiring remains pending and no production credential rotation is enabled.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused rotation tests passed with `build-wp0/tests/containercp_tests -tc="*DatabaseCredentialRotationService*"` (`13` cases, `114` assertions), covering config-update failure after DB mutation, verification failure rollback, DB restore failure, config restore failure, runtime restore failure, manual recovery state, lock release, and no secret-bearing diagnostics. Focused regressions passed for `*database*` (`34` cases, `290` assertions), `*WordPress*` (`49` cases, `286` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** Live compensation still depends on later wiring to actual MariaDB, WordPress config rollback handles, runtime operations, and storage. The WordPress/PHP-level verification boundary remains pending for WP-5.4.

---

## 2026-07-19 | `this commit` | WordPress — Add database credential rotation saga

**Summary:** Added the WP-5.2 happy-path database credential rotation saga. `DatabaseCredentialRotationService` now accepts an injected dependency boundary and executes the ordered rotation workflow with fakes: WordPress credential inspection, old credential verification, replacement password generation, MariaDB password change, WordPress config update, runtime application, new MariaDB credential verification, WordPress verification, site health verification, and metadata persistence. Unsupported inspection fails before mutation, generated passwords are only passed to dependency calls, and dependency failure messages are not surfaced to callers.

**Files changed:** `libs/database/DatabaseCredentialRotationService.h`, `libs/database/DatabaseCredentialRotationService.cpp`, `tests/test_database_credential_rotation.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The saga remains internal and fake-driven; it is not yet wired to real storage, runtime, jobs, REST API, CLI, Web UI, production MariaDB, or production WordPress files.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused rotation tests passed with `build-wp0/tests/containercp_tests -tc="*DatabaseCredentialRotationService*"` (`9` cases, `70` assertions), covering happy-path order, unsupported inspection before mutation, generated-password handling, redaction of dependency failure messages, post-verification stop before metadata, and lock release. Focused regressions passed for `*database*` (`30` cases, `246` assertions), `*WordPress*` (`49` cases, `286` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** WP-5.2 does not yet compensate after post-mutation failures. WP-5.3 must restore MariaDB/config/runtime or enter explicit manual recovery for failures after password mutation. Live provider/storage/runtime integration remains pending.

---

## 2026-07-19 | `this commit` | WordPress — Add credential rotation state machine

**Summary:** Added the WP-5.1 database credential rotation service foundation. `DatabaseCredentialRotationService` now defines the explicit saga state model, request/result/event structs, redacted event messages, and mutex-backed per-site/database locking. The service rejects unsupported `site_id=0`, rejects missing database ids, releases locks after failure, and fails closed until the remaining saga dependencies are wired.

**Files changed:** `libs/database/DatabaseCredentialRotationService.h`, `libs/database/DatabaseCredentialRotationService.cpp`, `tests/test_database_credential_rotation.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The service is not yet wired to providers, storage, jobs, REST API, CLI, Web UI, or production operations and cannot rotate credentials yet.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused rotation tests passed with `build-wp0/tests/containercp_tests -tc="*DatabaseCredentialRotationService*"` (`5` cases, `26` assertions), covering state strings, `site_id=0` rejection, missing database rejection, lock release after failure, and redacted events. Focused regressions passed for `*database*` (`26` cases, `202` assertions), `*WordPress*` (`49` cases, `286` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** This is the state-machine foundation only. The MariaDB provider, WordPress updater, runtime application, verification, metadata persistence, compensation, API/CLI, and GUI wiring remain pending for later WP-5/WP-6/WP-7 stages.

---

## 2026-07-19 | `this commit` | Documentation — Document MariaDB credential provider grants

**Summary:** Added WP-4.3 documentation for the MariaDB credential provider. The document records provider scope, secret transport, minimum grant direction, root-as-break-glass guidance, shared-user risk handling, redacted failure behavior, and current fake-runner validation status.

**Files changed:** `docs/development/mariadb-credential-provider.md`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This documentation does not enable API, CLI, GUI, jobs, production rotation, or live MariaDB access.

**Validation:** Documentation-only `git diff --check` passed.

**Known risks:** Exact narrow-account grant requirements must be verified against the MariaDB version used by the validation site before real rotation is enabled. Root remains documented only as bootstrap/break-glass compatibility, not the preferred runtime path.

---

## 2026-07-19 | `this commit` | Database — Add MariaDB credential provider

**Summary:** Added the WP-4.1/WP-4.2 MariaDB credential provider boundary. `MariaDBCredentialProvider` provides fakeable operations for password verification, password change, password restore, and shared-user detection using `MariaDBProcessRunner` plus a `CommandExecutor` adapter. Secrets are transported through protected host-side stdin bundles and fixed in-container temporary option/SQL files; passwords are not placed in command argv, shell strings, result messages, logs, API, CLI, or UI.

**Files changed:** `libs/database/MariaDBCredentialProvider.h`, `libs/database/MariaDBCredentialProvider.cpp`, `tests/test_mariadb_credential_provider.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The provider is not yet wired into the rotation saga, REST API, CLI, Web UI, jobs, storage, migration, or production operations.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused provider tests passed with `build-wp0/tests/containercp_tests -tc="*MariaDBCredentialProvider*"` (`6` cases, `29` assertions), covering no-secret argv construction, stdin bundle content, cleanup, SQL quoting, command failure redaction, restore path, shared-user query construction, and invalid target rejection. Focused regressions passed for `*database*` (`21` cases, `176` assertions), `*WordPress*` (`49` cases, `286` assertions), and `VestaSiteImporter*` (`31` cases, `79` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** Live MariaDB execution is not yet integration-tested and is not exposed to operators. Shared-user detection currently establishes the query/provider boundary and returns a safe default until WP-5 consumes parsed provider output. Minimum grants are documented separately in WP-4.3.

---

## 2026-07-19 | `this commit` | Migration — Use shared WordPress config updater

**Summary:** Refactored migration SQL import `wp-config.php` credential updates for WP-3.4 to use the shared `WordPressConfigUpdater`. The migration path now updates `DB_NAME`, `DB_USER`, `DB_PASSWORD`, and `DB_HOST` through one atomic sequence update, uses the shared PHP literal encoder, validates with the existing vector-argv container `php -l` check through the updater validation boundary, and relies on updater rollback before migration database rollback continues on failure.

**Files changed:** `libs/migration/VestaSiteImporter.h`, `libs/migration/VestaSiteImporter.cpp`, `libs/wordpress/WordPressConfigUpdater.h`, `libs/wordpress/WordPressConfigUpdater.cpp`, `tests/test_wordpress_config_update.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** Migration SQL import remains a no-secret workflow and still reports generic `wp-config.php update failed` or syntax failure behavior through existing migration error handling. Unsupported, ambiguous, symlinked, or path-escaping `wp-config.php` updates now fail closed through the shared updater instead of legacy regex replacement.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused WordPress tests passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`49` cases, `286` assertions). Migration regression passed with `build-wp0/tests/containercp_tests -tc="VestaSiteImporter*"` (`31` cases, `79` assertions) and `build-wp0/tests/containercp_tests -tc="*Migration*"` (`39` cases, `254` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** Trusted proxy block insertion still uses its existing migration-specific write path and is scheduled for final cleanup in WP-8 where appropriate. No rotation behavior exists yet.

---

## 2026-07-19 | `this commit` | WordPress — Validate config updates before completion

**Summary:** Added the WP-3.3 validation boundary for WordPress config credential updates. `WordPressConfigUpdater::update_file_atomic_validated()` now wraps the atomic writer with an injectable validator, reports success only after validation accepts the written file, automatically rolls back on validation failure, and returns a manual-recovery error state if rollback cannot complete. Validation diagnostics are intentionally generic and do not expose candidate password values.

**Files changed:** `libs/wordpress/WordPressConfigUpdater.h`, `libs/wordpress/WordPressConfigUpdater.cpp`, `tests/test_wordpress_config_update.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The validation boundary is not yet wired to runtime PHP execution, migration, REST API, CLI, Web UI, storage, or production site operations.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused WordPress tests passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`48` cases, `279` assertions), covering validation success, validation failure rollback, rollback failure/manual state, missing validator rejection, and redacted validation errors. Migration regression passed with `build-wp0/tests/containercp_tests -tc="VestaSiteImporter*"` (`31` cases, `79` assertions) and `build-wp0/tests/containercp_tests -tc="*Migration*"` (`39` cases, `254` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** Production PHP syntax validation is still an injected boundary rather than runtime-wired execution. Migration still uses its legacy file update path until WP-3.4.

---

## 2026-07-19 | `this commit` | WordPress — Add atomic config credential updates

**Summary:** Added the WP-3.2 atomic WordPress config file update primitive. `WordPressConfigUpdater::update_file_atomic()` validates the config path through the existing safety helper, rejects symlinks and unsafe paths, renders credential changes in memory, writes a protected same-directory temp file, fsyncs, atomically renames, preserves mode and root-only ownership metadata where allowed, cleans up temp files on failure, and returns an in-memory rollback handle. `rollback_file()` restores the previous content through the same atomic write path.

**Files changed:** `libs/wordpress/WordPressConfigUpdater.h`, `libs/wordpress/WordPressConfigUpdater.cpp`, `tests/test_wordpress_config_update.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The atomic writer is not yet wired to migration, syntax validation, REST API, CLI, Web UI, runtime, storage, or production site operations.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused WordPress tests passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`44` cases, `261` assertions), covering atomic replacement, rollback, mode preservation, temp cleanup, symlink rejection, path escape rejection, failure no-change behavior, and no secret in failure messages. Migration regression passed with `build-wp0/tests/containercp_tests -tc="VestaSiteImporter*"` (`31` cases, `79` assertions) and `build-wp0/tests/containercp_tests -tc="*Migration*"` (`39` cases, `254` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** PHP syntax validation and validation-failure rollback are deferred to WP-3.3. Migration still uses its legacy file update path until WP-3.4. Ownership preservation only changes file owner/group when running as root; non-root tests preserve mode and content.

---

## 2026-07-19 | `this commit` | Bug Fix — Safely encode WordPress credential literals

**Summary:** Fixed the WP-3.1 WordPress config renderer before filesystem writes. Double-quoted PHP credentials now escape dollar signs to prevent variable interpolation and encode newline, carriage return, tab, NUL, and other control characters with PHP string semantics. Single-quoted credentials now keep PHP single-quote semantics by escaping backslash and single quote and rejecting unsupported NUL/control values rather than silently changing the password. Conditional detection was also changed from proximity-based scanning to block-aware scanning so completed unrelated `if`/loop blocks, comments, and strings do not make an unconditional `DB_PASSWORD` unwritable, while genuinely conditional target definitions remain rejected.

**Files changed:** `libs/wordpress/WordPressConfigUpdater.cpp`, `tests/test_wordpress_config_update.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The renderer remains in-memory only and still is not wired to filesystem writes, syntax validation, migration update, REST API, CLI, Web UI, runtime, storage, or production site operations.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused WordPress tests passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`38` cases, `232` assertions). Migration regression passed with `build-wp0/tests/containercp_tests -tc="VestaSiteImporter*"` (`31` cases, `79` assertions) and `build-wp0/tests/containercp_tests -tc="*Migration*"` (`39` cases, `254` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** Single-quoted existing credential definitions reject NUL and non-newline/carriage-return/tab control characters because PHP single-quoted strings cannot safely represent those values through escape syntax. Atomic file writing, ownership/mode preservation, PHP syntax validation, rollback, and migration update refactoring remain deferred to WP-3.2 through WP-3.4.

---

## 2026-07-19 | `this commit` | WordPress — Add safe credential update renderer

**Summary:** Added the WP-3.1 in-memory WordPress credential update renderer. `WordPressConfigUpdater::render_update()` replaces exactly one supported direct string-literal `DB_NAME`, `DB_USER`, `DB_PASSWORD`, or `DB_HOST` definition while preserving surrounding content and quote style. It rejects missing, dynamic, included, duplicate, and conditional target definitions and keeps failure diagnostics free of secret values.

**Files changed:** `libs/wordpress/WordPressConfigUpdater.h`, `libs/wordpress/WordPressConfigUpdater.cpp`, `tests/test_wordpress_config_update.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The renderer is in-memory only and is not yet wired to filesystem writes, syntax validation, migration update, REST API, CLI, Web UI, runtime, storage, or production site operations.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused WordPress tests passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`31` cases, `180` assertions), covering DB password replacement, quote preservation, special-character escaping, unrelated content preservation, duplicate rejection, dynamic rejection, include rejection, conditional rejection, and redacted diagnostics. Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** Atomic file writing, ownership/mode preservation, PHP syntax validation, rollback, and migration update refactoring remain deferred to WP-3.2 through WP-3.4.

---

## 2026-07-19 | `this commit` | Migration — Use shared WordPress credential detector

**Summary:** Refactored the migration inspect-only `wp-config.php` credential parser for WP-2.3 to use `WordPressConfigDetector::inspect_content()`. The importer now relies on the shared WordPress detector for read-only DB constant parsing while preserving existing manifest behavior for found configs, direct-literal parse success, DB name/user/host fields, dynamic `DB_NAME` ambiguity, SQL dump lookup, and no-password migration output.

**Files changed:** `libs/migration/VestaSiteImporter.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** Migration inspect and dry-run output remain the same for supported direct-literal WordPress configs and dynamic `DB_NAME` ambiguity. `DB_PASSWORD` remains omitted from migration manifest/API/UI output and is no longer carried out of the inspect-only parser path.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused regression passed for `VestaSiteImporter*` (`31` cases, `79` assertions), `*Migration*` (`39` cases, `254` assertions), and `*WordPress*` (`25` cases, `162` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** Migration config update logic still uses its legacy replacement path until WP-3.4/WP-8.1. Unsupported WordPress config forms remain inspect-only/manual-selection paths; no rotation behavior exists yet.

---

## 2026-07-19 | `this commit` | WordPress — Add public-safe config inspection view

**Summary:** Added the WP-2.2 public-safe WordPress config inspection projection. `WordPressConfigPublicView` exposes status/source/mutability, site id, domain, non-secret DB name/user/host, password presence, and sanitized issues only. It intentionally omits config paths, site roots, document roots, root passwords, option-file paths, and raw `DB_PASSWORD` values. The service now also records read-only unsafe-permission warnings for group/other-accessible `wp-config.php` files.

**Files changed:** `libs/wordpress/WordPressConfigService.h`, `libs/wordpress/WordPressConfigService.cpp`, `tests/test_wordpress_config_service.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The public view is an internal projection for future API/UI wiring and is not yet exposed through REST API, CLI, Web UI, migration, runtime, storage, or production site operations.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused WordPress tests passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`25` cases, `162` assertions), covering redaction, no path exposure, byte-for-byte no-change inspection, unsafe permission warnings, and ambiguous config state. Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** REST API and Web UI are not yet wired to this public view. Migration still uses its existing parser until WP-2.3.

---

## 2026-07-19 | `this commit` | WordPress — Add read-only config inspection service

**Summary:** Added `WordPressConfigService` for WP-2.1. The service resolves a site by id or domain, rejects `site_id=0`, confines the resolved site root to the configured sites directory, checks common document-root locations for an active `wp-config.php`, validates the candidate path through the detector safety helper, reads the file without mutation, and returns the detector inspection result.

**Files changed:** `libs/wordpress/WordPressConfigService.h`, `libs/wordpress/WordPressConfigService.cpp`, `tests/test_wordpress_config_service.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The service is not yet exposed through REST API, CLI, Web UI, migration, runtime, storage, or production site operations.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused WordPress tests passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`21` cases, `138` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** The result is still internal and not a public-safe API view; WP-2.2 will add the explicit public-safe projection. Migration still uses its existing parser until WP-2.3.

---

## 2026-07-19 | `this commit` | WordPress — Harden config detector path safety

**Summary:** Added read-only filesystem safety helpers for WP-1.3. `WordPressConfigDetector::inspect_config_path()` now classifies candidate `wp-config.php` paths under an approved site root and fails closed for missing inputs, missing roots/files, backup or temp filenames, path traversal outside the root, symlinked roots/path components/config files, non-directory parents, and non-regular config files.

**Files changed:** `libs/wordpress/WordPressConfigDetector.h`, `libs/wordpress/WordPressConfigDetector.cpp`, `tests/test_wordpress_config_detector.cpp`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. These helpers are not yet wired into migration, REST API, CLI, Web UI, runtime, storage, or production site operations.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused WordPress tests passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`16` cases, `110` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** Site/domain resolution and `site_id=0` service-level rejection remain deferred to WP-2. The path helper is internal and read-only; it does not yet discover active configs automatically.

---

## 2026-07-19 | `this commit` | WordPress — Add credential source detector

**Summary:** Added the read-only WordPress credential source detector for WP-1.2. The detector scans `define(...)` calls outside PHP comments and strings, extracts supported direct string-literal database constants, redacts `DB_PASSWORD`, and classifies unsupported or ambiguous sources such as `getenv()`, `$_ENV`, `$_SERVER`, variable references, includes, concatenation expressions, helper calls, duplicates, conditionals, missing content, and missing credentials.

**Files changed:** `libs/wordpress/WordPressConfigDetector.h`, `libs/wordpress/WordPressConfigDetector.cpp`, `libs/wordpress/WordPressConfigTypes.h`, `tests/test_wordpress_config_detector.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The detector is not yet wired into migration, REST API, CLI, Web UI, runtime, storage, or production site operations.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused WordPress tests passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`11` cases, `88` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). `git diff --check` passed.

**Known risks:** Filesystem path safety is intentionally deferred to WP-1.3. Read-only site/domain inspection, migration refactor, safe config mutation, MariaDB credential changes, rotation saga, API/CLI, and Web UI support remain pending.

---

## 2026-07-19 | `this commit` | WordPress — Add credential inspection types

**Summary:** Added the initial WordPress credential inspection type model for WP-1.1. The new `WordPressConfigTypes` subsystem defines credential source, mutability, status, value-state, and issue severity enums with string conversion helpers, plus public-safe credential value/set/inspection structures that redact sensitive values by construction.

**Files changed:** `libs/wordpress/WordPressConfigTypes.h`, `libs/wordpress/WordPressConfigTypes.cpp`, `tests/test_wordpress_config_types.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. The new types are not wired into API, CLI, Web UI, migration, runtime, storage, or production site operations yet.

**Validation:** Incremental build passed with `cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused WordPress tests passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`5` cases, `51` assertions), including type/string conversion and sensitive-value redaction coverage. Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`).

**Known risks:** Parser, filesystem safety, read-only inspection service, config updates, MariaDB provider, rotation saga, API/CLI, and Web UI actions remain pending. The type model may receive additional enum values as later detector implementation covers more WordPress config forms.

---

## 2026-07-19 | `this commit` | Documentation — Add WordPress credential implementation checklist

**Summary:** Added the executable WP-0 through WP-8 implementation checklist for the approved v0.8 WordPress database credential-management foundation. The checklist maps current source behavior, records existing credential duplication and intended single-source ownership, defines incremental commit-sized work items, and captures clean baseline validation before implementation begins.

**Files changed:** `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This is documentation and validation evidence only; it does not create API endpoints, change the Web UI, modify SQLite schema, alter Docker Compose generation, rotate credentials, edit real `wp-config.php` files, or touch production/server state.

**Validation:** Clean configure/build passed with `cmake -S . -B build-wp0 -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`. Focused baseline tests passed for `VestaSiteImporter*` (`31` cases, `79` assertions), `*database*` (`21` cases, `176` assertions), `*Runtime*` (`12` cases, `106` assertions), and `*JsonFormatter*` (`6` cases, `6` assertions). Full doctest passed (`669` cases, `4507` assertions). Standalone CTest passed (`1/1`). Version checks passed for `containercp` and `containercpd` (`0.7.0`).

**Known risks:** This is not yet an implementation. Current WordPress config parsing and replacement still live in migration code, credentials remain duplicated across SQLite/TXT storage, site `.env`, and imported `wp-config.php`, and rotation/API/CLI/UI support remains pending for later WP stages.

---

## 2026-07-19 | `this commit` | Architecture — Design WordPress credential rotation foundation

**Summary:** Added the v0.8 WordPress database credential-management architecture package. The design introduces `WordPressConfigService` as the reusable owner for WordPress config discovery, credential source classification, read-only inspection, safe atomic direct-constant updates, syntax validation, and rollback. It also defines the `DatabaseCredentialRotationService` saga for coordinating MariaDB password changes with ContainerCP metadata, site `.env`, and `wp-config.php`, documents the rotation threat model, and reviews WP-CLI integration options. DB-1 and the Databases GUI are now explicitly postponed behind this credential foundation unless DB-1 remains strictly read-only and uses the approved inspection boundary.

**Files changed:** `planning/wordpress-config-management-v0.8-architecture.md`, `planning/wordpress-db-password-rotation-v0.8-plan.md`, `planning/wordpress-db-password-rotation-v0.8-threat-model.md`, `planning/wp-cli-integration-v0.8-review.md`, `planning/database-module-v0.8-architecture.md`, `planning/database-module-v0.8-implementation-plan.md`, `planning/product-roadmap.md`, `planning/backlog.md`, `planning/project-status.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This is documentation/planning only and does not create API endpoints, change the Web UI, modify SQLite schema, modify Docker Compose generation, deploy WP-CLI, rotate passwords, edit real `wp-config.php` files, or alter production/server state.

**Validation:** Documentation-only review against current source files for site/database models, generated `.env`, Docker Compose services, site layout, site creation/removal, runtime command execution, myVestaCP import parsing and `wp-config.php` update behavior, migration tests, and official WP-CLI documentation. No build or runtime validation was required because no production code changed.

**Known risks:** The architecture is not yet implemented. Current migration still owns narrow `wp-config.php` parsing/replacement logic, credentials remain duplicated across SQLite and site `.env`, WP-CLI is not deployed, and Databases DB-1 remains postponed until the WordPress credential foundation is approved and implemented.

---

## 2026-07-19 | `this commit` | Architecture — Finalize Databases module v0.8 decisions

**Summary:** Finalized the post-v0.7.0 Databases module architecture package after architecture review. The approved design keeps Databases as a v0.8 major subsystem, supports one site with many databases, introduces Database Profiles and provider boundaries, makes `MYSQL_ROOT_PASSWORD` bootstrap-only, prefers rotate-not-reveal credential workflows, recommends on-demand Adminer, explicitly includes migrated myVestaCP/imported databases, strengthens credential/security ownership, and keeps DB-1 as read-only inventory.

**Files changed:** `planning/database-module-v0.8-architecture.md`, `planning/database-module-v0.8-implementation-plan.md`, `planning/database-module-v0.8-open-source-review.md`, `planning/database-module-v0.8-threat-model.md`, `planning/database-module-architecture.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This is documentation/planning only and does not create API endpoints, change the Web UI, modify Docker Compose generation, change database schema, deploy Adminer, or alter production data.

**Validation:** Documentation-only review against current source files for database metadata, site creation/removal, Compose topology, runtime status/actions, API serialization, Web UI behavior, SQLite storage, backups, and relevant tests. No build or runtime validation was required because no production code changed.

**Known risks:** The architecture is not yet implemented. Current database delete behavior remains metadata-only, current backups remain tar-based without logical SQL dumps, and current credentials remain stored in SQLite and site `.env` until a future approved implementation changes them.

---

## 2026-07-19 | `778a43b` | Test Fix — Build version binaries for release CI

**Summary:** Fixed release CI coverage for version output checks by making the `containercp_tests` target depend on the `containercp` and `containercpd` binaries. GitHub Actions builds only `containercp_tests`, so the version-output test now has the binaries it executes.

**Files changed:** `tests/CMakeLists.txt`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. Release CI now validates `containercp --version` and `containercpd --version` instead of failing because the binaries were not built by the workflow target.

**Validation:** Clean configure passed with `cmake -S . -B build-release-fix -G Ninja -DCMAKE_BUILD_TYPE=Release`. CI-equivalent target build passed with `cmake --build build-release-fix --target containercp_tests -- -j1`, including `containercp` and `containercpd` through the new dependency. Full doctest passed (`669` cases, `4507` assertions). CTest passed (`1/1`). Version output checks passed for `build-release-fix/containercp --version` and `build-release-fix/containercpd --version`.

**Known risks:** Building `containercp_tests` now also builds the CLI and daemon binaries, increasing CI build scope slightly while preserving the focused version-output assertions.

---

## v0.7.0 — 2026-07-19

**Summary:** ContainerCP v0.7.0 completes the SQLite storage backend release. Core runtime storage can now run on SQLite after explicit operator activation, while legacy TXT storage is preserved as migration source and archive data.

**Storage architecture:** SQLite backend introduced as the active core storage backend after manual activation. Legacy TXT storage remains available as migration source/archive data. Migration is explicit through `containercp storage migrate-to-sqlite`; daemon startup does not run schema or data migration. SQLite startup failures do not silently fall back to TXT.

**Migration and activation:** Added TXT-to-SQLite orchestration, verification before activation, immutable legacy archive creation, strict `storage-state.json` parsing, activation-state consistency validation, schema-version validation, and fail-closed daemon startup.

**Reliability:** Added SQLite transaction handling for replacement writes, caller-visible propagation of failed SQLite writes, restart persistence validation, `site_id=0` compatibility, and production failure-path coverage.

**Security:** Added validation for symlink rejection, regular-file requirements, directory/file permissions, ownership checks, and archive integrity before SQLite startup completes.

**Validation:** Phase 11 production review checklist P11-R1 through P11-R7 is complete. A production-like deployment was successfully migrated and activated on SQLite, then validated in SQLite-only operation after legacy TXT runtime files were archived and removed. Release validation passed clean configure, clean rebuild without compiler warnings, full doctest (`669` cases, `4507` assertions, `0` skipped), CTest (`1/1`), and version output checks for `containercp` and `containercpd`. Release commit CI is required to pass before the `v0.7.0` tag is created.

**Files changed:** `libs/core/Version.h`, `app/containercpd/main.cpp`, `CMakeLists.txt`, `README.md`, `AGENTS.md`, `CHANGELOG.md`, `docs/releases/v0.7.0.md`, `docs/development/sqlite-storage-api.md`, `docs/development/storage-schema.md`, `docs/development/legacy-archive-api.md`, `planning/project-status.md`, `tests/CMakeLists.txt`, `tests/test_version.cpp`

**Known limitations:** SQLite activation remains explicit. Daemon startup validates but does not migrate. Invalid SQLite state fails startup. There is no automatic fallback to TXT. Archive retention/deletion is not automated. Existing legacy TXT fixtures and v0.6.0 release references remain for migration and historical validation.

---

## 2026-07-19 | `a6ea3c9` | Bug Fix — Keep SQLite migration staging on target filesystem

**Summary:** Fixed SQLite migration publishing when `/tmp` and `/srv/containercp/database` are on different filesystems. Migration staging now happens in a hidden directory next to the target SQLite database, so final publication can use atomic rename without failing with `Invalid cross-device link`.

**Files changed:** `libs/storage/MigrationOrchestrator.cpp`, `tests/test_migrate_sqlite.cpp`, `CHANGELOG.md`

**User-visible behavior:** `containercp storage migrate-to-sqlite` can now complete on hosts where the temporary directory and ContainerCP data directory are different mounts. Failed pre-fix attempts remain fail-before-activation: they do not create `containercp.db`, do not create `storage-state.json`, and do not switch the active backend.

**Validation:** Incremental build passed for `containercp_tests` and `containercpd` with `cmake --build build2 --target containercp_tests containercpd -- -j1`. Focused `MigrationOrchestrator happy path` passed (`1` case, `10` assertions). Focused `P11-R7*` passed (`1` case, `92` assertions). CTest passed (`1/1`).

**Known risks:** A failed migration attempt may leave an already-created immutable legacy archive from the pre-publish stages; this is safe but can require operator cleanup under a separate retention policy.

---

## 2026-07-19 | `d4d601a` | Maintenance — Reduce clean-build compiler warning noise

**Summary:** Removed cosmetic compiler warning noise without changing product behavior. Replaced deprecated OpenSSL SHA256 calls with EVP SHA256 helpers, localized c-ares deprecation suppression behind compatibility wrappers, removed unused variables and helpers, fixed initializer-order and misleading-indentation warnings, and used size-correct legacy dataset indices where warnings were emitted.

**Files changed:** `libs/core/ServiceRegistry.cpp`, `libs/dns/DnsCheckService.cpp`, `libs/dns/SpfAnalyzer.cpp`, `libs/migration/VestaSiteImporter.cpp`, `libs/network/NetworkService.cpp`, `libs/operations/SiteCreateOperation.cpp`, `libs/proxy/NginxProxyProvider.cpp`, `libs/storage/LegacyArchive.cpp`, `libs/storage/LegacyDatasetReader.cpp`, `libs/storage/MigrationEngine.cpp`, `libs/storage/StorageCanonicalizer.h`, `libs/storage/Verification.cpp`, `tests/test_dns_api.cpp`, `tests/test_fixture_loader.cpp`, `tests/test_importer.cpp`, `tests/test_migration.cpp`, `CHANGELOG.md`

**User-visible behavior:** No intended product behavior change. Build output is cleaner and no longer reports the addressed warning categories during clean local rebuilds.

**Validation:** Clean rebuild passed without compiler warnings for `containercp_tests` and `containercpd` using `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`, continued after tool timeouts with target-specific build commands. Full suite passed directly with `build2/tests/containercp_tests` (`666` cases, `4491` assertions). A valid standalone CTest run passed (`1/1`). An earlier concurrent `ctest` run was invalid because it ran the full test binary in parallel with a direct full-suite run and hit shared test resources.

**Known risks:** c-ares event-loop behavior is intentionally unchanged; deprecated c-ares calls remain behind local compatibility wrappers until a dedicated API migration is planned and tested.

---

## 2026-07-19 | `9b33697` | Update — Install SQLite build dependency during updates

**Summary:** Updated `scripts/update.sh` so git-based updates install required build dependencies, including `libsqlite3-dev`, before running CMake. This prevents older installations from failing configuration after SQLite support is pulled.

**Files changed:** `scripts/update.sh`, `CHANGELOG.md`

**User-visible behavior:** Running `./scripts/update.sh` on an existing Debian installation now installs the SQLite development package required by CMake before building ContainerCP.

**Validation:** `bash -n scripts/update.sh` passed.

**Known risks:** Requires update execution with sufficient privileges to run `apt-get`; non-APT systems still need manual dependency installation.

---

## 2026-07-18 | `c9c09b3` | Phase 11 — Final SQLite production validation report

**Summary:** Completed the final Phase 11 production validation report for SQLite activation review fixes. The report records commit hashes, focused-test evidence, full-suite evidence, clean rebuild evidence, git status evidence, CI status, and production readiness conclusions for P11-R1 through P11-R7.

**Files changed:** `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This closes the production review evidence package for Phase 11 SQLite activation hardening.

**Validation:** Documentation-only update based on validated HEAD `3e65609`. Latest Build and Test CI run `29660270878` passed for `3e65609`; `git status --short` produced no output before editing the final report.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `e2b9e90` | P11-R7 — Add end-to-end SQLite production upgrade test

**Summary:** Added a focused production upgrade integration test covering TXT fixture storage, manual SQLite migration, verification, archive creation, activation-state creation, startup validation, runtime read, runtime write, restart, post-restart validation, and no TXT fallback files in SQLite runtime storage.

**Files changed:** `tests/test_migrate_sqlite.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This adds coverage proving the approved upgrade path can migrate legacy TXT data to SQLite, activate through the production startup gate, accept runtime writes, persist them across restart, and avoid silently writing TXT fallback files.

**Validation:** Focused P11-R7 test passed (`1` case, `92` assertions). Full suite passed (`666` cases, `4491` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` completed with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`; `containercpd` compilation was continued after the tool timeout with `cmake --build build2 --target containercpd -- -j1` and completed successfully.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `e18a805` | P11-R6 — Complete SQLite filesystem security validation

**Summary:** Added SQLite activation startup security checks for storage directories, database files, activation-state files, and activation archive paths. Startup now rejects unsafe ownership, group/world-writable permissions, symlinked archive paths, non-regular activation-state paths, and unexpected filesystem objects before opening SQLite.

**Files changed:** `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** SQLite activation fails closed when startup inputs are exposed through unsafe filesystem permissions, wrong ownership, symlinks, or unsupported object types. Startup does not fall back to TXT storage on these security failures.

**Validation:** Focused P11-R6 tests passed (`6` cases, `144` assertions). Affected P11-R3 missing-archive regression test passed (`1` case, `7` assertions). Full suite passed (`665` cases, `4399` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings. Ownership mismatch behavior is implemented but not directly exercised in local tests because changing test-file ownership is not safe or portable in the normal developer workflow.

---

## 2026-07-18 | `51429f0` | P11-R5 — Complete SQLite production failure handling tests

**Summary:** Added focused production failure handling tests for SQLite startup validation. The new tests cover missing activation state, corrupted activation state, archive validation failure, migration state mismatch, unsupported schema version, and SQLite open failure while asserting fail-closed startup, no TXT fallback files, and unchanged SQLite marker state or unchanged corrupt database file.

**Files changed:** `tests/test_sqlite_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This adds validation coverage proving SQLite activation failures stop startup without silently falling back to TXT storage or mutating existing SQLite state.

**Validation:** Focused P11-R5 tests passed (`6` cases, `176` assertions). Full suite passed (`659` cases, `4255` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `7164301` | CI — Stabilize SQLite reopen verification test

**Summary:** Updated the importer verification test that checks wrong SQLite reopen behavior to use an explicit empty SQLite backend under the test fixture directory instead of `/nonexistent_storage_dir`. The test now deterministically verifies that reopen comparison fails on mismatched SQLite data across both CI and root-local environments.

**Files changed:** `tests/test_importer.cpp`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This stabilizes CI coverage for SQLite migration verification after the fail-closed startup validation changes.

**Validation:** Focused `Storage failure detected on corrupt db reopen` test passed (`1` case, `12` assertions). Full suite passed (`653` cases, `4079` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` completed with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`; `containercpd` compilation was continued after the tool timeout with `cmake --build build2 --target containercpd -- -j1` and completed successfully.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `815e1cc` | P11-R4 — Propagate SQLite write failures

**Summary:** Replaced ignored SQLite `try_save_*` results with controlled exceptions from public SQLite-backed save methods, so failed writes are visible to callers instead of being silently ignored. Added focused parent, child, and mail configuration write-failure coverage and updated existing foreign-key rollback tests to assert caller-visible failures.

**Files changed:** `libs/storage/SQLiteStorage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** SQLite-backed writes now fail closed with `SQLite save failed for <resource>` when the database rejects or cannot complete a write. SQLite mode no longer silently drops failed writes or falls back to TXT files for those write paths.

**Validation:** Focused P11-R4 tests passed (`3` cases, `22` assertions). Affected FK and rollback reruns passed. Full suite passed (`653` cases, `4073` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `c727106` | P11-R3 — Validate SQLite activation-state consistency

**Summary:** Extended SQLite startup validation so activation state must reference a real completed migration archive. Startup now validates the migration ID, state schema version, database path, normalized archive path, archive integrity, and archive manifest fields before opening SQLite.

**Files changed:** `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** If `storage.backend=sqlite` is configured and `storage-state.json` points to a missing, invalid, relocated, or inconsistent migration archive, daemon startup fails closed instead of accepting the activation state.

**Validation:** Focused P11-R3 tests passed (`7` cases, `46` assertions). Full suite passed (`650` cases, `4043` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `8e18db1` | P11-R2 — Strict SQLite activation-state parsing

**Summary:** Replaced SQLite activation-state substring extraction with a strict typed parser. Startup now rejects malformed JSON, duplicate keys, missing required keys, unknown keys, wrong value types, invalid strings, and invalid enum values before continuing SQLite activation.

**Files changed:** `libs/storage/Storage.cpp`, `libs/storage/Storage.h`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** If `storage.backend=sqlite` is configured and `storage-state.json` does not match the approved activation-state schema, daemon startup fails closed with an activation-state validation error instead of accepting ambiguous or malformed content.

**Validation:** Focused P11-R2 tests passed (`8` cases, `44` assertions). Full suite passed (`643` cases, `3969` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings. Archive existence and migration/schema consistency checks remain scoped to P11-R3.

---

## 2026-07-18 | `c557601` | P11-R1 — Remove automatic SQLite schema migration from startup

**Summary:** Removed automatic schema migration from `Storage` SQLite startup. Startup now opens the configured database and validates existing schema metadata/version instead of running `MigrationEngine::migrate()`.

**Files changed:** `libs/storage/Storage.cpp`, `libs/storage/Storage.h`, `tests/test_sqlite_storage.cpp`, `tests/test_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** `containercpd` startup no longer creates or updates SQLite schema. If SQLite is configured with missing or unsupported schema metadata, startup fails closed instead of migrating automatically.

**Validation:** Focused P11-R1 tests passed (`2` cases, `19` assertions). Full suite passed (`635` cases, `3925` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, member reorder warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `545a4ce` | P11-21 — Phase 11 final validation

**Summary:** Completed final Phase 11 validation for SQLite activation. Clean rebuild, CTest, full doctest, and worktree status checks were run against validated code HEAD `f9036aa`.

**Files changed:** `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No functional behavior change; this closes Phase 11 validation records.

**Validation:** Clean rebuild of `containercp_tests` and `containercpd` succeeded. CTest passed (`1/1`). Full doctest suite passed (`633` cases, `3822` assertions). Worktree was clean after validation.

**Known risks:** Clean rebuild still emits existing compiler warning debt, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, and misleading indentation warnings in legacy/archive/test code.

---

## 2026-07-18 | `046e400` | P11-20 — SQLite activation production runbook

**Summary:** Added a production runbook for SQLite activation covering prerequisites, migration, activation, validation, failure handling, rollback, and operator safety warnings.

**Files changed:** `docs/sqlite-activation-runbook.md`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** Operators now have a documented procedure for safely migrating to SQLite and rolling back to legacy TXT if activation fails.

**Validation:** Documentation-only change; full suite from P11-19 passed (`633` cases, `3822` assertions), and `containercpd` target built successfully before this runbook update.

**Known risks:** Runbook assumes legacy TXT files remain present for configuration-only rollback.

---

## 2026-07-18 | `173db12` | P11-19 — SQLite startup integration test

**Summary:** Added end-to-end integration coverage proving a migrated SQLite database opens through the production startup validation gate and exposes all checked runtime snapshots.

**Files changed:** `tests/test_migrate_sqlite.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No functional behavior change; this validates that successful migration output can be activated by the daemon startup path.

**Validation:** Focused P11-19 test passed (`1` case, `19` assertions). Full suite passed (`633` cases, `3822` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-19. SQLite startup observability logs make migration/storage tests more verbose.

---

## 2026-07-18 | `d824ec2` | P11-18 — SQLite site_id=0 sentinel validation

**Summary:** Added runtime validation that approved `site_id=0` sentinel records survive SQLite writes and validated restart.

**Files changed:** `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No behavior change; this is validation coverage proving approved orphan/admin/external sentinel records remain valid after SQLite activation.

**Validation:** Focused P11-18 test passed (`1` case, `24` assertions). Full suite passed (`632` cases, `3803` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-18. SQLite startup observability logs make storage tests more verbose.

---

## 2026-07-18 | `d8fd466` | P11-17 — SQLite activation-state security

**Summary:** Hardened SQLite startup validation by rejecting symlinked or non-regular `storage-state.json` files before reading activation state JSON.

**Files changed:** `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** If `storage.backend=sqlite` is configured and `storage-state.json` is a symlink or non-regular file, startup now fails with a descriptive error instead of following the path.

**Validation:** Focused P11-17 test passed (`1` case, `4` assertions). Full suite passed (`631` cases, `3779` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-17. SQLite startup observability logs make storage tests more verbose.

---

## 2026-07-18 | `615e8b3` | P11-16 — SQLite migration operator workflow

**Summary:** Added explicit operator next steps to successful SQLite migration diagnostics, including the required config change, daemon restart validation, and legacy archive retention guidance.

**Files changed:** `libs/storage/MigrationOrchestrator.cpp`, `tests/test_migrate_sqlite.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** `containercp storage migrate-to-sqlite` success output now tells operators how to activate SQLite safely after migration.

**Validation:** Focused P11-16 test passed (`1` case, `6` assertions). Full suite passed (`630` cases, `3775` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-16. SQLite startup observability logs make migration tests more verbose.

---

## 2026-07-18 | `526e410` | P11-15 — SQLite startup observability

**Summary:** Added `STORAGE` category logs for SQLite backend startup. Startup now logs backend selection, validation success, readiness, and fail-closed exception reasons.

**Files changed:** `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** Operators now see clear SQLite startup logs, including descriptive failure reasons when activation state or validation checks fail.

**Validation:** Focused P11-15 tests passed (`2` cases, `8` assertions). Full suite passed (`629` cases, `3769` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-15. Test output is more verbose because SQLite startup paths now emit observability logs.

---

## 2026-07-18 | `e855ff6` | P11-14 — SQLite failure handling

**Summary:** Added fail-closed startup handling for symlinked SQLite database paths. Startup validation now inspects the configured database path with `lstat()` and rejects symlinks before opening SQLite.

**Files changed:** `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** If `storage.backend=sqlite` points to a symlinked `containercp.db`, daemon startup now fails with a descriptive error instead of following the link.

**Validation:** Focused P11-14 test passed (`1` case, `4` assertions). Full suite passed (`627` cases, `3761` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-14.

---

## 2026-07-18 | `40f703e` | P11-13 — SQLite restart persistence

**Summary:** Added restart persistence validation for SQLite activation. The test writes all runtime resource categories, closes storage, writes activation state, reopens with startup validation enabled, and verifies all checked snapshots still load.

**Files changed:** `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No functional behavior change; this is validation coverage proving SQLite-backed data survives restart and production startup validation.

**Validation:** Focused P11-13 test passed (`1` case, `37` assertions). Full suite passed (`626` cases, `3757` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-13.

---

## 2026-07-18 | `e954568` | P11-12 — SQLite read-path validation

**Summary:** Added focused validation that SQLite-mode runtime reads use SQLite only and ignore legacy TXT files, including poisoned TXT content left beside `containercp.db`.

**Files changed:** `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No functional behavior change; this is validation coverage for SQLite backend reads. It confirms legacy TXT files cannot affect runtime reads after SQLite activation.

**Validation:** Focused P11-12 tests passed (`2` cases, `51` assertions). Full suite passed (`625` cases, `3720` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-12.

---

## 2026-07-18 | `f3dd14e` | P11-11 — SQLite write-path validation

**Summary:** Added focused validation for SQLite-mode write behavior after runtime repository wiring. The tests prove replacement writes commit to SQLite, omitted records are removed, legacy TXT files are not created, and failed child-table writes roll back without losing the last committed state.

**Files changed:** `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No functional behavior change; this is validation coverage for SQLite backend writes. It reduces risk that SQLite-mode runtime mutations silently fall back to TXT or partially apply failed child writes.

**Validation:** Focused P11-11 tests passed (`2` cases, `29` assertions). Full suite passed (`623` cases, `3669` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-11.

---

## 2026-07-18 | `7a616a5` | P11-10 — Runtime repository SQLite wiring

**Summary:** Completed SQLite runtime routing for the two remaining TXT-only resources, `backups` and `auth_users`, and verified the full 17-resource runtime set through SQLite snapshots.

**Files changed:** `libs/storage/SQLiteStorage.h`, `libs/storage/SQLiteStorage.cpp`, `libs/storage/Storage.h`, `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** When SQLite is the configured backend, backup metadata and auth users now persist to `containercp.db` instead of legacy TXT files. The runtime storage abstraction no longer leaves `backups` or `auth_users` on the legacy path in SQLite mode.

**Validation:** Focused P11-10 tests passed (`2` cases, `62` assertions). Full suite passed (`621` cases, `3640` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-10.

---

## 2026-07-18 | `23bfe33` | P11-09 — No silent SQLite fallback

**Summary:** SQLite backend selection is now loaded during normal daemon startup before `ServiceRegistry` constructs `Storage`, so `storage.backend=sqlite` cannot be ignored and silently fall back to legacy TXT storage.

**Files changed:** `app/containercpd/main.cpp`, `tests/test_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** If SQLite is configured and startup validation fails, `containercpd` exits non-zero before starting REST API or Web UI listeners. Operators see the storage validation error instead of a daemon that starts on legacy storage.

**Validation:** Focused P11 tests passed (`7` cases, `14` assertions). Full suite passed (`620` cases, `3577` assertions). Daemon namespace validation with missing activation state exited `134`, was not alive after 2 seconds, and had no listeners on the selected API/UI ports.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to this change; no new warning was introduced by P11-09.

---

## v0.6.0 — 2026-07-16

**Stable release.** See `docs/release-notes-v0.6.0.md` for full release notes.

**Validation:** All 94 items across 5 stages passed. 242 deterministic tests.
257 full suite. Zero compiler warnings. Browser verification complete.

**RC1:** `v0.6.0-rc1` (2026-07-16) — validated and promoted to stable.

---

## v0.6.0-rc1 — 2026-07-16

**Release scope:** DNS and Mail release. First release candidate for v0.6.

### Mail (ARCH-006)
- MailDomain resource with 4 modes (Disabled, LocalPrimary, ExternalRelay, SplitM365)
- Mailbox CRUD with SHA-512-CRYPT password hashing
- Mail alias support with domain-level routing and virtual_alias_maps
- Docker mail stack: Postfix, Dovecot, Redis, Rspamd (stock images)
- DKIM key generation via OpenSSL, stored in MailDomain, Rspamd milter signing
- TLS configuration for Postfix + Dovecot with CertificateStore paths
- External relay mode: per-domain transport maps
- Split-M365 mode: local mailboxes + catch-all relay with LMTP routing
- Runtime synchronization: 11 mail CRUD handlers trigger config regeneration
- Mail health reporting (Postfix/Dovecot/Redis status via HealthRegistry)
- Module lifecycle (activate/deactivate/status via MailModuleState)
- Mail reload (`POST /api/mail/reload`) and recover (`POST /api/mail/recover`)
- SMTP server fixes: bookworm base image, chroot, socket cleanup, DNS resolution
- Smarthost API (`GET/POST /api/mail/smarthost`) with TLS + SASL support
- DKIM signing fix for PHP Mail: `allow_username_mismatch=true`
- PHP Mail enable/disable per site

### DNS Diagnostics (ARCH-007)
- DnsCheckService using c-ares library (A, AAAA, MX, TXT, CNAME, NS, SOA, CAA)
- 60s in-memory cache with refresh=1 bypass and concurrent access protection
- `GET /api/domains/<domain>/dns-check` with type filtering and error semantics
- Domain List with progressive DNS/Runtime/Health column loading
- Domain Detail with 5 tabs: Overview, DNS Records, Mail, Security, Health
- Configured vs Published comparison for A, AAAA, MX, SPF, DKIM, DMARC, CAA, MTA-STS, TLS-RPT
- SPF analysis (RFC 7208) with SpfAnalyzer — ip4, ip6, a, mx, include, redirect, all
- DMARC Wizard with 3 policies (Monitor=none, Quarantine, Reject)
- Evidence/Why panels with expected/actual/reason/fix for all record types
- Context-aware Health Score (9 weighted checks, grade boundaries, mail/no-mail context)
- Admin-panel virtual system Site and Domain (site_id=0) with capability fields
- 16 commits, 257 tests, zero compiler warnings

### SSL and Security
- ACME HTTP-01 with Let's Encrypt (staging + production environments)
- CertificateStore with versioned metadata and auto-renewal
- SSL REST API (issue, renew, enable, disable, HTTP→HTTPS redirect)
- SSL Web UI page with status overview
- Admin-panel SSL (site_id=0) with certificate management

### Tests and Reliability
- Deterministic test suite: 242 tests
- Live-DNS integration suite: 15 tests (tagged [integration])
- API handler tests through Router dispatch with FakeDnsCheckService
- JSON syntax validation in regression tests
- CTest suite separation (deterministic vs integration)
- Thread-safe concurrent cache access tests

### Important Fixes
- Site ID 0 foreign-key collision in MailDomain lookup (frontend)
- JSON generation bug: missing closing quote in DomainViewService
- Stale loader race condition in HealthCache
- ID-0 collision in mail association (domain_id=0 matching)
- Admin-panel site_id=0 cannot be removed via generic endpoints
- SPF nesting bug in Health scoring (DKIM/DMARC outside rootDns block)
- DMARC parser: `indexOf('p=none')` matched `sp=none`

---

## ARCH-007 — DNS Diagnostic Center (2026-07-16)

**Epic status:** COMPLETED

**Commits:** `8f7a249..72a2333` (16 commits)

**Summary:** Read-only DNS diagnostic center providing live DNS resolution,
Configured vs Published comparison, health scoring, and security recommendations.

**Major milestones:**

| Phase | Description |
|-------|-------------|
| Phase 0 | Existing code verification, production inspection |
| Phase 1 | DnsCheckService with c-ares backend, record parsing, caching |
| Phase 2 | REST API endpoint, DomainViewService mail fields |
| Phase 3 | Domain list with DNS/Mail/Runtime/Health columns |
| Phase 4 | Domain detail with 5-tab layout (Overview, DNS Records, Mail, Security, Health) |
| Phase 5 | Configured vs Published comparison + copy buttons |
| Phase 6 | Mail tab with conditional MailDomain display |
| Phase 7 | Security tab, DMARC Wizard, evidence/why panels |
| Phase 8 | Health scoring engine (9 check types, weighted, context-aware) |
| Phase 9 | Unit tests, API handler tests, frontend verification |

**Key components:**
- `libs/dns/DnsCheckService.h/.cpp` — c-ares DNS resolution
- `libs/dns/SpfAnalyzer.h/.cpp` — RFC 7208 SPF analysis
- `libs/dns/DnsCheckHandler.h/.cpp` — production API handler
- `libs/api/SitesViewService.h/.cpp` — enriched sites with admin panel
- `libs/domain/DomainViewService.h/.cpp` — enriched domains with admin panel
- `libs/network/NetworkService.h/.cpp` — public IP detection
- `web/app.js` — full GUI (domain list, detail, tabs, evidence panels)
- `web/js/utils.js` — health scoring, formatters, comparison helpers
- `web/js/cache.js` — DNS/Health caching with TTL

**Admin panel (site_id=0):** Virtual system domain and site synthesized at the
view layer. Protected from deletion. Runtime N/A. SSL applicable.

---

## 2026-07-15 | `db07c1a` | Fix DKIM signing for PHP Mail — username mismatch

**Root cause:** Rspamd's `dkim_signing` module defaults to `allow_username_mismatch=false`.
When PHP Mail sends via msmtp, the SASL username (`site-11@php.containercp.internal`)
differs from the `From:` header (`wordpress@unity.softico.ua`), causing the module
to skip signing. SnappyMail worked because its SASL username matches the `From:` address.

**Fix:**
- Added `allow_username_mismatch = true` to generated `dkim_signing.conf`
- Removed all experimental patches (Docker image patch, Lua wrappers, settings.conf overrides)
- Stock Rspamd image restored — no modifications to `dkim_signing.lua`

**update.sh improved:**
- Mail stack Docker images (Rspamd, Dovecot, Postfix, SnappyMail) now rebuilt on each update
- Prevents stale images after Dockerfile changes

**Changed files:** `libs/mail/providers/DockerMailProvider.cpp`, `docker/mail/Dockerfile.rspamd`, `scripts/update.sh`

---

## 2025-07-09 | Phase 1–5: Runtime management

- Runtime subsystem with `RuntimeActionExecutor`, `ServiceRole`, `CommandExecutor`
- Site Details page redesigned as Website Management Center with Runtime card
- Runtime card shows Frontend/PHP/Database/Redis status + restart actions
- Runtime architecture refactoring: `ServiceRole` abstraction, `ContainerStatus` moved to executor
- Phase 3 fix: restart-all semantics, restart-db/redis actions added
- Phase 5 cleanup: moved container status inspection into `RuntimeActionExecutor`
- Phase 4: Sites UI restart actions (⚡ dropdown), Phase 5: moved to Site Details

See `docs/changelog/runtime-phases.md` for detailed entries with commit hashes,
file changes, validation results, and known risks.

---

## 2025-07-08 | SSL & HTTPS subsystem

- ACME HTTP-01 challenge via Web UI (staging + production)
- Bootstrap simplified (removed SSL step)
- Admin Panel HTTPS on port 443
- Let's Encrypt integration with auto-renewal
- `CertificateStore`, `RenewalScheduler`, `PemCertificateProvider`
- HTTPS status display in Sites Runtime card (consumes `CertificateStore`)

See `docs/changelog/ssl-subsystem.md` for detailed per-commit entries.

---

## 2025-07-08 | RC2 — Stability & Production Foundation

- Daemon architecture, REST API hardening
- Deployment scripts, update mechanism
- Port management refactoring
- Bug fixes for login, site removal, and rollback

See `docs/changelog/rc2-stability.md` for detailed entries.

---

## 2025-07-08 and earlier | Earlier development

- Multi-site Docker networking (ARCH-004)
- Web UI v0.5, PHP hosting, profiles
- CLI tooling, template engine
- Sprint reviews and infrastructure setup

See `docs/changelog/early-development.md` for detailed entries.

---

### Risks (current)

- Existing sites with host-port allocation retain old compose template
- Fresh site creation uses new template (always overwritten on disk)
- Deprecated PortManager not yet removed — cleanup planned
- `RuntimeActionExecutor` requires Docker Compose v2+
- Async jobs cancelled/marked failed if daemon shuts down during execution

---

## 2026-07-11 | Mail module hardening

- Network isolation: LMTP port 24 removed from host, ports bound to 127.0.0.1
- LMTP via Docker DNS (`containercp-mail-dovecot:24`) instead of `127.0.0.1:24`
- Router consolidation: 6 prefix handlers → 2 dispatchers, no 404 fallthrough
- Transactional `apply_config()`: generate → `postfix check` → reload → rollback
- Self-signed TLS cert auto-generated on fresh install (`ensure_certificate()`)
- Certificate status reported in health endpoint (valid/self-signed/expired/missing)
- Alias self-loop detection, `postmap -q` validation before apply
- Process-level health checks: `postfix status`, `doveadm who`, `redis-cli ping`
- Health status model: ok / degraded / error
- E2E test script: `scripts/test-mail-routing.sh`
- All aliases now written to Postfix `virtual_alias_maps` (was `(void)aliases;`)
- Port publishing fixed: Postfix 25/465/587, Dovecot 143/993 exposed on host

---

## 2026-07-11 | SMTP + DNS + Smarthost

- SMTP server fixes: Postfix master starts reliably (base image → debian:bookworm,
  stale socket cleanup in entrypoint, virtual_mailboxes mount, empty map files,
   Rspamd milter temporarily disabled, later re-enabled for DKIM signing)
- Postfix config: compatibility_level, mynetworks, smtpd_relay_restrictions,
  smtp_host_lookup, maillog_file (direct file logging, no syslog dependency)
- Docker DNS fix: resolv.conf with Google DNS, chroot jail copy, `dns: 8.8.8.8`
- Smarthost API: `GET /api/mail/smarthost`, `POST /api/mail/smarthost`
  ```json
  {"enabled":true,"host":"smtp.gmail.com","port":587,
   "username":"user@gmail.com","password":"app-password"}
  ```
- DKIM DNS record format (add TXT to your DNS provider):
  ```
  Type:  TXT
  Name:  dkim._domainkey.<your-domain>
  Value: v=DKIM1; k=rsa; p=<your-public-key>
  ```
- Direct MX delivery verified: admin@maillab.softi.co → powern76@gmail.com
  (SPF: PASS, DMARC: PASS, TLS: AES_256_GCM_SHA384)

---

## 2026-07-13 | Existing site upgrade path — trusted proxy + mod_rewrite

- `VestaSiteImporter::upgrade_site()` — upgrades existing WordPress sites:
  - Checks/fixes Apache mod_rewrite in 00-load-modules.conf
  - Adds trusted proxy block to wp-config.php (BEGIN CONTAINERCP TRUSTED PROXY)
  - PHP syntax check with backup/restore on failure
- CLI: `--upgrade` flag — runs without --backup
- DaemonApp: handles --upgrade mode
- All existing sites upgraded on production (test-gui-*, testssl, unity)

## 2026-07-13 | Trusted proxy HTTPS detection + Apache mod_rewrite

- Trusted proxy block added to wp-config.php during SQL import:
  - Reads `X-Forwarded-Proto` header, sets `$_SERVER['HTTPS'] = 'on'`
  - Only applies to requests through central nginx proxy
  - Idempotent: `// BEGIN CONTAINERCP TRUSTED PROXY` / `// END CONTAINERCP TRUSTED PROXY`
- Apache mod_rewrite: `LoadModule rewrite_module` in generated 00-load-modules.conf
- Nginx HTTPS vhost sends: `X-Forwarded-Proto https`, `X-Forwarded-Port 443`, `X-Forwarded-Ssl on`
- Fixes: ERR_TOO_MANY_REDIRECTS, 404 on pretty permalinks

## 2026-07-13 | Declarative proxy sync — orphan cleanup, HTTPS generation, validation

- `NginxProxyProvider::sync_all_proxies()` — full declarative sync:
  - Removes orphan .conf files (no proxy entry → file deleted)
  - Generates correct HTTP/HTTPS config based on CertificateStore
  - Validates each config with nginx -t
  - Checks upstream container existence
- `sync_all_https_configs()` now delegates to `sync_all_proxies()`
- Fix: unity.softico.ua now has HTTPS (301) after sync
- Fix: orphan proxy configs auto-removed on startup

## 2026-07-13 | ContainerCP PHP runtime with MySQL extensions

- New Docker image: `ghcr.io/powern/containercp-php:8.4` (php:8.4-fpm + mysqli + pdo_mysql)
- Parameterized Dockerfile: `docker/php/Dockerfile` with `ARG PHP_VERSION`
- ServiceRegistry: legacy PHP images auto-migrated to ContainerCP image on restart
- PHP container preflight: `import_sql()` checks mysqli/pdo_mysql before destructive ops
- Error if missing: "Target PHP runtime missing required MySQL extensions"
- Example site (site-11) upgraded: `docker compose stop → rm → compose up -d php`
- `scripts/update.sh` now builds PHP image during update
- update.sh builds `ghcr.io/powern/containercp-php:8.4` if Dockerfile present

## 2026-07-12 | VestaSiteImporter — Stage 2: web file import

- `import_files()` — extract, safety-copy, rsync, ownership fix, container restart
- Safety-copy rollback: restores original public/ on failure
- Path traversal protection via `realpath` prefix check
- Hidden files preserved (`.htaccess`, `.user.ini`, `.well-known`)
- Web UI: Import files button after Stage 1
- API: `POST /api/migration/vesta/import-files`
- CLI: `--import-files` flag

## 2026-07-12 | VestaSiteImporter — MyVestaCP migration tool (Phase 1-2)

- New CLI: `migrate-vesta-site --backup --domain --owner [--dry-run]`
- `VestaSiteImporter` — read-only inspection of MyVestaCP backup archives
- Secure tar listing, domain extraction, web root detection (public_html/public/htdocs)
- WordPress detection via wp-config.php parsing (regex, single+double quotes)
- SQL dump discovery in archive
- Dry-run mode: shows manifest without system changes
- 7 unit tests for inspect/parsing/path validation

## 2026-07-12 | SnappyMail webmail integration

- New container: `containercp-mail-snappymail` (alpine + nginx + php84)
- Image: `ghcr.io/containercp/mail-snappymail:latest`
- Accessible at `https://<server-hostname>/webmail/` via nginx proxy
- Connects to Dovecot IMAP and Postfix SMTP (STARTTLS auth)
- New `webmail_upstream` param in `ProxyConfigBuilder::Params`
- Web UI sidebar: Webmail link added between Mail and Proxy
- Health check: snappymail container status reported in /api/health

---

## 2026-07-12 | Rspamd DKIM signing (replaced OpenDKIM)

- OpenDKIM milter was not signing — replaced with Rspamd milter proxy
- `POST /api/mail/domains/<id>/dkim/generate` generates 2048-bit RSA key
- DKIM DNS record stored in `MailDomain::dkim_public_key_dns`, returned via API
- Rspamd `dkim_signing` module signs outbound mail via milter on port 11332
- Postfix milter: `smtpd_milters = inet:containercp-mail-rspamd:11332`
- Config: `worker-proxy.inc`, `worker-normal.inc`, `dkim_signing.conf`, `logging.inc`
- Key permissions: 644 (Rspamd runs as `_rspamd` user in container)
- Bug fixes: `use_esld=false` (eSLD mismatch broke domain key lookup),
  `worker-normal.inc` not generated, `worker-proxy.inc` not mounted in compose
- Docker images: `ghcr.io/containercp/mail-rspamd:latest` (debian:trixie + rspamd)

## 2026-07-18 | P11-03 — Explicit migration command

- New CLI: `containercp storage migrate-to-sqlite` with `--source`, `--database`, `--archive-root`, `--source-version`, `--target-version`, `--confirm`
- Daemon handler validates flags, checks paths, generates UUID v4 migration ID
- Command does NOT start HTTP service (handler-only, no API dependency)
- Requires `--confirm` flag, exits non-zero on failure
- 2 new unit tests (607 total, 3541 assertions)
- Files: `libs/cli/CommandDispatcher.cpp`, `libs/daemon/DaemonApp.cpp`, `tests/test_daemon.cpp`

## 2026-07-18 | P11-02 — Backend selection contract

- `Config` gains `storage_backend()` getter, `set_storage_backend()`, `load_storage_backend()`
- Backend source: `CONTAINERCP_STORAGE_BACKEND` env var → `/srv/containercp/database/storage_backend` file → `"legacy"` default
- `ServiceRegistry` accepts `StorageOptions` from config, validates at startup
- Unknown backend value → startup failure (no silent Txt fallback)
- 5 new unit tests (605 total, 3526 assertions)
- Files: `libs/config/Config.h/cpp`, `libs/core/ServiceRegistry.h/cpp`, `tests/test_storage.cpp`
