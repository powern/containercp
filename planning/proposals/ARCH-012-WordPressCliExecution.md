# ARCH-011: WordPress CLI Execution Architecture

**Status:** Review

**Proposal owner:** ContainerCP project

**Related task:** WPCLI-002

**Audit baseline:** `planning/wp-cli-001-audit-and-modernization-plan.md`

**Audit HEAD:** `f24d8467241a6038f944b71172e96e90008519a3`

**Implementation gate:** No implementation phase may start until this proposal
is explicitly reviewed and moved to `Approved`.

## Problem

ContainerCP currently has no WP-CLI implementation. The existing WordPress
subsystem owns configuration inspection, credential rotation, and a fixed PHP
database verifier. The migration subsystem can execute PHP CLI diagnostics, but
the local variable named `wp_cli` is not WP-CLI and no `wp` binary or Phar is
present.

The WPCLI-001 audit also established that the current site model does not yet
provide a single canonical object containing all values required for safe
WordPress command execution:

- selected managed site
- actual running PHP image
- private site network
- mounted WordPress document root
- PHP-FPM worker identity
- bounded runner lifecycle

An administrator must not provide any of those values manually. A naive
implementation would also inherit the root daemon's Docker access and could
create root-owned files or execute against the wrong site.

## Motivation

ContainerCP is container-native and API-first. A safe WordPress capability must
therefore execute inside the selected site's runtime boundary while keeping
WP-CLI out of normal PHP images unless a concrete requirement proves that
necessary.

This proposal supports the Product Vision goals of:

- per-site PHP/runtime isolation;
- predictable container execution;
- provider/runtime separation;
- API-first administration;
- safe multi-site hosting;
- backward compatibility with existing WordPress credential management.

## Goals

- Derive all execution inputs from canonical managed-site state.
- Use the actual running PHP image of the selected site's `php` service.
- Pin WP-CLI independently from the site PHP image.
- Execute WP-CLI in an ephemeral, short-lived runner.
- Keep the runner on the selected site's private network only.
- Mount only the selected site's WordPress document root.
- Use a proven non-root PHP-FPM filesystem identity.
- Fail closed when runtime, identity, image, path, artifact, or network
  requirements cannot be proven.
- Start with four low-risk read-only operations.
- Keep API, GUI, credential rotation, and migration responsibilities separate.
- Validate the runner against a real disposable WordPress site during the
  read-only backend phase.

## Non-Goals

- No WP-CLI implementation in this proposal task.
- No REST endpoint implementation.
- No GUI control implementation.
- No arbitrary WP-CLI console.
- No shell or raw argv API.
- No database password operation through WP-CLI.
- No replacement of `WordPressConfigService`.
- No replacement of `WordPressRuntimeVerifier`.
- No replacement of the existing credential rotation saga.
- No migration correctness fix in the WP-CLI implementation task.
- No automatic WordPress provisioning.
- No multisite administration.
- No host-installed WP-CLI.
- No permanent WP-CLI installation in every PHP image.

## Current Architecture

### Existing WordPress configuration boundary

`WordPressConfigService` resolves a managed site and searches safe document-root
candidates. `WordPressConfigDetector` owns path safety, symlink rejection,
credential source classification, and public-safe inspection.

Relevant files:

- `libs/wordpress/WordPressConfigService.{h,cpp}`
- `libs/wordpress/WordPressConfigDetector.{h,cpp}`
- `libs/wordpress/WordPressPhpDefineScanner.{h,cpp}`
- `libs/wordpress/WordPressConfigTypes.{h,cpp}`

The current service result includes site root, document root, config path, and
container document root when a candidate config is found. Its `ok` status also
reflects credential support, which is not sufficient as the only future
runtime-context gate. The implementation phase must add or expose a safe
runtime-context result without duplicating path logic.

### Existing runtime verifier

`WordPressRuntimeVerifier` executes a fixed PHP script through:

```text
docker compose --project-directory <site-root> exec -T php php -r <script>
```

It validates absolute paths, site containment, service names, container roots,
and config containment. It owns the existing WordPress/PHP database-access
verification path and remains separate from WP-CLI.

### Existing site runtime

`DockerComposeProvider` generates a site compose project under the site root.
The generated services are `web`, `php`, `mariadb`, and `redis`. The PHP image
is currently selected from `PhpVersionManager::get_default()`, which is a known
site provisioning correctness issue and is not sufficient for existing site
runtime discovery.

`SiteRuntimeManager` and `RuntimeActionExecutor` already own site service names,
compose project status, and site runtime actions. They remain reusable, but
WP-CLI must obtain the actual image and network from the running selected PHP
container rather than trusting a configured default.

### Existing process execution

`runtime::CommandExecutor` provides argv-based fork/exec execution. `run()` uses
`execvp()` and is unbounded. `run_safe()` provides a fixed environment, timeout,
and output limits but uses `execve()`, so executable paths must be absolute.

No WP-CLI process is currently executed.

## Proposed Architecture

```text
Managed Site ID
  -> canonical WordPressRuntimeContext
  -> typed WordPressCliOperation
  -> validated runner plan
  -> bounded absolute-path CommandExecutor
  -> ephemeral runner using actual PHP image ID
  -> selected private network and document-root mount
  -> proven PHP-FPM non-root UID/GID
  -> WP-CLI result
  -> redaction and typed result
  -> structured audit event
  -> REST API in a later phase
  -> GUI in a later phase
```

The administrator supplies only the managed `site_id` and, in later mutation
phases, typed operation parameters such as a validated plugin identifier. The
administrator does not supply runtime paths, container names, image names,
network names, PHP executables, UID/GID values, or WP-CLI paths.

### Canonical context ownership

| Context value | Canonical owner | Resolution rule |
|---|---|---|
| Site ID | `SiteManager` | Selected managed site record only; reject missing/system site |
| Domain | `SiteManager` / `Site.domain` | Use persisted canonical domain; never request text |
| Site root | `WordPressConfigService` plus `Config::sites_dir()` | Resolve and normalize `<sites_root>/<domain>`; reject traversal and symlinks |
| WordPress document root | `WordPressConfigService` and `WordPressConfigDetector` | Reuse safe candidate/path logic; do not reconstruct in API/UI |
| Host config path | `WordPressConfigService` | Internal-only safe config path; never public API data |
| Container document root | `WordPressConfigService` | Map the selected site's actual web server/runtime layout; validate against mounts |
| Compose project | canonical site root plus runtime validation | Require the selected compose file and selected-site labels to match |
| PHP service | generated runtime service role | Require the canonical `php` service; reject request-provided service names |
| Actual PHP container | runtime inspection | Require exactly one running selected-site `php` container |
| Actual PHP image | runtime inspection | Read `.Config.Image` and immutable `.Image` from that container |
| Private network | actual PHP container network inventory | Select the one trusted selected-site private network; reject public/unrelated networks |
| PHP-FPM identity | selected PHP container read-only probe | Resolve configured effective FPM worker user/group to numeric UID/GID |
| Runtime capability | future `WordPressRuntimeContext` | Report independently whether read-only and mutation gates are satisfied |

The future `WordPressRuntimeContext` is a value object owned by the existing
WordPress subsystem. It is not a persisted `core::Resource` and does not create
a second site registry.

## Canonical WordPress Runtime Context

The implementation phase should extend `WordPressConfigService` with a
runtime-context resolution operation rather than duplicating the existing path
logic in a new handler.

Conceptually, the result contains:

```text
site_id
domain
site_root
document_root
container_document_root
config_path (internal-only, optional for operations that need it)
compose_project_identity
compose_file
php_service = "php"
php_container_name (internal-only)
actual_php_image_reference (internal-only)
actual_php_image_id (internal-only)
private_network_name (internal-only)
php_fpm_uid
php_fpm_gid
runtime_status
read_only_capability
mutation_capability
failure_code
```

The context is resolved synchronously immediately before an operation. It is
not accepted from API or GUI payloads and is not persisted as a long-lived
claim because containers and images can change.

### Site and document-root validation

The context resolver must:

1. Resolve the managed `Site` by numeric ID.
2. Reject `site_id=0`, system sites, disabled sites, and missing sites.
3. Resolve the site root through `WordPressConfigService` path logic.
4. Require a regular compose file within that site root.
5. Require a safe regular WordPress document root within that site root.
6. Validate the host-to-container mount mapping against the selected PHP
   container rather than trusting only the web-server string.
7. Reject symlinks and paths escaping the managed site root.

Credential mutability must not be required for runtime context. A site with a
dynamic or unsupported credential source may still be inspected by a future
read-only operation, but it must not cause `WordPressConfigService` parsing to
be duplicated or bypassed.

## Actual PHP Runtime/Image Selection

### Discovery

The selected site's actual runtime is discovered from the running `php`
container, not from `Domain.php_version`, configured defaults, or
`PhpVersionManager::get_default()`.

The trusted sequence is:

1. Resolve the compose project from the canonical site root.
2. Use the existing runtime command boundary to list the selected compose
   project's `php` service container.
3. Require exactly one running container for that service.
4. Inspect that container using the trusted Docker executable.
5. Verify ContainerCP site labels and compose working directory identify the
   selected site.
6. Read `.Config.Image` for the configured reference.
7. Read `.Image` for the immutable image ID/digest actually used by the running
   container.
8. Verify the image ID still exists locally and is runnable.
9. Store both the reference and immutable ID in the transient context.

The runner uses the immutable image ID from the running container, not a tag
that could have moved since the site started. The image reference remains in
the context for diagnostics and audit but is not supplied by the client.

### Image identity validation

The context resolver must fail closed if:

- the PHP container is not running;
- more than one candidate PHP container is found;
- the container lacks the expected selected-site labels;
- the compose working directory does not match the selected site root;
- `.Config.Image` is empty;
- `.Image` is empty or cannot be inspected;
- the immutable image cannot be used to create the runner;
- the actual PHP runtime is unsupported;
- the container mount does not map the selected document root.

The configured `PhpVersion` record may be retained as descriptive metadata, but
it cannot authorize or select the WP-CLI runtime when it disagrees with the
running container.

### Unsupported runtime

If the actual image cannot run the pinned Phar with the required PHP version,
extensions, or libraries, the operation returns a typed unsupported-runtime
result and performs no WP-CLI execution.

ContainerCP must not silently fall back to:

- the global default PHP image;
- a generic `wordpress:cli` PHP image;
- host PHP;
- another site's PHP container;
- a permanently installed WP-CLI binary in an unrelated container.

## WP-CLI Artifact and Version Policy

### Artifact model

WP-CLI is an independently pinned Phar mounted read-only into the ephemeral
runner. It is not installed permanently into every site's PHP image.

The future managed artifact layout is:

```text
/srv/containercp/wp-cli/wp-cli.phar
/srv/containercp/wp-cli/version
/srv/containercp/wp-cli/sha256
```

These files are operator/package-managed, root-owned, and not writable through
the REST API or WordPress site mounts. The exact version and SHA-256 value must
be selected in the implementation proposal revision and must never be a
floating download URL.

### Source and integrity

The artifact comes from an official WP-CLI release asset obtained during a
reviewed package/update workflow. It is not downloaded during a site operation.

Before execution, ContainerCP verifies:

1. The Phar is a regular non-symlink file.
2. The file is inside the managed ContainerCP WP-CLI artifact directory.
3. The file owner and mode meet the root-owned read-only policy.
4. The computed SHA-256 matches the root-owned manifest value.
5. The recorded version is present and matches the reviewed artifact metadata.
6. The artifact can execute using the selected actual PHP image.

Integrity verification must use a direct bounded implementation or a validated
absolute executable path. It must not use shell interpolation.

Any mismatch returns `wp_cli_artifact_invalid` and prevents runner creation.

### Upgrade policy

WP-CLI upgrades are controlled independently from site PHP image upgrades.
An upgrade requires:

- a reviewed version change;
- a new reviewed SHA-256 value;
- disposable integration validation;
- update of the managed artifact and manifest atomically;
- retention of the previous artifact until the new artifact is validated;
- rollback to the previous verified artifact if validation fails.

No site operation may fetch or self-update WP-CLI.

## Filesystem Execution Identity

### Decision

The canonical WP-CLI mutation identity is the effective non-root PHP-FPM
worker UID/GID of the selected site's actual running PHP container.

This is not:

- `Site.owner`;
- a guessed numeric UID/GID;
- the host daemon UID/GID;
- an arbitrary SFTP user;
- an SFTP group ID;
- the host owner of an unrelated site.

The SFTP subsystem's managed groups remain owned by the SFTP/access subsystem.
They are not automatically reused as the WordPress process identity.

### Derivation

The selected PHP container is inspected read-only to determine the effective
PHP-FPM pool user and group. The future implementation must use a fixed argv
probe compatible with the supported PHP image, such as the PHP-FPM configuration
test plus non-shell account resolution inside the selected container.

The result must provide numeric UID/GID and prove:

1. The user and group exist in the selected PHP image.
2. UID is not zero.
3. GID is valid and belongs to the selected runtime identity.
4. The identity is the PHP-FPM worker identity, not merely the Docker exec
   default user.
5. The selected document root is visible to that identity.
6. The identity belongs to the selected site's runtime only.

The host document root owner is inspected as an additional ownership signal,
not as an authority to invent a new identity. ContainerCP must not chown the
site to make the operation pass.

### Fail-closed policy

If the FPM worker UID/GID cannot be proven, is root, is inconsistent, or cannot
access the selected document root, ContainerCP returns
`wordpress_filesystem_identity_unproven` and refuses all filesystem-mutating
WP-CLI operations.

For the initial read-only phase, the same non-root identity is used so that
WordPress bootstrap is not tested under a broader privilege than future
mutations. If it cannot be proven, WP-CLI execution is refused rather than
falling back to root.

No automatic ownership repair is part of WP-CLI execution.

### Regression requirements

Tests must cover:

- worker UID/GID extraction;
- root worker rejection;
- missing user/group rejection;
- mismatch between Docker exec user and FPM worker;
- unrelated SFTP identity rejection;
- selected-site identity binding;
- document-root access failure;
- absence of `--user 0` fallback;
- no root-owned files after disposable mutation validation.

## Ephemeral Runner

The runner uses the selected site's immutable PHP image ID and a separately
mounted pinned WP-CLI Phar.

Conceptually, the runner has:

```text
image: actual selected php container immutable image ID
entrypoint: php from that image
argv: /opt/containercp/wp-cli/wp-cli.phar <typed operation args>
user: proven PHP-FPM numeric UID:GID
network: selected site's private network only
mount: selected WordPress document root only
mount mode: read-only for Phase 2
root filesystem: read-only
capabilities: drop all
security: no-new-privileges
ports: none
docker socket: none
host root: none
ContainerCP secrets: none
HOME: isolated temporary directory
temporary directory: isolated tmpfs
CPU: bounded
memory: bounded
PIDs: bounded
name: deterministic internal site/operation name
cleanup: explicit success/failure/timeout cleanup
```

The runner must not mount the compose file, `/srv/containercp` broadly,
ContainerCP databases, `.env`, Docker socket, host `/`, or another site root.
Only the selected WordPress document root is available. Any WP-CLI cache or
temporary data uses isolated tmpfs storage.

### Read-only mounts

All Phase 2 operations use a read-only document-root mount unless a disposable
integration test proves that an operation needs a write for correctness. A
write requirement would require an operation-specific architecture revision,
not an implicit global switch.

### Naming and cleanup

The runner name is generated internally from the trusted site ID and an
internal operation identifier, for example:

```text
containercp-wpcli-site-<site-id>-op-<operation-id>
```

The operation identifier is not client-controlled. A pre-existing runner with
the expected name causes fail-closed handling and cleanup inspection; it is not
silently reused.

Every execution path must attempt cleanup. A cleanup failure is an operation
failure and an audit event, not a successful command result.

## Network Isolation

The runner network is selected from the actual PHP container network inventory.

The resolver must:

1. Inspect the selected PHP container networks.
2. Reject `containercp-public`.
3. Reject Docker bridge and unrelated networks.
4. Require the private network label/name and compose identity to match the
   selected managed site.
5. Require the selected PHP container to be attached to that network.
6. Require the selected database service to be reachable through that same
   private network when the operation needs WordPress bootstrap.

The network name is never accepted from a request. A deterministic name may be
used only as an additional trusted-site-ID check after actual membership and
labels have been verified.

The runner is never attached to:

- `containercp-public`;
- another site's private network;
- host bridge networks;
- mail networks;
- networks not required by the selected operation.

## Typed Operations and Bootstrap Policy

### Initial Phase 2 allowlist

Only these typed operations are in the initial read-only backend:

- `WordPressCliOperation::CoreIsInstalled`
- `WordPressCliOperation::CoreVersion`
- `WordPressCliOperation::PluginList`
- `WordPressCliOperation::ThemeList`

The service accepts an enum/typed request, not a command string or arbitrary
argv. It does not expose raw WP-CLI arguments.

### Excluded from Phase 2

- `config list`;
- `config get`;
- `config set`;
- `config create`;
- `db check`;
- DB credentials;
- database password operations;
- arbitrary option access;
- arbitrary WP-CLI commands;
- arbitrary PHP execution;
- `eval` and `eval-file`;
- plugin/theme/core mutations;
- cache mutations;
- database reset/drop/repair/search-replace.

### Bootstrap flags

All initial operations use non-interactive, deterministic output policy:

- `--no-color` is required.
- Interactive prompts are disabled by operation construction; no prompt input
  is provided.
- Plugin/theme skipping is operation-specific, not globally assumed.

Initial candidate policy to be proven in disposable integration tests:

| Operation | Candidate bootstrap policy | Acceptance requirement |
|---|---|---|
| Core is installed | `--skip-plugins --skip-themes --no-color` | Must correctly identify installation without loading site extensions |
| Core version | `--skip-plugins --skip-themes --no-color` | Must return core version accurately |
| Plugin list | `--skip-plugins --skip-themes --no-color` | Must list installed plugins and statuses accurately without executing plugin code |
| Theme list | `--skip-plugins --skip-themes --no-color` | Must list installed themes and statuses accurately without executing theme code |

If a skip flag makes an operation incorrect, the operation must not silently
remove the flag. It either receives a documented operation-specific exception
after security review or remains unsupported. Integration tests must prove the
chosen policy against broken and malicious disposable plugins/themes.

## Command Construction

The future command is always an argv vector built from typed operation code.

The service must never build:

```text
sh -c "..."
bash -c "..."
```

No API request may contain a raw shell command, raw argv vector, container name,
host path, network name, PHP executable, UID, GID, or WP-CLI path.

Later plugin/theme/package identifiers receive operation-specific validation.
They are still argv values, never shell fragments.

## CommandExecutor Decision

Phase 1 must use the existing bounded `CommandExecutor::run_safe()` contract,
but must correct the executable resolution assumption before WP-CLI execution.

The selected approach is trusted absolute executable discovery:

1. At trusted daemon/runtime setup, inspect a fixed candidate list for Docker,
   such as `/usr/bin/docker` and `/usr/local/bin/docker`.
2. Require a regular executable file and canonical path.
3. Store the selected absolute path in the transient runtime context.
4. Pass only that absolute path to `run_safe()`.
5. Reject non-absolute executable paths in the WP-CLI runner plan.

No shell PATH lookup is introduced. The existing `run()` behavior remains
unchanged for unrelated callers in this architecture phase.

### Timeout semantics

`run_safe()` terminating its local Docker CLI child does not guarantee that a
`docker run` container has stopped. The WP-CLI service must therefore treat
timeout as a two-step failure:

1. Bounded process cancellation.
2. Explicit `docker rm -f <internal-runner-name>` cleanup using the trusted
   absolute Docker executable and a separate bounded cleanup timeout.

The service verifies that the runner is absent before returning. If removal or
verification fails, the result is `runner_cleanup_failed` and the operation is
not successful.

The same cleanup policy applies to:

- WP-CLI non-zero exit;
- Docker CLI failure;
- runner startup failure;
- timeout;
- partial runner creation;
- stale runner name.

Daemon interruption requires best-effort cleanup and startup reconciliation of
ContainerCP-labelled WP-CLI runners. It must not delete containers without the
matching ContainerCP runner label and selected-site identity.

## Result, Redaction, and Audit

The future service returns a typed internal result containing:

- operation type;
- site ID;
- domain;
- success/failure code;
- exit code;
- bounded redacted stdout;
- bounded redacted stderr;
- timeout flag;
- cleanup status;
- runtime image ID/reference metadata;
- duration.

Host paths, container names, network names, image IDs, raw config, passwords,
and arbitrary site output are internal diagnostics and must not be public API
fields by default.

Later audit events follow existing WordPress credential audit conventions and
include site ID, domain, operation, job/operation ID, result, timeout, cleanup
status, and bounded diagnostic code. They must exclude passwords, command
secrets, raw argv, raw config, and unredacted site output.

## New Resources

None.

`WordPressRuntimeContext` is transient execution state, not a persisted
`core::Resource`. WP-CLI inventory is not persisted in the initial architecture.

## Managers

None.

The existing `SiteManager`, `JobManager`, and `JobExecutor` remain owners of
their current responsibilities. A new manager would duplicate existing state
ownership.

## Storage

No storage change and no migration.

The WP-CLI Phar and integrity metadata are operator/package-managed files under
`/srv/containercp/wp-cli/`, not ContainerCP resources. Site runtime context is
resolved dynamically and is not persisted.

## Providers

No new Provider interface is introduced in Phase 0.

### New service in a later implementation phase

Add one narrow `WordPressCliService` under `libs/wordpress/` to own:

- typed operation allowlisting;
- runner-plan construction;
- artifact validation;
- actual runtime context validation;
- runner lifecycle;
- result redaction;
- capability/failure codes.

This service is necessary because no current class owns typed WP-CLI policy.

### Provider boundary decision

Do not add a generic shell provider, generic command console, or generic
container-exec abstraction. The service uses the existing `CommandExecutor`
for bounded argv execution.

If implementation tests require a seam, use a narrow injected runner/executor
boundary owned by `WordPressCliService`; do not broaden `Runtime` or
`HostingProvider` responsibilities.

## REST API

No endpoints are added in WPCLI-002 or Phase 0.

Later Phase 3 may add typed read-only endpoints owned by `ApiServer`. They must
accept only a managed `site_id` and typed operation selection. They must not
accept runtime paths, container/network/image names, raw WP-CLI commands, or
raw argv.

The existing endpoints remain unchanged:

- `GET /api/wordpress/database-credentials/status`
- `POST /api/wordpress/database-credentials/rotate`

No new externally reachable unauthenticated endpoint may be introduced while
`AllowAllAuth` remains the current API middleware.

## Web UI

No UI changes are added in WPCLI-002 or Phase 0.

Later Phase 3 may add read-only capability/status display to the existing Site
Details page. The current WordPress credential card remains the UI for
credential inspection and rotation and is not replaced by WP-CLI.

## CLI

No CLI changes are added in WPCLI-002 or Phase 0.

Later CLI support must remain a thin client of the approved API/daemon
boundary. It must not execute WP-CLI locally or accept a raw WP-CLI command.

## Configuration

No runtime configuration is added in Phase 0.

The later artifact policy uses these package-managed paths:

- `/srv/containercp/wp-cli/wp-cli.phar`
- `/srv/containercp/wp-cli/version`
- `/srv/containercp/wp-cli/sha256`

The later implementation may expose read-only diagnostic configuration to
operators, but artifact path, SHA, image, network, UID, GID, and command values
must not be request-overridable.

## Existing Ownership Boundaries

### `WordPressConfigService`

Remains responsible for safe managed-site WordPress context discovery,
document-root/config path safety, and public-safe credential inspection. It must
not be bypassed by API/UI code or duplicated by WP-CLI handlers.

### `WordPressConfigDetector`

Remains responsible for config path safety and credential source classification.
WP-CLI must not become a second credential parser.

### `WordPressRuntimeVerifier`

Remains responsible for the current fixed PHP/MariaDB database-access
verification flow. WP-CLI is not introduced merely to replace this verifier.

### Database credential rotation

`DatabaseCredentialRotationService`, its adapter, MariaDB provider, and
`WordPressConfigUpdater` remain the exclusive owners of database password
rotation. WP-CLI receives no database passwords and does not update
`DB_PASSWORD`.

### `VestaSiteImporter`

Migration archive inspection/import and WP-CLI execution are separate concerns.
The hard-coded container probe, migration path validation, and Apache path
issues found by WPCLI-001 become a separate migration-correctness task and are
not changed by the WP-CLI runtime foundation.

## Migration Strategy

No migration script is required.

Existing sites do not receive WP-CLI automatically. A later implementation
phase must validate the actual running PHP image of each site dynamically.
Existing storage records are not rewritten to claim a PHP image that may not
match the running container.

The WP-CLI artifact is installed by a reviewed package/update workflow, not by
site migration and not by a site container mutation.

## Backward Compatibility

The proposal preserves:

- existing WordPress credential endpoints;
- existing credential rotation CLI command;
- existing WordPress credential UI;
- existing migration endpoints and CLI commands;
- existing site compose layout;
- existing Site.owner metadata;
- existing PHP/domain/storage records;
- existing WordPress configuration and runtime verifier behavior.

Unsafe or unprovable new runtime contexts fail closed. This is new capability
behavior and does not change valid existing operations.

## Rejected Alternatives

### Host-installed WP-CLI

Rejected because host PHP/extensions may differ from the selected site, host
execution has a broader filesystem blast radius, and it violates the
container-native runtime model.

### Generic `wordpress:cli` runner image

Rejected because its PHP version, extensions, and runtime libraries may not
match the selected site's actual PHP environment. WP-CLI is independently
pinned, but PHP runtime must come from the selected site image.

### Permanent WP-CLI installation in every PHP image

Rejected because it expands every image, affects non-WordPress sites, couples
WP-CLI updates to PHP image updates, and exposes an administrative tool inside
normal application containers.

### Site PHP image rebuilt with WP-CLI permanently installed

Rejected for the same footprint and update-coupling reasons. The preferred
model mounts a reviewed Phar read-only into an ephemeral runner.

### Administrator-supplied container/path/network/PHP values

Rejected because it defeats managed-site isolation and creates cross-site and
host-path confusion risks.

### Raw WP-CLI console

Rejected for the initial capability because arbitrary commands and PHP
bootstrap behavior cannot be safely represented as a narrow administrator
operation. It requires a separate security review.

### Reusing SFTP identity as WordPress identity

Rejected because SFTP users/groups have a different owner subsystem and are not
proven to be the PHP-FPM worker identity.

### Running mutations as root and repairing ownership afterward

Rejected because it can execute site code with unnecessary privilege, leaves
ownership races, and can damage existing ownership policy.

## Separate Migration Correctness Task

The following WPCLI-001 findings are explicitly outside this proposal's WP-CLI
implementation:

- remove `site-N-web` hard-coded probing;
- correct Apache/Nginx config validation path selection;
- centralize and validate migration domain/path input;
- redact migration PHP diagnostic output where required.

They require a separate task, separate tests, and separate logical commit.

## Revised Implementation Phases

### Phase 0: Architecture proposal and contract

This proposal. Documentation only. No runtime, API, GUI, migration, or test
implementation.

Completion criteria:

- Proposal reviewed and moved to `Approved`.
- Actual PHP image, artifact, identity, isolation, and cleanup decisions are
  not deferred to implementation.

### Separate Migration Correctness Task

Only existing `VestaSiteImporter` correctness/security fixes. No WP-CLI runner,
API, GUI, or mutation implementation.

### Phase 1: Canonical managed-site runtime context

Implement transient context resolution using existing WordPress and runtime
ownership. Prove actual running PHP container, image ID, document-root mount,
private network, PHP service, and PHP-FPM UID/GID. No WP-CLI API, GUI, or
mutation.

Tests must cover unit path/identity/label validation and fail-closed behavior.

### Phase 2: Read-only WP-CLI backend

Implement typed `WordPressCliService`, pinned Phar validation, actual selected
PHP image runner, selected network/mount, non-root identity, bounded execution,
cleanup, redaction, and the four-operation allowlist.

This phase must include disposable real WordPress integration proving:

- `core is-installed`;
- `core version`;
- `plugin list`;
- `theme list`;
- actual image identity;
- network and mount isolation;
- non-root execution;
- bounded output and timeout;
- cleanup after success, failure, and timeout;
- no root-owned files;
- cross-site rejection.

No API, GUI, or mutation.

### Phase 3: Typed read-only REST API and GUI

Expose only approved typed read-only operations through authenticated admin
boundaries and a thin Site Details UI. No raw console and no mutation.

### Phase 4: Approved job-backed mutations

Add only reviewed typed plugin/theme/core/language/cache operations. Require
proven canonical PHP-FPM UID/GID, write-enabled mount only per operation,
job execution, audit, and safe failure handling. Database credential rotation
remains separate.

### Phase 5: Full disposable integration and stabilization

Validate Apache, Nginx, multiple actual PHP images, multiple sites, read-only
operations, mutations, timeouts, failures, cleanup, ownership, audit, API, and
GUI. Production execution is not part of repository-level approval.

## Risks

### Blockers

- Existing site creation does not reliably bind per-site PHP metadata to the
  generated image; multi-PHP validation may require a separate hosting/runtime
  correction.
- The supported PHP image set must expose a reliable non-root FPM worker
  identity probe.
- Some WordPress operations may require bootstrap behavior that conflicts with
  plugin/theme skipping; disposable tests must prove operation-specific flags.
- The WP-CLI artifact has no current managed installation path; package/update
  work is a prerequisite for Phase 2.
- Docker CLI absolute-path discovery must work on supported installations.
- A timeout can terminate Docker CLI while leaving a runner container alive;
  explicit container cleanup and verification are mandatory.
- `AllowAllAuth` remains existing security debt; no new external WP-CLI route
  may bypass the authenticated admin boundary.
- Existing site-controlled PHP code is untrusted from the runner perspective;
  container isolation is a boundary, not a PHP sandbox.

## Validation Plan

### Phase 0 documentation validation

- Review complete proposal sections against `planning/proposals/README.md`.
- Confirm no new Resource, Manager, Storage, REST API, GUI, or CLI is proposed
  for Phase 0 implementation.
- Confirm migration fixes are separated.
- Run `git diff --check`.
- Inspect complete diff and ensure only intended documentation/changelog files
  change.

### Later implementation validation

- Unit tests for canonical context and fail-closed gates.
- Fake executor tests for exact argv and no shell use.
- Artifact integrity and version tests.
- UID/GID and ownership tests.
- Runner network/mount isolation tests.
- Timeout and cleanup tests.
- Disposable real WordPress integration in Phase 2.
- Full multi-PHP, Apache, Nginx, multi-site, mutation, API, GUI, audit, and
  cleanup matrix in Phase 5.

## Approval Checklist

- [ ] All proposal sections reviewed.
- [ ] Actual running PHP image discovery accepted.
- [ ] Immutable image ID runner model accepted.
- [ ] WP-CLI Phar version and integrity policy accepted.
- [ ] PHP-FPM UID/GID derivation and fail-closed rule accepted.
- [ ] Network and mount isolation accepted.
- [ ] `run_safe()` absolute executable decision accepted.
- [ ] Timeout and runner cleanup accepted.
- [ ] Phase 2 disposable integration gate accepted.
- [ ] Migration correctness work separated.
- [ ] Proposal status changed from `Review` to `Approved` before Phase 1.
