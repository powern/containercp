# WP-CLI Integration v0.8 Review

## Status

Research and recommendation note only. This document does not approve WP-CLI deployment, image changes, production installation, or use against real sites.

## Summary

WP-CLI is a mature and appropriate future tool for WordPress operations, but it should not be the first implementation mechanism for password rotation in ContainerCP v0.8. The safer path is to implement a narrow `WordPressConfigService` for detection and direct-constant updates first, then add WP-CLI later as an optional validation/operations runner for tasks where loading WordPress is useful.

Recommended v0.8 position:

- Do not install WP-CLI inside every PHP image by default.
- Do not depend on WP-CLI for the first credential-rotation implementation.
- Prefer a controlled on-demand WP-CLI runner container later, after runner isolation and secret handling are reviewed.
- Use WP-CLI initially for read-only validation experiments only on disposable sites.

## Official Project Facts

| Area | Finding |
|------|---------|
| Purpose | WP-CLI is the command-line interface for WordPress and supports admin, database, multisite, plugin, theme, and core operations. |
| Maintenance | The project has shipped continuously since 2011 and is maintained by the WordPress community. |
| Current stable noted upstream | The GitHub project page identifies current stable release `2.12.0` as of the reviewed page. |
| License | `wp-cli/wp-cli` is MIT licensed. |
| PHP requirement | Official install guide requires PHP `7.2.24` or later. |
| WordPress requirement | GitHub README states WordPress `4.9` or later, with degraded functionality possible for older versions. |
| Recommended install | Official guide recommends downloading the Phar, verifying it, marking executable, and placing it on `PATH`. |
| Alternative installs | Composer, Homebrew, Docker image `wordpress:cli`, `.deb`, Fedora/CentOS packages, and custom PHP binary support are documented. |
| Config commands | `wp config` can create, get, list, set, delete, path-check, and shuffle salts for `wp-config.php`. |
| DB commands | `wp db` performs database operations using credentials from `wp-config.php`. |

Sources reviewed:

- `https://wp-cli.org/`
- `https://make.wordpress.org/cli/handbook/guides/installing/`
- `https://developer.wordpress.org/cli/commands/config/`
- `https://developer.wordpress.org/cli/commands/config/get/`
- `https://developer.wordpress.org/cli/commands/config/set/`
- `https://developer.wordpress.org/cli/commands/config/list/`
- `https://developer.wordpress.org/cli/commands/config/create/`
- `https://developer.wordpress.org/cli/commands/db/`
- `https://developer.wordpress.org/cli/commands/db/check/`
- `https://github.com/wp-cli/wp-cli`

## Relevant WP-CLI Behavior

`wp config get`, `wp config list`, and `wp config set` run on the `before_wp_load` hook, before WordPress fully loads. They accept `--config-file=<path>` and global `--path=<path>` parameters.

`wp db check` runs after `wp-config.php` has been loaded and uses `DB_HOST`, `DB_NAME`, `DB_USER`, and `DB_PASSWORD` from the config unless overridden. It runs `mysqlcheck` and therefore depends on available client utilities and runtime connectivity.

`wp config create` supports `--prompt=dbpass` to avoid disclosing passwords through shell history in manual workflows, but ContainerCP still needs non-interactive secret handling and must avoid argv/log exposure.

## Integration Options

### Option A: Install WP-CLI Inside Each PHP Image

Description: bake `wp` into every ContainerCP PHP image.

Strengths:

- Runs in the same PHP runtime as the site.
- Uses the same mounted document root.
- Simple `docker exec site-<id>-php wp ...` operational model.

Risks:

- Increases every PHP image footprint and maintenance surface.
- Ties WP-CLI update cadence to PHP image rebuilds.
- Makes WP-CLI present for every site, including non-WordPress sites.
- Gives compromised app containers an extra administrative tool.
- Does not solve secret handling by itself.

Decision: not recommended as the default v0.8 approach.

### Option B: Controlled On-Demand WP-CLI Runner Container

Description: run a short-lived container such as `wordpress:cli` attached to the target site network and mounted to the target site files.

Strengths:

- Keeps WP-CLI out of normal PHP images.
- Short-lived and easier to version/pin independently.
- Can be scoped to one site network and one mounted document root.
- Cleaner fit for job-based operations.

Risks:

- Requires careful UID/GID, volume, network, and path mapping.
- Must prevent runner from reaching unrelated sites.
- Must ensure credentials are not exposed through arguments, environment, logs, or temporary files.
- Requires image pinning and update policy.

Decision: preferred future WP-CLI model after the narrow config service exists.

### Option C: Host-Installed WP-CLI Against Mounted Site Files

Description: install `wp` on the host and execute it against `/srv/containercp/sites/<domain>/public`.

Strengths:

- Easy to install through Phar or package manager.
- No per-site image changes.

Risks:

- Host PHP version and extensions may differ from the site PHP container.
- Host process reads site code and config directly.
- Harder to reproduce per-site runtime behavior.
- Broader blast radius if path resolution is wrong.

Decision: not recommended for managed operations.

### Option D: No WP-CLI For Credential Rotation; Narrow Dedicated Config Editor

Description: implement `WordPressConfigService` for direct config detection/update and use MariaDB provider for connection verification.

Strengths:

- Smallest security surface for initial credential work.
- Does not require installing external tooling.
- Avoids loading arbitrary WordPress/plugin code for simple config edits.
- Gives ContainerCP explicit source classification and mutation policy.

Risks:

- Must correctly handle PHP config syntax for supported patterns.
- Does not cover complex dynamic configs in the first version.
- Eventually duplicates some capability that WP-CLI already has.

Decision: recommended first implementation path for WP-1 through WP-5.

## Recommendation

Use Option D first. Build `WordPressConfigService` as a narrow, test-heavy config inspection and update service. Defer WP-CLI execution until after credential rotation has a safe provider boundary and runner threat model.

When WP-CLI is introduced, use Option B as the default model:

- Pinned runner image.
- On-demand short-lived container.
- Attached only to the target site network.
- Mounted only to the target site document root and required config paths.
- No host port exposure.
- No credentials in argv or logs.
- Job-scoped cleanup on success and failure.

## Safe Initial WP-CLI Uses

Potential later read-only uses:

- `wp config path` to confirm config discovery.
- `wp config list --format=json` on disposable validation sites.
- `wp db check` only after client utility availability and output redaction are verified.
- `wp core is-installed` for future WordPress provisioning validation.

Not approved for first credential implementation:

- `wp config set DB_PASSWORD <password>` with the password in argv.
- `wp db repair`, `wp db optimize`, `wp db reset`, `wp db drop`, or other mutating DB commands.
- Running WP-CLI against production sites during planning.

## Required Controls Before WP-CLI Deployment

- Architecture proposal or approved implementation task.
- Runner image/version pinning.
- Runner network scoping to one site.
- Read-only mount mode for read-only commands.
- Write mode only for explicitly approved mutations.
- UID/GID handling compatible with site file ownership.
- Path containment checks for `--path` and `--config-file`.
- Secret redaction for stdout, stderr, job messages, and logs.
- No password arguments in command vectors.
- Cleanup of containers, temporary files, and networks on success/failure.
- Tests proving no production site state changes during read-only commands.

## Open Questions

- Should ContainerCP pin `wordpress:cli` or build its own minimal WP-CLI runner image?
- Should runner operations execute as the same UID/GID as the PHP container document root?
- Which WP-CLI commands should be allowed by an explicit allowlist in v0.8?
- Should WP-CLI support be optional per node or installed as part of the standard runtime?
