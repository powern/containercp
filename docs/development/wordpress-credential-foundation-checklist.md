# WordPress Credential Foundation Implementation Checklist

## Status

Implementation checklist for approved ContainerCP v0.8 WordPress database credential-management foundation.

This checklist is executable in small commits. Each item has one independently verifiable objective, focused tests, acceptance criteria, and a planned commit message. Do not implement Databases GUI, Adminer, SQL import/export, full WordPress site creation, or production/server changes in this scope.

## Baseline Source Map

| Area | Current behavior | Source |
|------|------------------|--------|
| Site model | `Site` includes transient DB fields `db_name`, `db_user`, `db_password`; these are populated during site create and used for `.env` generation | `libs/site/Site.h`, `libs/operations/SiteCreateOperation.cpp`, `libs/provider/DockerComposeProvider.cpp` |
| Database model | `Database` stores `db_name`, `db_user`, `db_password`, `engine`, `version`, `owner_id`, `site_id`, and `enabled` | `libs/database/Database.h` |
| Database manager | `DatabaseManager::create()` and `remove()` are metadata-only; no physical MariaDB lifecycle | `libs/database/DatabaseManager.cpp` |
| SiteCreateOperation | Generates one database record with sanitized `<domain>_db`, `<domain>_user`, and a random password, then copies the values into the transient `Site` object before provider deployment | `libs/operations/SiteCreateOperation.cpp` |
| EnvGenerator | Writes `DB_NAME`, `DB_USER`, `DB_PASSWORD`, generated `MYSQL_ROOT_PASSWORD`, generated `REDIS_PASSWORD`, and site metadata to `.env` | `libs/docker/EnvGenerator.cpp` |
| ComposeGenerator | Uses `.env` values to bootstrap MariaDB via `MYSQL_DATABASE`, `MYSQL_USER`, `MYSQL_PASSWORD`, and `MYSQL_ROOT_PASSWORD`; PHP gets site metadata only | `libs/docker/ComposeGenerator.cpp` |
| Site layout | Creates `public`, `www`, `logs`, `tmp`, `ssl`, `backups`, `config/*`, and `compose`; no WordPress-specific layout owner exists | `libs/filesystem/SiteLayout.cpp` |
| Docker provider | Resolves site directory as `cfg_.sites_dir() + site.domain + "/"`, writes `.env`, renders Compose, writes web config, creates default `public/index.php`, starts stack | `libs/provider/DockerComposeProvider.cpp` |
| Vesta inspect | Extracts `wp-config.php` from backup archives, finds exact basename, parses simple DB constants, marks variable `DB_NAME` ambiguous | `libs/migration/VestaSiteImporter.cpp` |
| Vesta SQL import | Reads target DB credentials from `.env`, uses `MYSQL_PWD=<password>` in command environment, imports SQL, rewrites `wp-config.php`, runs `php -l`, adds proxy block, restarts PHP | `libs/migration/VestaSiteImporter.cpp` |
| Vesta config update | Uses narrow regex/string replacement, writes directly through `std::ofstream`, keeps `.containercp-before-sql` backup, has rollback copy path | `libs/migration/VestaSiteImporter.cpp` |
| Storage | SQLite and legacy TXT persist `db_password`; verification marks password comparison as sensitive | `libs/storage/SQLiteStorage.cpp`, `libs/storage/Storage.cpp`, `libs/storage/Verification.cpp` |
| Jobs | Jobs are in-memory; API returns `message` directly, so future provider/service errors must be redacted before `JobManager::update()` | `libs/jobs/Job.h`, `libs/jobs/JobManager.cpp`, `libs/api/ApiServer.cpp` |
| API | `GET /api/databases` serializes id, name, user, engine, site_id, enabled and omits password; migration inspect exposes `wp_db_name`, `wp_db_user`, `wp_db_host`, not password | `libs/api/ApiServer.cpp`, `libs/api/JsonFormatter.cpp` |
| CLI | `database-show` prints DB name/user/engine/version/site/enabled, not password; migration output prints DB name/user only | `libs/daemon/DaemonApp.cpp`, `libs/cli/CommandDispatcher.cpp` |
| Site GUI | Site detail page exists; migration UI displays parsed `DB_NAME` and `DB_USER`, never password; no WordPress credentials section exists | `web/app.js` |
| Runtime | `ServiceRole::Database` maps to Compose service `mariadb`; existing runtime actions include `restart-db` | `libs/runtime/ServiceRole.cpp`, `libs/api/ApiServer.cpp` |

## Credential Reference Search Summary

Search terms reviewed: `db_name`, `db_user`, `db_password`, `DB_NAME`, `DB_USER`, `DB_PASSWORD`, `DB_HOST`, `MYSQL_ROOT_PASSWORD`, `wp-config`.

Concentrated owners:

- Models/managers: `libs/site/`, `libs/database/`.
- Persistence: `libs/storage/SQLiteStorage.cpp`, `libs/storage/Storage.cpp`, `libs/storage/SQLiteSnapshotReader.h`, `libs/storage/LegacyDatasetReader.cpp`, `libs/storage/LegacyImporter.cpp`, `libs/storage/Verification.cpp`, `libs/storage/StorageCanonicalizer.h`, `libs/storage/SchemaMigrations.cpp`.
- Generated runtime: `libs/docker/EnvGenerator.cpp`, `libs/docker/ComposeGenerator.cpp`, `libs/provider/DockerComposeProvider.cpp`.
- Migration: `libs/migration/VestaSiteImporter.cpp`, `libs/migration/VestaSiteImporter.h`.
- API/CLI/UI: `libs/api/ApiServer.cpp`, `libs/api/JsonFormatter.cpp`, `libs/daemon/DaemonApp.cpp`, `libs/cli/CommandDispatcher.cpp`, `web/app.js`.
- Tests: `tests/test_migration.cpp`, `tests/test_migration_api.cpp`, `tests/test_sqlite_storage.cpp`, `tests/test_fixture_loader.cpp`, `tests/test_importer.cpp`, `tests/test_schema.cpp`.

## Current Duplication And Intended SSOT

| Data | Current copies | Intended owner during this scope |
|------|----------------|----------------------------------|
| Database metadata | `DatabaseManager`, SQLite/TXT storage, API views | `DatabaseManager` remains metadata owner |
| DB password | `Database.db_password`, SQLite/TXT storage, site `.env`, imported `wp-config.php`, operation memory | `DatabaseCredentialRotationService` owns rotation workflow; `WordPressConfigService` owns application config projection; future secret store deferred |
| WordPress DB constants | `VestaSiteImporter` parsing/update logic only | `WordPressConfigDetector` and `WordPressConfigService` |
| Runtime DB bootstrap env | site `.env`, Compose MariaDB environment | `EnvGenerator` remains bootstrap projection; rotation updates only when an approved `.env` owner exists |
| Physical MariaDB password | MariaDB user table inside per-site service | `MariaDBCredentialProvider` through existing runtime command boundary |
| Job/user-facing status | in-memory `JobManager`, API job JSON, CLI output, Web UI polling | Services must write redacted status only |

## Baseline Validation Evidence

- Initial HEAD: `6213847 docs: design WordPress credential rotation foundation`.
- Worktree before WP-0 edits: clean.
- Baseline configure/build/tests: passed with `cmake -S . -B build-wp0 -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-wp0 --target containercp_tests containercp containercpd -- -j1`.
- Baseline focused migration tests: passed with `build-wp0/tests/containercp_tests -tc="VestaSiteImporter*"` (`31` test cases, `79` assertions).
- Baseline affected runtime/API/storage tests: passed with `build-wp0/tests/containercp_tests -tc="*database*"` (`21` test cases, `176` assertions), `build-wp0/tests/containercp_tests -tc="*Runtime*"` (`12` test cases, `106` assertions), and `build-wp0/tests/containercp_tests -tc="*JsonFormatter*"` (`6` test cases, `6` assertions).
- Baseline full doctest suite: passed with `build-wp0/tests/containercp_tests` (`669` test cases, `4507` assertions).
- Baseline CTest: passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`). An earlier concurrent CTest run was discarded as invalid because it ran in parallel with a direct full-suite run and hit shared test resources.
- Baseline clean build: passed; build output showed no compiler warnings.
- Baseline version checks: passed with `build-wp0/containercp --version` and `build-wp0/containercpd --version` (`0.7.0`).

## WP-0 Checklist And Baseline

### [x] WP-0.1 Final checklist and baseline evidence

Objective: Convert the approved architecture into this ordered checklist, map current source behavior, record credential duplication/SSOT, run baseline validation, and commit no behavior changes.

Affected components: documentation and validation only.

Expected files: `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`.

Security considerations: No source behavior changes; confirms current secret-bearing files and redaction boundaries before implementation.

Focused tests: `build/tests/containercp_tests -tc="VestaSiteImporter*"`, `build/tests/containercp_tests -tc="*database*"`, `build/tests/containercp_tests -tc="*Runtime*"`, `build/tests/containercp_tests -tc="*JsonFormatter*"` where available.

Acceptance criteria: Checklist exists, source map complete, baseline validation recorded, working tree contains only docs/changelog, commit pushed.

Commit message: `docs: add WordPress credential implementation checklist`.

Result: Complete. No behavior changes were made; validation evidence is recorded above.

## WP-1 Credential-Source Detector

### [x] WP-1.1 Add detector model types

Objective: Add `WordPressCredentialSource`, mutability/status enums, value state types, warnings/errors, and redacted inspection data structures.

Affected components: new WordPress subsystem, tests, build configuration.

Expected files: `libs/wordpress/WordPressConfigTypes.h`, `libs/wordpress/WordPressConfigTypes.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/test_wordpress_config_detector.cpp`, `CHANGELOG.md`.

Security considerations: Public-safe structs must not serialize or expose password values by default.

Focused tests: type/string conversion tests and password-redaction tests.

Acceptance criteria: Types compile, conversion coverage exists, no behavior changes outside new subsystem.

Commit message: `wordpress: add credential inspection types`.

Result: Complete. Added `WordPressConfigTypes` with source, mutability, status, value-state, issue severity, public-safe credential values, redacted credential sets, and inspection views. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`5` test cases, `51` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`).

### [x] WP-1.2 Implement read-only detector parser

Objective: Detect supported direct constants and unsupported/dynamic forms without mutating files or interpreting PHP.

Affected components: WordPress detector.

Expected files: `libs/wordpress/WordPressConfigDetector.h`, `libs/wordpress/WordPressConfigDetector.cpp`, `tests/test_wordpress_config_detector.cpp`, `CHANGELOG.md`.

Security considerations: No mutation; no shell; no filesystem traversal; password presence tracked separately from public-safe output.

Focused tests: single quotes, double quotes, whitespace, comments, optional semicolons, special characters, escaped quotes, duplicates, conditionals, `getenv()`, `$_ENV`, `$_SERVER`, variables, includes, concatenation, helper calls, missing content, password redaction.

Acceptance criteria: Detector returns source type, mutability, presence, ambiguity, and unsupported state correctly.

Commit message: `wordpress: add credential source detector`.

Result: Complete. Added `WordPressConfigDetector::inspect_content()` as a read-only parser for `define(...)` calls outside comments/strings. The detector supports direct literal constants, preserves public DB name/user/host values, redacts password presence, classifies environment/server variables, variable references, includes, concatenation expressions, helper calls, duplicates, conditionals, and missing content without interpreting PHP or mutating files. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`11` test cases, `88` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`).

### [x] WP-1.3 Add detector filesystem safety helpers

Objective: Add read-only helpers to classify active `wp-config.php` paths and reject backups, temp files, symlinks, and path escapes.

Affected components: WordPress detector/service precursor.

Expected files: `libs/wordpress/WordPressConfigDetector.*`, `tests/test_wordpress_config_detector.cpp`, `CHANGELOG.md`.

Security considerations: Reject symlinks, non-regular files, traversal, and paths outside approved root; ignore backup/temp names.

Focused tests: missing file, backup file ignored, symlink rejected, path traversal rejected, `site_id=0` safe handling in utility inputs.

Acceptance criteria: Unsafe paths fail closed and leave files untouched.

Commit message: `wordpress: harden config detector path safety`.

Result: Complete. Added `WordPressConfigPathSafety`, `WordPressConfigDetector::inspect_config_path()`, and active filename classification. The helper accepts only regular `wp-config.php` files inside the provided site root and rejects missing roots/files, backup/temp names, traversal outside root, symlinked roots/path components/config files, non-directory parents, and non-regular config paths without reading or mutating file content. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`16` test cases, `110` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`).

## WP-2 Read-Only WordPressConfigService

### [x] WP-2.1 Add `WordPressConfigService` inspection by site/domain

Objective: Resolve site/domain, site root, document root, active config path, and detector result through a reusable service.

Affected components: WordPress service, site/database manager integration, tests, build configuration.

Expected files: `libs/wordpress/WordPressConfigService.h`, `libs/wordpress/WordPressConfigService.cpp`, `tests/test_wordpress_config_service.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `CHANGELOG.md`.

Security considerations: Read-only; reject `site_id=0`; no symlink following; no path outside `Config::sites_dir()`.

Focused tests: valid migrated site, missing site, missing root, missing config, non-WordPress site, site_id=0, path escape.

Acceptance criteria: Structured internal inspection returned; no file changes after inspection.

Commit message: `wordpress: add read-only config inspection service`.

Result: Complete. Added `WordPressConfigService` with read-only inspection by site id and domain, explicit `site_id=0` rejection, site root resolution under the configured sites directory, document-root candidate resolution, safe active `wp-config.php` path validation through the detector helper, file read, and detector result return. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`21` test cases, `138` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`).

### [x] WP-2.2 Add public-safe redaction view

Objective: Provide a public-safe view suitable for API/UI use without exposing `DB_PASSWORD`.

Affected components: WordPress service/API-adjacent helpers.

Expected files: `libs/wordpress/WordPressConfigService.*`, `tests/test_wordpress_config_service.cpp`, `CHANGELOG.md`.

Security considerations: No password, root password, option file path, or secret-bearing diagnostic in public view.

Focused tests: redaction, byte-for-byte no-change, unsafe permissions warning, ambiguous config state.

Acceptance criteria: Public view contains source/mutability/status and non-secret values only.

Commit message: `wordpress: add public-safe config inspection view`.

Result: Complete. Added `WordPressConfigPublicView` and `WordPressConfigService::public_view()` with site id, domain, status/source/mutability strings, DB name/user/host, password-presence boolean, and redacted issues only. The public view contains no config path, site root, document root, raw password, root password, or option-file path. Added read-only unsafe-permission warnings and byte-for-byte no-change tests. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`25` test cases, `162` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`).

### [x] WP-2.3 Refactor migration inspection to reuse detector

Objective: Replace duplicated parse-only logic in `VestaSiteImporter` with shared detector helpers where safe while preserving existing behavior.

Affected components: migration importer, WordPress detector, tests.

Expected files: `libs/migration/VestaSiteImporter.cpp`, `libs/migration/VestaSiteImporter.h`, `tests/test_migration.cpp`, `tests/test_migration_api.cpp`, `CHANGELOG.md`.

Security considerations: Do not expose password in migration JSON; do not change archive extraction behavior except parser ownership.

Focused tests: current migration inspect tests, realistic myVesta fixture tests, JSON no password test.

Acceptance criteria: Existing migration tests remain green; same user-visible inspect behavior.

Commit message: `migration: use shared WordPress credential detector`.

Result: Complete. Replaced the migration inspect-only `wp-config.php` credential parsing block with `WordPressConfigDetector::inspect_content()` while preserving `wp_config_found`, direct-literal `wp_config_parsed`, DB name/user/host manifest fields, dynamic `DB_NAME` ambiguity warnings, and no-password output behavior. Focused validation passed with `build-wp0/tests/containercp_tests -tc="VestaSiteImporter*"` (`31` test cases, `79` assertions), `build-wp0/tests/containercp_tests -tc="*Migration*"` (`39` test cases, `254` assertions), and `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`25` test cases, `162` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`).

## WP-3 Safe Atomic WordPress Config Update

### [x] WP-3.1 Add in-memory direct-constant update renderer

Objective: Render safe replacements for exactly one direct literal target while preserving unrelated content and quote style.

Affected components: WordPress config service/detector.

Expected files: `libs/wordpress/WordPressConfigUpdater.h`, `libs/wordpress/WordPressConfigUpdater.cpp`, `tests/test_wordpress_config_update.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `CHANGELOG.md`.

Security considerations: Reject dynamic, duplicate, conditional, include, variable, concatenation, and helper-call sources.

Focused tests: DB_PASSWORD replacement, single/double quote preservation, special chars, escaped values, duplicate rejection, dynamic rejection, include rejection.

Acceptance criteria: Renderer mutates only supported literal definitions and exposes no secret diagnostics.

Commit message: `wordpress: add safe credential update renderer`.

Result: Complete. Added `WordPressConfigUpdater::render_update()` for in-memory replacement of exactly one supported direct string-literal credential constant. The renderer preserves quote style and unrelated content, escapes replacement values for the existing quote style, ignores commented definitions, and rejects missing, dynamic, included, duplicate, and conditional target definitions with redacted diagnostics. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`31` test cases, `180` assertions). Full CTest passed with `ctest --test-dir build-wp0 --output-on-failure` (`1/1`).

Follow-up blocker fix: Complete. Fixed PHP literal encoding so double-quoted credentials escape `$` and encode newline, carriage return, tab, NUL, and other control characters with PHP semantics; single-quoted credentials escape backslash and single quote and reject unsupported NUL/control values. Replaced proximity-based conditional detection with block-aware scanning that ignores comments and strings while preserving fail-closed rejection for genuinely conditional target definitions. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`38` test cases, `232` assertions), `build-wp0/tests/containercp_tests -tc="VestaSiteImporter*"` (`31` test cases, `79` assertions), `build-wp0/tests/containercp_tests -tc="*Migration*"` (`39` test cases, `254` assertions), and full CTest (`1/1`).

### [x] WP-3.2 Add atomic file writer and rollback handle

Objective: Write updated config with temp file, fsync, atomic rename, mode/ownership preservation, cleanup, and rollback handle.

Affected components: WordPress config service filesystem operations.

Expected files: `libs/wordpress/WordPressConfigUpdater.*`, `tests/test_wordpress_config_update.cpp`, `CHANGELOG.md`.

Security considerations: No symlink writes; no path escapes; temp files protected; cleanup on failure; no password in errors.

Focused tests: ownership/mode preservation where portable, unrelated content preserved, atomic replacement, rename/write failure, temp cleanup, symlink rejection, pre-validation unchanged.

Acceptance criteria: Supported config updates atomically; failures leave original content intact or report rollback state.

Commit message: `wordpress: add atomic config credential updates`.

Result: Complete. Added `WordPressConfigUpdater::update_file_atomic()` and `rollback_file()` with safe path classification, no-symlink regular-file checks, protected same-directory temp file creation, full write, mode preservation, root-only ownership preservation, fsync, atomic rename, parent-directory fsync, temp cleanup, and an in-memory rollback handle containing the previous content and metadata. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`44` test cases, `261` assertions), `build-wp0/tests/containercp_tests -tc="VestaSiteImporter*"` (`31` test cases, `79` assertions), `build-wp0/tests/containercp_tests -tc="*Migration*"` (`39` test cases, `254` assertions), and full CTest (`1/1`).

### [x] WP-3.3 Add PHP syntax validation boundary

Objective: Validate updated PHP config through approved runtime or test substitute and restore atomically on validation failure.

Affected components: WordPress service, runtime command boundary, tests.

Expected files: `libs/wordpress/WordPressConfigService.*`, `tests/test_wordpress_config_update.cpp`, `CHANGELOG.md`.

Security considerations: No shell interpolation; scoped command; redacted output; no password in command text.

Focused tests: syntax success, syntax failure rollback, validation output redaction.

Acceptance criteria: No success until syntax validation passes; rollback verified.

Commit message: `wordpress: validate config updates before completion`.

Result: Complete. Added `WordPressConfigValidator`, `WordPressConfigValidationResult`, and `WordPressConfigUpdater::update_file_atomic_validated()` as an injectable validation boundary around the atomic writer. Updates succeed only after the validator accepts the written file. Validation failures automatically rollback through the same atomic rollback path and return redacted diagnostics; rollback failure reports a manual-recovery state. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`48` test cases, `279` assertions), `build-wp0/tests/containercp_tests -tc="VestaSiteImporter*"` (`31` test cases, `79` assertions), `build-wp0/tests/containercp_tests -tc="*Migration*"` (`39` test cases, `254` assertions), and full CTest (`1/1`).

### [x] WP-3.4 Refactor migration config update to shared updater

Objective: Move importer `wp-config.php` replacement to shared updater while preserving import behavior.

Affected components: migration importer, WordPress updater, tests.

Expected files: `libs/migration/VestaSiteImporter.cpp`, `libs/migration/VestaSiteImporter.h`, `tests/test_migration.cpp`, `tests/test_wordpress_config_update.cpp`, `CHANGELOG.md`.

Security considerations: Preserve rollback and no-secret-output behavior; do not broaden import mutability.

Focused tests: migrated fixture update, importer regression coverage, duplicate/dynamic rejection behavior documented.

Acceptance criteria: Migration tests pass and duplicate parser/update logic is reduced.

Commit message: `migration: use shared WordPress config updater`.

Result: Complete. Refactored migration SQL import credential updates to use `WordPressConfigUpdater` sequence updates and the validation boundary with the existing vector-argv container `php -l` check. The legacy direct regex write path was removed, credential values are rendered through the shared PHP literal encoder, updates happen through the atomic writer, and validation failure rolls back through the updater before migration DB rollback continues. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`49` test cases, `286` assertions), `build-wp0/tests/containercp_tests -tc="VestaSiteImporter*"` (`31` test cases, `79` assertions), `build-wp0/tests/containercp_tests -tc="*Migration*"` (`39` test cases, `254` assertions), and full CTest (`1/1`).

## WP-4 MariaDB Password-Change Provider

### [x] WP-4.1 Add MariaDB credential provider command model

Objective: Add provider interfaces/results for verify, change password, restore old password, user identity, and shared-user risk detection with fake executor tests.

Affected components: database provider boundary.

Expected files: `libs/database/MariaDBCredentialProvider.h`, `libs/database/MariaDBCredentialProvider.cpp`, `tests/test_mariadb_credential_provider.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `CHANGELOG.md`.

Security considerations: No API/CLI/UI logic; no shell strings; result errors redacted.

Focused tests: SQL/command construction, no shell injection, exact `user@host`, missing user, shared user, stopped/missing service, auth failure.

Acceptance criteria: Provider model compiles and fake tests cover all non-live paths.

Commit message: `database: add MariaDB credential provider`.

Result: Complete. Added `MariaDBCredentialProvider`, target/admin/user identity models, provider results, fakeable `MariaDBProcessRunner`, and `CommandExecutor` runner adapter. The provider exposes verify, change password, restore password, and shared-user detection boundaries without API/CLI/UI logic. Focused validation passed with `build-wp0/tests/containercp_tests -tc="*MariaDBCredentialProvider*"` (`6` test cases, `29` assertions), `build-wp0/tests/containercp_tests -tc="*database*"` (`21` test cases, `176` assertions), `build-wp0/tests/containercp_tests -tc="*WordPress*"` (`49` test cases, `286` assertions), and full CTest (`1/1`).

### [x] WP-4.2 Add safe secret transport for MariaDB commands

Objective: Use protected temp defaults files or reviewed stdin mechanism for credentials without password argv exposure.

Affected components: MariaDB provider, filesystem helper, tests.

Expected files: `libs/database/MariaDBCredentialProvider.*`, `tests/test_mariadb_credential_provider.cpp`, `CHANGELOG.md`.

Security considerations: `0600` temp files, cleanup on success/failure, no password in retained stdout/stderr/job/log strings.

Focused tests: special-character passwords, argv redaction, cleanup success/failure, redacted stderr.

Acceptance criteria: Tests prove command vectors do not contain password values.

Commit message: `database: protect MariaDB credential transport`.

Result: Complete. Implemented protected host-side stdin bundles (`0600`) and a fixed in-container shell script that splits stdin into protected temporary option and SQL files, executes `mariadb`, and cleans up. Passwords and SQL are never placed in command argv or result messages; command failures return redacted diagnostics and cleanup is verified in tests.

### [ ] WP-4.3 Document minimum grant model

Objective: Record exact grant assumptions for provider operations before saga integration.

Affected components: documentation only unless constants are needed.

Expected files: `docs/development/mariadb-credential-provider.md`, `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`.

Security considerations: Root remains bootstrap/break-glass only; service account path documented.

Focused tests: documentation-only; no code tests required beyond `git diff --check`.

Acceptance criteria: Operator can review privilege requirements and residual root compatibility risks.

Commit message: `docs: document MariaDB credential provider grants`.

## WP-5 DatabaseCredentialRotationService Saga

### [ ] WP-5.1 Add rotation service state machine and lock

Objective: Add operation states, per-site/database lock, request/result structs, and no-op/fake dependencies for tests.

Affected components: database/wordpress orchestration.

Expected files: `libs/database/DatabaseCredentialRotationService.h`, `libs/database/DatabaseCredentialRotationService.cpp`, `tests/test_database_credential_rotation.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `CHANGELOG.md`.

Security considerations: No real rotation yet; no passwords in states/messages; lock released on all paths.

Focused tests: state order, concurrent conflict, lock release after success/failure.

Acceptance criteria: State machine and locking work with fakes.

Commit message: `wordpress: add credential rotation state machine`.

### [ ] WP-5.2 Implement direct-constant rotation happy path

Objective: Orchestrate site/database resolution, config inspection, old connection verification, new password generation, provider change, config update, runtime apply, DB verification, WordPress verification, site health verification, metadata persistence.

Affected components: rotation service, WordPress service, MariaDB provider, runtime, storage save boundary.

Expected files: `libs/database/DatabaseCredentialRotationService.*`, `tests/test_database_credential_rotation.cpp`, `CHANGELOG.md`.

Security considerations: Explicit operator action only; no unsupported env-backed mutation; no success until all verification passes; no secret messages.

Focused tests: supported migrated direct-literal config, unsupported source rejection, ambiguous rejection, metadata update after provider success, verification sequence.

Acceptance criteria: Full happy path passes with fakes and no password leakage.

Commit message: `wordpress: add database credential rotation saga`.

### [ ] WP-5.3 Implement compensation and manual recovery states

Objective: Restore old MariaDB password, restore old config, reapply runtime, verify old access, and produce manual recovery state if compensation fails.

Affected components: rotation service, WordPress updater, MariaDB provider, runtime fakes.

Expected files: `libs/database/DatabaseCredentialRotationService.*`, `tests/test_database_credential_rotation.cpp`, `CHANGELOG.md`.

Security considerations: No old/new password in persisted diagnostics; no indefinite destructive retry.

Focused tests: config update fails after DB mutation, DB verification fails, WordPress verification fails, site verification fails, compensation succeeds, DB restore fails, config restore fails, runtime restore fails, manual recovery state, no secret persisted.

Acceptance criteria: Every post-mutation failure path compensates or clearly reports manual recovery.

Commit message: `wordpress: add credential rotation compensation`.

### [ ] WP-5.4 Add WordPress/PHP-level verification boundary

Objective: Prove updated `wp-config.php` can establish DB access through scoped PHP execution or safe fake in tests.

Affected components: WordPress service, runtime command boundary, rotation service.

Expected files: `libs/wordpress/WordPressConfigService.*`, `libs/database/DatabaseCredentialRotationService.*`, `tests/test_database_credential_rotation.cpp`, `tests/test_wordpress_config_service.cpp`, `CHANGELOG.md`.

Security considerations: No arbitrary shell interpolation; no password in command text; output redacted.

Focused tests: verification success/failure, command scoping, redaction.

Acceptance criteria: Rotation requires direct MariaDB verification, WordPress-level verification, and site health verification.

Commit message: `wordpress: verify rotated credentials through PHP runtime`.

## WP-6 CLI And REST API

### [ ] WP-6.1 Add API-first rotation endpoint

Objective: Add REST endpoint that queues a rotation job through the same rotation service and returns job id only.

Affected components: API, jobs, service registry wiring, docs.

Expected files: `libs/api/ApiServer.cpp`, `libs/core/ServiceRegistry.h`, `libs/core/ServiceRegistry.cpp`, `docs/api/API_REFERENCE.md`, `tests/test_api.cpp`, `CHANGELOG.md`.

Security considerations: Admin-only according to existing auth model; typed confirmation; no password request/response; concurrent lock conflict; no duplicated business logic.

Focused tests: unauthenticated/unauthorized if supported by current auth test model, missing site, invalid confirmation, unsupported config, job accepted, concurrent conflict, response redaction.

Acceptance criteria: API returns accepted/job id and never password.

Commit message: `api: expose WordPress database credential rotation`.

### [ ] WP-6.2 Add CLI client command

Objective: Add CLI command that requests rotation through daemon/service boundary and prints job/status without passwords.

Affected components: CLI dispatcher, daemon command handler, tests/docs.

Expected files: `libs/cli/CommandDispatcher.cpp`, `libs/daemon/DaemonApp.cpp`, `tests/test_daemon.cpp`, `tests/test_migration_api.cpp` or new CLI tests, `docs/api/API_REFERENCE.md`, `CHANGELOG.md`.

Security considerations: Explicit confirmation; no current/new password accepted from normal CLI path; no password output.

Focused tests: command parsing, confirmation required, output redaction, nonzero failure path where testable.

Acceptance criteria: CLI is client/orchestrator only, not a second implementation.

Commit message: `cli: add WordPress database password rotation command`.

## WP-7 Site Detail GUI Action

### [ ] WP-7.1 Add site detail credential status view

Objective: Display non-secret WordPress database credential source/status on the Site detail page.

Affected components: API public-safe inspection endpoint or included site detail response, Web UI.

Expected files: `libs/api/ApiServer.cpp`, `web/app.js`, `docs/api/API_REFERENCE.md`, GUI/API tests where available, `CHANGELOG.md`.

Security considerations: No password in DOM, JavaScript variables, local/session storage, URL, or API response.

Focused tests: supported migrated site, unsupported source, ambiguous config, response/DOM redaction by static tests if no browser harness exists.

Acceptance criteria: Site detail shows DB name/user/host/source/support state and warnings only.

Commit message: `web: show WordPress credential status on site detail`.

### [ ] WP-7.2 Add rotate action and job progress UI

Objective: Add typed domain confirmation, impact warning, API job submission, progress display, and duplicate-click prevention on Site detail page.

Affected components: Web UI, API job polling.

Expected files: `web/app.js`, `docs/api/API_REFERENCE.md`, UI tests/static checks where available, `CHANGELOG.md`.

Security considerations: No passwords; disable unsupported/ambiguous states; no browser storage; clear compensated/manual recovery result.

Focused tests: confirmation required, unsupported disabled, ambiguous disabled, job progress, success, compensated failure, manual recovery, static no-secret checks.

Acceptance criteria: GUI action is safe and does not touch Databases page.

Commit message: `web: add site database password rotation action`.

## WP-8 Final Integration And Hardening

### [ ] WP-8.1 Remove remaining duplicate WordPress parser/update logic

Objective: Complete safe refactor of `VestaSiteImporter` to shared services after regression tests prove equivalent behavior.

Affected components: migration importer and WordPress services.

Expected files: `libs/migration/VestaSiteImporter.*`, `libs/wordpress/*`, `tests/test_migration.cpp`, `CHANGELOG.md`.

Security considerations: Existing migration behavior remains green; no password exposure.

Focused tests: all migration tests, importer update tests, no-password JSON tests.

Acceptance criteria: Shared WordPress subsystem is the SSOT for parsing/update.

Commit message: `wordpress: consolidate migration credential handling`.

### [ ] WP-8.2 Update operator/API/security documentation

Objective: Document supported/unsupported configs, rotation workflow, compensation/manual recovery, API/CLI/GUI behavior, and residual risks.

Affected components: documentation.

Expected files: `docs/development/wordpress-credential-management.md`, `docs/api/API_REFERENCE.md`, `planning/wordpress-*.md`, `planning/project-status.md`, `CHANGELOG.md`.

Security considerations: Docs must warn about plaintext SQLite/`.env` residual risk and no production rotation without explicit operator action.

Focused tests: docs-only `git diff --check`.

Acceptance criteria: Operator has clear deployment/test procedure for one approved migrated test site.

Commit message: `docs: document WordPress credential operations`.

### [ ] WP-8.3 Run final secret-leak and release validation

Objective: Run complete clean configure/build/tests/static secret checks, record exact counts, and ensure working tree clean after final commit.

Affected components: full repository validation.

Expected files: `docs/development/wordpress-credential-foundation-checklist.md`, `CHANGELOG.md`.

Security considerations: Confirm no passwords in normal outputs, logs, jobs, API, DOM, command construction, or tests beyond intentional literals.

Focused tests: secret-leak grep/static checks, CLI help/version, API regression, migration regression, full doctest, CTest, clean build.

Acceptance criteria: Clean configure, clean rebuild, full doctest, CTest, version checks, `git diff --check`, `git status --short`, and CI pass.

Commit message: `wordpress: finalize credential rotation foundation`.
