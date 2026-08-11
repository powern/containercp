# WPCLI-001: Existing WP-CLI Source Audit and Modernization Plan

## Audit Baseline

- Audited HEAD: `f24d8467241a6038f944b71172e96e90008519a3`
- Audited commit: `fix: preserve site access during SFTP grants`
- Audit date: 2026-08-11
- Scope: analysis and implementation planning only

## Executive Finding

ContainerCP does not currently implement WP-CLI support.

The source tree contains no `wp` executable invocation, WP-CLI Phar,
WP-CLI image, WP-CLI service, `--path`, `--config-file`, plugin endpoint,
theme endpoint, or WordPress core management endpoint.

The only WP-CLI-looking source symbol is the local variable `wp_cli` in
`libs/migration/VestaSiteImporter.cpp:2048`. It invokes PHP CLI against
WordPress `index.php`; it does not invoke WP-CLI.

The current WordPress subsystem provides:

- WordPress configuration and credential detection.
- Safe path and symlink validation.
- Public-safe credential status.
- Atomic `wp-config.php` updates and rollback.
- MariaDB credential target resolution.
- Database credential rotation jobs.
- A fixed PHP/MariaDB runtime verifier.
- WordPress-aware myVestaCP migration diagnostics and import handling.

## Current Execution Paths

### Credential inspection and rotation

```text
Site Details / Databases UI
  -> GET or POST WordPress credential API
  -> WordPressDatabaseCredentialResolver
  -> WordPressConfigService
  -> WordPressConfigDetector
  -> DatabaseCredentialRotationJobService
  -> DatabaseCredentialRotationService
  -> DatabaseCredentialRotationAdapter
  -> WordPressConfigUpdater / MariaDB provider / WordPressRuntimeVerifier
  -> JobManager result
  -> API and UI polling
```

Important files:

- `libs/api/ApiServer.cpp:783-932`
- `libs/wordpress/WordPressDatabaseCredentialResolver.cpp:25-77`
- `libs/wordpress/WordPressConfigService.cpp:95-284`
- `libs/database/DatabaseCredentialRotationAdapter.cpp:209-499`
- `libs/wordpress/WordPressRuntimeVerifier.cpp:98-150`

This path does not execute WP-CLI.

### myVestaCP migration and PHP diagnostic path

```text
Migration UI or CLI
  -> migration API or daemon request handler
  -> VestaSiteImporter
  -> archive and wp-config inspection
  -> selected site directory and marker validation
  -> Docker/PHP/container commands through CommandExecutor
  -> stdout/stderr/exit code
  -> direct result or background job
  -> API, CLI, or UI response
```

Important files:

- `web/pages/migration.js:6-240`
- `libs/api/ApiServer.cpp:3987-4745`
- `libs/daemon/DaemonApp.cpp:553-741`
- `libs/cli/CommandDispatcher.cpp:323-385`
- `libs/migration/VestaSiteImporter.cpp:528-756`
- `libs/migration/VestaSiteImporter.cpp:1406-1521`
- `libs/migration/VestaSiteImporter.cpp:1524-2130`

The HTTP 500 diagnostic at `VestaSiteImporter.cpp:2043-2053` runs:

```text
docker exec site-<id>-php php -d display_errors=1 <index.php>
```

This is PHP CLI, not WP-CLI.

## Current Resolution Behavior

### Site

The credential path resolves a managed site by numeric `site_id`, then uses
the persisted domain to resolve `<sites_root>/<domain>`.

`WordPressConfigService` rejects traversal, symlinked roots, and unsafe config
paths. It searches `public`, `public_html`, `htdocs`, `www`, `root`, and the
site root itself.

The migration path receives a domain string and constructs paths directly.
Migration API input is not consistently passed through the standard hostname
validation boundary before all path construction.

### Container

The managed compose project is the selected site root. The standard PHP
service is named `php`. Migration also derives names such as:

```text
site-<site_id>-php
site-<site_id>-db
```

The migration SQL path additionally inspects PHP mounts to map the selected
host public directory to the container path.

### Document root

Managed sites mount `./public` into the web and PHP containers.

The current hard-coded container roots are:

- Apache: `/usr/local/apache2/htdocs`
- Nginx: `/var/www/html`

### PHP

PHP commands run inside the selected PHP container, so the binary and
extensions come from that container. However, site creation currently uses
`PhpVersionManager::get_default()` in `DockerComposeProvider`; the persisted
domain PHP version is not used to generate the site's PHP image.

Different sites therefore do not reliably receive different PHP versions
through the current site creation path.

### Unix user and privilege

The systemd unit has no `User=` directive, so `containercpd` runs as root.
WordPress-related Docker commands do not specify `--user`, and no `setuid`,
`setgid`, `sudo`, or `runuser` boundary exists.

`Site.owner` is metadata, not an execution identity.

Migration file import is the only related path that explicitly determines a
UID/GID from `/var/www/html` and uses an Alpine `chown` container to align
imported files with the PHP document-root ownership.

## Process Execution Findings

`runtime::CommandExecutor::run()` uses fork/exec and an argv vector:

- No implicit `/bin/sh`.
- No implicit `bash`.
- stdout and stderr captured separately.
- Exit code returned to the caller.
- No timeout.
- No output bound.

The current WordPress-related command vectors are therefore not shell
concatenations. The broader runtime still has unrelated `std::system()` shell
execution in `DockerRuntime.cpp`, and future WP-CLI code must not reuse that
pattern.

`CommandExecutor::run_safe()` has timeout and output-limit parameters, but the
current WordPress paths use unbounded `run()` instead.

## Security and Correctness Findings

- No WP-CLI binary or missing-binary handling exists.
- No `--path` or `--config-file` is used.
- No site-owner execution context exists.
- A future naive `docker exec` implementation would execute as the root daemon
  context and could create root-owned site files.
- The daemon has root-level Docker access.
- API construction uses `AllowAllAuth`.
- Migration domain input is not centralized through the managed-site resolver.
- `upgrade_site()` probes hard-coded `site-N-web` at
  `VestaSiteImporter.cpp:2143-2145`, which can target an unrelated container.
- Apache migration config validation falls back to the Nginx path
  `/var/www/html/wp-config.php` at `VestaSiteImporter.cpp:2210-2212`.
- Migration diagnostics log raw PHP output and can expose site-controlled data.
- No structured audit record exists for WordPress command execution.
- Current WordPress detection is config/credential detection, not full
  WordPress core/plugin/theme detection.
- No plugin, theme, core, language, cache, or config management is exposed.

## Functionality to Preserve

The following existing components should be reused unchanged unless a test
demonstrates a concrete required correction:

- `WordPressConfigDetector`.
- `WordPressPhpDefineScanner`.
- `WordPressConfigService` path and public-safe behavior.
- `WordPressDatabaseCredentialResolver` exact target matching.
- `WordPressConfigUpdater` atomic update and rollback behavior.
- `WordPressRuntimeVerifier` containment checks and redacted failures.
- Existing WordPress database credential rotation API, CLI, UI, job, and audit.
- Existing site compose layout and `php` service convention.
- Existing `CommandExecutor` argv execution primitive.
- Existing `JobManager` and `JobExecutor`.
- Existing Apache and Nginx WordPress templates.
- Existing migration archive safety checks and ownership correction.

Database password rotation must continue using `WordPressConfigUpdater`; it
must not be replaced with a password-bearing `wp config set` argv.

## Regression Coverage Gap

Current tests cover WordPress config parsing, path safety, redaction, atomic
updates, rollback, credential rotation, migration archive inspection, marker
validation, generic command execution, and fixed PHP runtime verification.

There are no tests for:

- WP-CLI binary or Phar detection.
- WP-CLI `--path` or `--config-file`.
- Runner image, network, mount, or PHP selection.
- UID/GID execution.
- Root-owned file prevention.
- Plugin, theme, core, language, or cache operations.
- WP-CLI command allowlisting.
- WP-CLI timeout/output limits.
- WP-CLI audit records.
- Cross-site or host-path rejection for a WP-CLI operation.
- Real Docker/WordPress integration.
- Multiple sites with different PHP versions.

## Modernization Target

The smallest architecture consistent with the current codebase is:

```text
REST API
  -> typed WordPress operation handler
  -> WordPressConfigService site-context resolution
  -> narrow WordPressCliService allowlist
  -> bounded CommandExecutor
  -> pinned short-lived WP-CLI runner
  -> selected site network and document-root mount
  -> selected site UID/GID
  -> redacted result and structured audit event
  -> JobManager for mutations
  -> API/UI response
```

The administrator should select a managed site only. ContainerCP should derive
the compose project, network, document root, PHP-compatible runtime, UID/GID,
and WP-CLI location.

The preferred runner model is an on-demand pinned runner container, not a
WP-CLI installation in every normal PHP image and not host-installed WP-CLI.

## Phased Implementation Plan

### Phase 0: Architecture Proposal and Contract

Scope:

- Add and approve an architecture proposal under `planning/proposals/`.
- Define supported read-only and mutating operations.
- Define runner image pinning, `--path`, UID/GID, network, mount, timeout,
  output, secret, audit, and unsupported-command policies.
- Document that existing credential rotation remains separate.

Expected files/components:

- New proposal under `planning/proposals/`.
- `docs/api/API_REFERENCE.md`.
- `docs/development/api-rules.md`.
- WordPress security documentation.

Tests:

- No implementation tests.

Completion criteria:

- Proposal approved.
- No arbitrary shell or raw argv API permitted by the design.
- Existing credential and migration behavior explicitly preserved.

Not part of this phase:

- No source implementation.
- No runner image.
- No live WordPress execution.

### Phase 1: Centralize Site Runtime Context and Correct Existing Bugs

Scope:

- Extend `WordPressConfigService` only as needed to expose a safe runtime
  context independent of credential mutability.
- Centralize site, compose project, document root, container root, and service
  resolution.
- Remove the hard-coded migration container probe.
- Correct Apache migration config validation.
- Validate migration domains before path construction.
- Add bounded execution for new WordPress operations.

Expected files/components:

- `libs/wordpress/WordPressConfigService.{h,cpp}`.
- `libs/wordpress/WordPressRuntimeVerifier.{h,cpp}`.
- `libs/migration/VestaSiteImporter.cpp`.
- `libs/runtime/CommandExecutor.{h,cpp}`.
- `libs/core/ServiceRegistry.{h,cpp}`.
- `CMakeLists.txt` and `tests/CMakeLists.txt`.

Tests:

- Site ID/domain mismatch.
- Site-root and document-root traversal.
- Apache/Nginx mapping.
- Migration invalid-domain rejection.
- Hard-coded-container regression.
- Correct Apache container path.
- Timeout, output, and cleanup behavior.
- Existing WordPress and migration regression suites.

Completion criteria:

- All new WordPress execution context comes from the selected managed site.
- No arbitrary unchecked domain controls a host path.
- Existing valid API behavior remains compatible.

Not part of this phase:

- No WP-CLI endpoint.
- No plugin/theme/core mutation.
- No GUI changes.

### Phase 2: Read-Only WP-CLI Backend

Scope:

- Add a narrow `WordPressCliService`.
- Use a pinned short-lived runner.
- Attach only to the selected site network.
- Mount only the selected document root.
- Use read-only mounts.
- Derive and pass `--path` internally.
- Use typed operations rather than raw command strings.
- Bound output and timeout.
- Redact results and return typed missing-runner/missing-WP-CLI errors.

Initial operations:

- `core is-installed`.
- `core version`.
- `plugin list`.
- `theme list`.
- `config path`.
- Redacted `config list`.
- `db check` only after client and redaction validation.

Expected files/components:

- New `libs/wordpress/WordPressCliService.{h,cpp}`.
- `libs/runtime/CommandExecutor.{h,cpp}`.
- `libs/core/ServiceRegistry.{h,cpp}`.
- `CMakeLists.txt` and `tests/CMakeLists.txt`.

Tests:

- Exact runner argv.
- Internal `--path` derivation.
- No shell command acceptance.
- Site network and mount isolation.
- Read-only mount enforcement.
- Missing binary behavior.
- UID/GID policy.
- Timeout/output truncation.
- Secret absence from results and logs.

Completion criteria:

- Read-only operations execute only against the selected site.
- Arbitrary commands are impossible through the service.
- Existing credential and migration paths are unchanged.

Not part of this phase:

- No filesystem mutations.
- No plugin/theme/core updates.
- No GUI exposure.

### Phase 3: API-First Read-Only UI

Scope:

- Add typed REST endpoints for approved read-only operations.
- Add site detail capability/status UI.
- Preserve the existing credential card and endpoints.

Expected files/components:

- `libs/api/ApiServer.cpp`.
- `docs/api/API_REFERENCE.md`.
- `docs/development/api-rules.md`.
- `web/pages/sites.js`.
- `web/pages/databases.js` only for shared capability display if needed.
- `web/core/jobs.js` if polling is required.
- `tests/test_api.cpp` and new API/UI tests.

Tests:

- Site ownership and operation allowlist.
- Unknown operation rejection.
- Cross-site rejection.
- No password, path, or raw stderr exposure.
- UI uses only approved API endpoints.

Completion criteria:

- Administrator can inspect a selected WordPress site without manually
  finding the container, PHP binary, document root, or WP-CLI path.
- UI remains an API client.

Not part of this phase:

- No mutation.
- No credential rotation changes.
- No arbitrary WP-CLI console.

### Phase 4: Job-Backed Mutations

Scope:

- Add explicitly approved plugin, theme, core, language, cache, and non-secret
  config operations.
- Execute mutations through jobs.
- Use selected-site UID/GID and write-enabled mounts only for approved actions.
- Add public-safe structured audit events.
- Never transport database passwords through WP-CLI.

Expected files/components:

- `libs/wordpress/WordPressCliService.{h,cpp}`.
- New WordPress CLI audit component following the existing credential audit
  pattern.
- `libs/api/ApiServer.cpp`.
- `libs/jobs/JobManager.*` and `libs/jobs/JobExecutor.*` if required.
- `web/pages/sites.js`.
- `web/core/jobs.js`.
- API/security documentation.
- Mutation and job tests.

Tests:

- Plugin install/update/delete.
- Theme install/update/delete.
- Core update.
- Language and cache operations.
- Package identifier validation.
- Cross-site and host-path rejection.
- UID/GID and file ownership.
- Mount mode.
- Timeout and cleanup.
- Audit redaction.
- Job failure and retry behavior.

Completion criteria:

- Every mutation is site-aware and job-backed.
- No mutation can target another site.
- New files are not root-owned unintentionally.
- Failure and audit states are explicit and redacted.

Not part of this phase:

- No arbitrary command execution.
- No DB reset/drop/repair operations.
- No credential rotation through WP-CLI.
- No multisite administration.

### Phase 5: Disposable-Site Integration Validation

Scope:

- Validate Apache and Nginx WordPress sites.
- Validate multiple PHP image versions.
- Validate multiple-site isolation.
- Validate read-only and mutating operations.
- Validate failure, timeout, missing-runner, ownership, cleanup, API, UI, and
  audit behavior.

Expected files/components:

- `planning/TEST_ENVIRONMENT.md`.
- New disposable integration fixtures.
- WordPress CLI documentation.
- `CHANGELOG.md`.

Tests:

- Real Docker/WordPress integration.
- Two-site isolation.
- Different PHP versions.
- WordPress path/config discovery.
- Plugin/theme/core filesystem changes.
- Root-ownership prevention.
- Runner cleanup.
- Job retry and failure handling.

Completion criteria:

- Disposable-site validation passes.
- Full regression suite passes.
- Zero compiler warnings.
- API, security, and operational documentation is complete.

Not part of this phase:

- No production-site execution.
- No automatic WordPress provisioning.
- No host-installed WP-CLI.

## Compatibility Considerations

Preserve the existing credential status endpoint, rotation endpoint, CLI
command, migration endpoints, site compose layout, owner metadata, templates,
and storage records.

Unsafe migration domains and unsupported runtime contexts should intentionally
be rejected rather than preserved as accepted behavior.

No storage migration is required for the initial WP-CLI capability if runner
configuration remains configuration/code based.

## Audit Status

- No WP-CLI modernization was implemented during WPCLI-001.
- No source files or tests were modified during the audit.
- No commit was created during the audit.
- Nothing was pushed during the audit.

## WPCLI-002 Architectural Corrections

The formal architecture proposal supersedes the modernization target and phase
details above where they conflict:

`planning/proposals/ARCH-012-WordPressCliExecution.md`

The proposal is `Approved`. Implementation proceeds phase by phase, with each
phase remaining a separate reviewable commit and validation gate.

### Corrected execution model

```text
Managed Site ID
  -> canonical WordPressRuntimeContext
  -> typed WordPressCliOperation
  -> validated runner plan
  -> bounded absolute-path CommandExecutor
  -> ephemeral runner using actual selected PHP image ID
  -> selected private network and document-root mount
  -> proven PHP-FPM non-root UID/GID
  -> redacted typed result and audit event
  -> REST API in a later phase
  -> GUI in a later phase
```

The actual PHP image is discovered from the selected site's running `php`
container. `Domain.php_version`, the configured PHP default, and
`PhpVersionManager::get_default()` are not authoritative for an existing
site's WP-CLI runtime.

The runner uses the selected container's immutable image ID and a separately
pinned, read-only WP-CLI Phar. It does not use a generic PHP image,
host-installed WP-CLI, or a permanent WP-CLI installation in every PHP image.

The canonical mutation identity is the selected PHP container's proven,
non-root PHP-FPM worker UID/GID. `Site.owner` and SFTP users/groups are not
treated as that identity. If the identity cannot be proven, filesystem-
mutating operations fail closed and no root fallback is allowed.

### Corrected initial read-only allowlist

Phase 2 is limited to:

- `core is-installed`;
- `core version`;
- `plugin list`;
- `theme list`.

Phase 2 excludes config inspection, database checks, credentials, arbitrary
options, arbitrary arguments, and arbitrary commands. Existing
`WordPressConfigService`, `WordPressRuntimeVerifier`, and database credential
rotation remain their current owners.

### Revised phases

#### Phase 0: Architecture Proposal and Contract

Review and approve `ARCH-012-WordPressCliExecution.md`. This phase is
documentation only and includes no runner, API, GUI, migration, or test
implementation.

#### Separate Migration Correctness Task

Fix only the existing `VestaSiteImporter` issues found by WPCLI-001:

- hard-coded `site-N-web` probing;
- Apache/Nginx config validation mismatch;
- centralized migration domain/path validation;
- migration diagnostic redaction.

This is a separate task, separate test scope, and separate logical commit. It
is not WP-CLI implementation.

#### Phase 1: Canonical Managed-Site Runtime Context

Extend the existing WordPress/runtime boundaries only as needed to resolve:

- selected managed site;
- canonical site and document roots;
- compose/runtime identity;
- actual running PHP container;
- actual immutable PHP image ID;
- selected private network;
- PHP service identity;
- proven PHP-FPM worker UID/GID;
- runtime capability and fail-closed status.

Do not use configured PHP defaults as runtime truth. Do not add an API, GUI,
mutation, or migration fix in this phase.

#### Phase 2: Read-Only WP-CLI Backend and Early Integration Gate

Add one narrow typed `WordPressCliService` using the selected actual PHP image,
the pinned Phar, selected private network, selected document root, non-root
identity, bounded execution, cleanup, redaction, and the four-operation
allowlist.

Real disposable WordPress integration is required before Phase 2 completion.
It must prove the four operations, actual image use, network/mount isolation,
cross-site rejection, bounded output, timeout cleanup, and no root-owned files.

No API, GUI, mutation, config list/get, DB check, or arbitrary console belongs
to this phase.

#### Phase 3: Typed Read-Only REST API and GUI

Expose only the four approved read-only operations through typed API endpoints
and a thin Site Details UI. No raw argv, raw command field, arbitrary console,
mutation, credential rotation change, or migration change is included.

#### Phase 4: Approved Job-Backed Mutations

Add only reviewed typed plugin/theme/core/language/cache operations. Require
the proven PHP-FPM UID/GID, operation-specific write mounts, job execution,
public-safe audit, timeout, cleanup, and ownership tests. Database credential
rotation remains separate and never passes passwords through WP-CLI.

#### Phase 5: Full Disposable Integration and Stabilization

Validate Apache, Nginx, at least two actual PHP image/runtime versions,
multiple sites, read-only operations, approved mutations, timeout/failure
cleanup, ownership, audit, API, and GUI behavior.

### Phase 2 integration gate

The required disposable integration starts before API/UI implementation. It
must prove:

1. The correct managed site is selected.
2. The actual running PHP image is used.
3. The correct private network is used.
4. Only the selected document root is mounted.
5. `core is-installed` succeeds.
6. `core version` succeeds.
7. `plugin list` succeeds.
8. `theme list` succeeds.
9. Output and timeout bounds hold.
10. Runner cleanup succeeds after success, failure, and timeout.
11. No root-owned files are created.
12. Cross-site execution is rejected.

### Ownership and identity correction

The former generic phrase `selected site UID/GID` is replaced by a concrete
policy: the effective non-root PHP-FPM worker UID/GID of the selected running
PHP container. It must be derived and proven. It must not be guessed, copied
from `Site.owner`, or borrowed from the SFTP subsystem. Failure to prove it
blocks WP-CLI execution and especially all mutations.

### CommandExecutor correction

The WP-CLI runner uses `CommandExecutor::run_safe()` only with a trusted
absolute Docker executable path. The architecture does not add shell PATH
lookup. A timeout must explicitly remove and verify removal of the runner
container because terminating the local Docker CLI child is not sufficient.

### Explicitly excluded from initial implementation

- raw `config list`;
- raw `config get`;
- DB credentials;
- database password operations;
- `db check`;
- arbitrary options;
- arbitrary WP-CLI arguments;
- arbitrary WP-CLI commands;
- arbitrary PHP execution;
- database reset/drop/repair/search-replace;
- arbitrary WP-CLI console;
- permanent WP-CLI installation in normal PHP images;
- host-installed WP-CLI.
