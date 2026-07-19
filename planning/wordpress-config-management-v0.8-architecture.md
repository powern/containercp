# WordPress Config Management v0.8 Architecture

## Status

Design proposal for post-v0.7.0 work. This document records the target architecture only. It does not authorize production code, schema changes, Docker Compose changes, REST API route creation, Web UI changes, WP-CLI deployment, real password rotation, production site modification, or changes to `web2.softico.ua`.

## Problem

ContainerCP currently has two different WordPress credential paths:

- Site creation generates database metadata and writes generated credentials to the site `.env`, but it does not provision a complete WordPress application or a managed `wp-config.php`.
- The myVestaCP importer can parse and later rewrite `wp-config.php` during SQL import, but that logic is embedded inside `VestaSiteImporter` and uses narrow regex replacement designed for the import flow only.

The Databases v0.8 work needs a reusable, safe way to inspect and update WordPress database configuration before password rotation, imported database adoption, database verification, and future WordPress site creation can be reliable.

## Current Baseline

| Area | Current behavior | Evidence |
|------|------------------|----------|
| Site model | `Site` stores `db_name`, `db_user`, and `db_password` fields | `libs/site/Site.h` |
| Database model | `Database` stores name, user, password, engine, version, owner, site relation, and enabled flag | `libs/database/Database.h` |
| Site creation | Generates one database record with `safe_db`, `safe_user`, and random password | `libs/operations/SiteCreateOperation.cpp` |
| Site layout | Creates `public`, `www`, `logs`, `tmp`, `ssl`, `backups`, and config directories | `libs/filesystem/SiteLayout.cpp` |
| Compose topology | Mounts `./public` into both web and PHP containers and runs per-site `mariadb` | `libs/docker/ComposeGenerator.cpp` |
| Env generation | Writes `DB_NAME`, `DB_USER`, `DB_PASSWORD`, `MYSQL_ROOT_PASSWORD`, and `REDIS_PASSWORD` | `libs/docker/EnvGenerator.cpp` |
| Imported WordPress parse | myVestaCP inspection extracts simple DB constants and flags variable `DB_NAME` as ambiguous | `libs/migration/VestaSiteImporter.cpp` |
| Imported WordPress update | SQL import rewrites `DB_NAME`, `DB_USER`, `DB_PASSWORD`, and `DB_HOST` and runs `php -l` | `libs/migration/VestaSiteImporter.cpp` |
| Tests | Migration tests cover simple constants, ambiguous `getenv()`, realistic config, and `.bak` rejection | `tests/test_migration.cpp` |

## Goals

- Introduce `WordPressConfigService` as the single owner for WordPress configuration discovery, inspection, validation, and safe updates.
- Support migrated myVestaCP sites and future ContainerCP-created WordPress sites through the same service.
- Detect credential source type before any mutation.
- Support read-only inspection before write operations.
- Update only configurations that are classified as safe and unambiguous.
- Preserve the original file format as much as practical.
- Use atomic write, backup, syntax validation, and rollback for every write.
- Keep credentials out of API responses, CLI output, job messages, logs, browser state, and process lists.
- Provide a foundation that Databases DB-1, imported database adoption, password rotation, and future WordPress creation can reuse.

## Non-Goals

- WordPress core installation, plugin installation, theme management, or CMS updater behavior.
- WordPress admin automation beyond database configuration inspection and update.
- WP-CLI deployment in this document.
- A GUI flow for Databases or WordPress management.
- Physical MariaDB password rotation, grants, or database lifecycle.
- SQLite schema changes.
- Rewriting arbitrary dynamic PHP configuration.
- Modifying real production sites during planning.

## Architecture Principles

- API first: future mutating operations must be available through REST before CLI or Web UI clients.
- Daemon owns business logic; CLI and Web UI must not parse or edit WordPress config directly.
- `WordPressConfigService` owns WordPress config semantics; import and database services call it instead of duplicating regex logic.
- Read-only detection comes before every write.
- Ambiguous configuration is safe to report but not safe to mutate without explicit operator input and a future approved path.
- File writes must be atomic and compensating.
- Secret values are operation inputs, not display data.
- Detection results and mutation results are structured data, not human-only log text.

## Proposed Component

### `WordPressConfigService`

Proposed location: `libs/wordpress/`.

Responsibilities:

- Locate the WordPress document root for a `site_id` or domain.
- Locate candidate `wp-config.php` files under the approved site directory.
- Reject backup files such as `wp-config.php.bak`, `.containercp-before-sql`, and temporary files as active configs.
- Parse database configuration source and values.
- Classify credential source and mutability.
- Expose read-only inspection results to database/import services.
- Perform safe atomic config updates for supported direct constants.
- Validate updated PHP syntax through the site PHP runtime when available.
- Roll back the config file if validation or downstream verification fails.
- Return redacted diagnostics.

Non-responsibilities:

- Physical MariaDB password change.
- Metadata persistence for Database records.
- API serialization.
- Web UI behavior.
- WP-CLI process management.
- Backup archive creation.

## Credential Source Classification

`WordPressConfigService` should classify the credential source before returning values or allowing writes.

| Source | Meaning | v0.8 read | v0.8 write |
|--------|---------|-----------|------------|
| `wp_config_constant` | Direct literal `define('DB_*', 'value')` in active `wp-config.php` | Yes | Yes, when unique and syntactically safe |
| `environment_file` | Config reads from environment values backed by site `.env` | Yes, via approved resolver | Update `.env` only in a later approved task |
| `compose_environment` | Values are supplied directly through Compose environment | Inspect only | No in WP-1 to WP-3 |
| `managed_secret` | Future ContainerCP secret store or generated include owns the value | Inspect only until implemented | Later approved task |
| `included_config_file` | Active `wp-config.php` delegates DB constants to an included file | Inspect include path when contained and static | Later approved task after include safety rules |
| `imported_site_config` | Migrated site config detected from myVestaCP files or imported app state | Yes | Only after explicit adoption/rotation workflow |
| `ambiguous` | Duplicate, conditional, variable, fallback, or dynamic definitions | Metadata only | No |
| `unsupported` | Syntax or layout is known but unsupported | Metadata only | No |
| `unknown` | WordPress config not found or not identifiable | Metadata only | No |

## Inspection Result

The service should return a structured result similar to:

```text
WordPressConfigInspection
  site_id
  domain
  document_root
  config_path
  wordpress_detected
  multisite_detected
  credential_source
  mutability
  db_name_state
  db_user_state
  db_password_state
  db_host_state
  db_name_value_redacted_or_present_flag
  db_user_value_redacted_or_present_flag
  db_host_value_redacted_or_present_flag
  warnings[]
  errors[]
```

Rules:

- Never include `DB_PASSWORD` in normal result serialization.
- Return `password_present = true` instead of the password value.
- Return non-secret values such as database name, user, and host only when needed by internal services; API view models can choose to redact or omit them.
- Include enough source detail to explain why a config is mutable, read-only, ambiguous, or unsupported.

## Detection Rules

The first implementation should support direct literal constants:

- `DB_NAME`
- `DB_USER`
- `DB_PASSWORD`
- `DB_HOST`

Supported forms:

- Single or double quoted constant names.
- Single or double quoted string literals.
- Common whitespace variants.
- Optional trailing semicolon.
- Comments elsewhere in the file.

Detected but not writable in the first update implementation:

- `getenv('DB_NAME')`, `$_ENV`, `$_SERVER`, or variable-based expressions.
- Conditional `if (!defined('DB_NAME'))` blocks.
- Multiple active definitions for the same constant.
- Function calls or concatenation in DB constant values.
- Includes or requires that may define DB constants.
- Values generated by helper functions.
- Multisite constants that are unrelated to database credentials.
- Nonstandard paths outside the approved site root.

The detector must ignore inactive backup files and must not scan outside the configured site directory.

## Update Rules

Supported writes in WP-3:

- Replace one unique direct literal definition each for `DB_NAME`, `DB_USER`, `DB_PASSWORD`, and `DB_HOST`.
- Preserve the existing quote style where practical.
- Preserve surrounding whitespace and unrelated content.
- Write to a temporary file in the same directory.
- Set restrictive permissions based on the original file mode.
- Rename atomically over the original file.
- Keep a timestamped ContainerCP backup outside the public web path when feasible, or beside the file with a clearly non-active suffix if no safer layout exists.
- Run PHP syntax validation.
- Return a rollback token or backup path to the caller for same-job compensation.

Unsupported writes must fail closed with a structured reason.

## Validation

Validation sequence for a write:

1. Resolve site and document root from ContainerCP metadata and layout.
2. Inspect active `wp-config.php` and classify source.
3. Refuse mutation unless source is `wp_config_constant` and all target definitions are unique.
4. Render the replacement in memory.
5. Write temporary file in the same directory.
6. Atomically rename over the original.
7. Run `php -l` through the site PHP container if available, using the container path that maps to the host config path.
8. Let the caller verify the new database connection.
9. Roll back on syntax or connection failure.

## Migration Import Integration

`VestaSiteImporter` should stop owning WordPress config parsing and replacement after `WordPressConfigService` exists.

Migration flow should become:

- Inspect backup config through `WordPressConfigService` parsing helpers.
- Import files unchanged.
- Import SQL into the target database.
- Ask `WordPressConfigService` to update supported direct constants.
- Verify PHP syntax.
- Verify WordPress/database connectivity through the approved database verification boundary.
- Roll back database and config on failure.

This keeps existing migration behavior but moves reusable logic to the correct subsystem.

## Interaction With Databases v0.8

DB-1 read-only inventory may use `WordPressConfigService` only for read-only inspection and credential availability classification. DB-1 must not rewrite `wp-config.php`, rotate credentials, create users, change grants, or normalize imported configuration.

Password rotation and imported database adoption must call `WordPressConfigService` for config updates instead of editing files directly.

## Test Plan

Unit tests:

- Direct constants with single quotes.
- Direct constants with double quotes.
- Whitespace and comments around definitions.
- Escaped characters in string literals.
- Duplicate definitions return `ambiguous`.
- Conditional definitions return `ambiguous` or `unsupported`.
- `getenv()`, `$_ENV`, and `$_SERVER` return environment-backed or ambiguous states.
- Includes are detected but not mutated in WP-3.
- `wp-config.php.bak` is ignored.
- Nonstandard contained path can be inspected but path traversal is rejected.
- Password values are never serialized in public results.

Integration tests:

- Update direct constants, run `php -l`, and preserve unrelated file content.
- Failed syntax validation restores original file.
- Failed database verification restores original file.
- Migrated myVestaCP fixture uses the same parser as the importer.
- DB-1 read-only inspection leaves site files byte-for-byte unchanged.

## Implementation Order

1. WP-1: Current-state discovery and credential source detector.
2. WP-2: `WordPressConfigService` read-only inspection.
3. WP-3: Safe atomic configuration update with tests.
4. WP-4: MariaDB password-change provider.
5. WP-5: `DatabaseCredentialRotationService` saga and compensation.
6. WP-6: CLI command and API job endpoint.
7. WP-7: Site detail GUI action.
8. WP-8: Reuse foundation in future WordPress site creation.
9. DB-1: Resume Databases read-only page only after the credential foundation is stable, unless it proceeds independently without distracting from WP work.

## Open Questions

- Should ContainerCP-created WordPress sites prefer direct constants, environment-backed constants, or a generated include file for new installations?
- Should WordPress config backups live under `site/backups/config/` instead of beside `wp-config.php`?
- Should imported include-file configs be supported in v0.8 or left read-only until v0.9?
- What minimum authentication/authorization milestone is required before exposing rotation through the external Web UI?
