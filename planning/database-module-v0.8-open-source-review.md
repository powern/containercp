# Databases Module v0.8 Open-Source Review

## Status

Research note for architecture planning. No component is approved for deployment by this document alone.

## Recommendation

Use Adminer as the default v0.8 database administration tool candidate, but do not deploy it until ContainerCP has safe database lifecycle APIs, credential hygiene, authenticated launch tokens, and audit logging.

Use MariaDB command-line tools for import/export and backup integration. Prefer logical backups with `mariadb-dump` in v0.8. Defer physical online backup tooling with `mariadb-backup` to a later release.

## Reviewed Components

| Component | License | Fit | Recommendation |
|-----------|---------|-----|----------------|
| Adminer | Apache License 2.0 or GPL 2 | Lightweight PHP database admin tool with MariaDB/MySQL support and export/import features | Preferred admin UI candidate |
| phpMyAdmin | GPL-2.0 | Mature MySQL/MariaDB administration UI, broader feature set, larger footprint | Reference or future option, not v0.8 default |
| CloudBeaver Community | Apache-2.0 | Rich multi-database web manager, Java backend and React UI, heavier operational footprint | Not v0.8 default |
| `mariadb-dump` | MariaDB client utility | Official logical dump utility; appropriate for per-site SQL exports | Preferred v0.8 export/backup tool |
| `mariadb` client | MariaDB client utility | Official command-line import/query client | Preferred v0.8 import/lifecycle execution tool |
| `mariadb-backup` | Open-source MariaDB physical backup tool | Production-grade hot physical backups, but restore model and privileges are complex | Defer beyond v0.8 |

## Adminer

Official positioning: database management in a single PHP file. The official site lists support for MySQL, MariaDB, PostgreSQL, SQLite, MS SQL, Oracle, and others through plugins. It states Adminer is free for commercial and non-commercial use under Apache License 2.0 or GPL 2.

Strengths for ContainerCP:

- Very small footprint compared with phpMyAdmin and CloudBeaver.
- PHP-based, matching the existing PHP-oriented site stack.
- Supports MariaDB/MySQL directly.
- Includes database export/import functionality.
- Supports plugins and customization.
- Can run as an isolated sidecar in a site network.
- Good fit for on-demand, time-limited launch.

Risks:

- It is a full database administration surface, equivalent to direct DB access.
- Official security guidance recommends restricting public access and adding web-server protection.
- Auto-login or session bridging must be custom and carefully reviewed.
- Public exposure would create a high-value attack surface.
- Adminer version updates become ContainerCP security maintenance work.

Required controls before adoption:

- No public host port.
- ContainerCP-authenticated reverse proxy gate.
- Short-lived launch token.
- No credentials in URL or browser history.
- Audit log for launch and revoke.
- Clear update strategy for Adminer image/file version.
- Per-site network isolation.

Decision: preferred v0.8 admin UI candidate, but only after lifecycle, backup, and threat-model controls are implemented.

## phpMyAdmin

Official positioning: free PHP tool for administering MySQL and MariaDB over the web. The project supports database/table/user/privilege management, SQL execution, import/export, multi-server administration, schema visualization, and many related operations. The GitHub repository is GPL-2.0 licensed.

Strengths:

- Mature and widely known.
- Strong MariaDB/MySQL focus.
- Rich import/export and user privilege features.
- Familiar to many hosting users.

Risks for ContainerCP v0.8:

- Heavier footprint than Adminer.
- MySQL/MariaDB-specific, while Adminer leaves future multi-engine room.
- Larger web application surface to secure and update.
- More configuration and session-management complexity.
- GPL-2.0 integration and distribution implications need deliberate review if bundled.

Decision: keep as a future option or reference. Do not use as v0.8 default.

## CloudBeaver Community

Official positioning: cloud database manager with Java server and TypeScript/React web UI. The repository states CloudBeaver Community is free and open-source under Apache 2.0.

Strengths:

- Modern multi-database web manager.
- Rich SQL editor and data editor.
- Broad driver ecosystem.
- Apache-2.0 license.

Risks for ContainerCP v0.8:

- Significantly larger runtime footprint than Adminer.
- Requires Java server management and additional persistent configuration.
- Overlaps with ContainerCP's own Web UI and auth surface.
- More complex to make per-site and time-limited.
- Changelog shows frequent security dependency updates, increasing maintenance burden.

Decision: not a v0.8 default. Revisit for a future advanced database-workbench feature if ContainerCP needs a multi-engine SQL workspace.

## MariaDB Command-Line Tools

v0.8 implements MariaDB only, but tool choices should live behind a Database Profile and provider boundary. Future MySQL or PostgreSQL support should select different provider commands without changing the public inventory API.

### `mariadb-dump`

The MariaDB documentation describes `mariadb-dump` as a backup program used to dump a database or collection of databases for backup or transfer. It emits SQL statements for database structures and data, and supports CSV/XML-style outputs. It recommends options such as `--quick` for large tables and documents `--single-transaction` for consistent InnoDB dumps.

Strengths:

- Official MariaDB utility.
- Logical dumps are portable and easy to include in site backups.
- Works well for per-site database export.
- Easier restore model than physical backup tools.

Risks:

- Can be slow for large databases.
- `--single-transaction` consistency depends on storage engine and concurrent DDL behavior.
- Passwords passed on command line are insecure; option files are required.
- Routines/events require additional privileges and explicit options.

Decision: preferred v0.8 export and backup-dump tool.

### `mariadb` Client

The `mariadb` command-line client should be used for lifecycle SQL and import execution through a provider wrapper.

Required controls:

- Argument-vector execution.
- Credentials through option files.
- Strict database/user identifier validation.
- Sanitized stderr in API responses.
- Job-based execution for long operations.

Decision: preferred v0.8 import and lifecycle command tool.

### `mariadb-backup`

The MariaDB documentation describes `mariadb-backup` as an open-source physical online backup tool for InnoDB, Aria, and MyISAM tables. It supports hot online InnoDB backups and production-quality nearly non-blocking full backups. It also requires specific global privileges and has a more complex restore model.

Strengths:

- Production-grade physical online backups.
- Better for large databases and advanced recovery needs.
- Supports incremental backup and PITR workflows in advanced scenarios.

Risks for ContainerCP v0.8:

- Overkill for initial per-site database management.
- Physical restore is harder to combine safely with per-site multi-tenant stacks.
- Requires broader privileges than logical dumps.
- Partial restore workflows are complex.
- Unknown option behavior requires careful operational discipline.

Decision: defer beyond v0.8. Use logical `mariadb-dump` first.

## Password and Secret Handling Options

Current ContainerCP generates alphanumeric passwords with `PasswordGenerator` and stores DB credentials in SQLite and site `.env`. v0.8 should not introduce an external secret store until requirements are explicit, but it must stop avoidable leaks.

Minimum recommended approach:

- Continue using generated credentials for compatibility.
- Do not expose passwords in API responses.
- Use temporary client option files for MariaDB tools.
- Enforce restrictive permissions on credential files.
- Redact secrets from logs and job messages.
- Add password rotation before or alongside Adminer launch.

Future options:

- Local encrypted secret store with a host-held key.
- Integration with systemd credentials, age, SOPS, or a future provider abstraction.
- Per-operation temporary database users for Adminer/export/import.

## Adminer Lifecycle Model Review

| Model | Attack surface | RAM usage | Startup latency | Docker network isolation | Cleanup complexity | Credential lifetime | Maintenance | Multi-node compatibility |
|-------|----------------|-----------|-----------------|--------------------------|--------------------|---------------------|-------------|--------------------------|
| Permanent sidecar | Higher because the admin surface is always running | Higher baseline per site | Low after site start | Good if attached only to the site network | Lower per session, higher long-term container management | Longer-lived session plumbing is more likely | Continuous patching and monitoring for every site | More idle containers across nodes |
| On-demand temporary container | Lower because Adminer exists only during approved sessions | Near zero while inactive | Higher at launch | Good if attached only to target site network | Higher because route, token, and container cleanup must be reliable | Shortest; token and credentials can share a strict TTL | Fewer running components; image still needs patch policy | Better default because work occurs only on target node when requested |

Recommendation: on-demand temporary Adminer. It minimizes attack surface, RAM usage, and credential lifetime. The tradeoff is cleanup complexity; implementation must include token expiry, route revocation, container removal, audit events, and retryable cleanup recovery.

## Final Recommendation

For v0.8, build the Databases module around these defaults:

- MariaDB only.
- Adminer as optional, authenticated, on-demand admin UI.
- `mariadb-dump` for logical exports and backup integration.
- `mariadb` client for imports and lifecycle SQL.
- `mariadb-backup`, phpMyAdmin, and CloudBeaver deferred.

This keeps the feature useful while staying aligned with ContainerCP's current Compose topology and security posture.
