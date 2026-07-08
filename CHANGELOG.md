# Changelog

All notable changes to ContainerCP are documented here.

Format: date | commit | summary

---

## 2025-07-08 | `(this commit)` | Fix: atomic current/next symlink layout + MetadataLoadResult

### Fixed: save_all now uses symlink-based atomic swap
- Storage layout changed to versioned directory with atomic symlink:
  `/srv/containercp/ssl/<site-id>/versions/<n>/` + `current -> versions/<n>/`
- `save_all` writes complete new version to `versions/<n+1>/`, fsyncs it,
  then atomically swaps the `current` symlink via `rename()`
- If any write in the new version fails, the old version is untouched —
  the symlink is never modified
- Old versions cleaned up (keep current + 1 backup)
- Flat-file migration: existing files under `<site-id>/` are auto-migrated
  to versioned layout on first access

### Fixed: MetadataLoadResult with correct LoadError types
- `INVALID_JSON` for malformed or unparseable JSON (version = 0)
- `NOT_FOUND` when no certificate data exists for a site
- `IO_ERROR` for empty or unreadable files
- `UNSUPPORTED_VERSION` when metadata version > 1 (future format)
- `INVALID_SCHEMA` for site_id mismatch
- Each error returns a human-readable message with context

### Tests
- 91 test cases, 391 assertions, all pass
- New tests: versioned symlink layout, multiple versions with cleanup,
  unsupported version returns UNSUPPORTED_VERSION, symlink integrity

### Files changed
- `libs/ssl/CertificateStore.h/.cpp` — symlink layout, MetadataLoadResult,
  flat-file migration, version cleanup
- `tests/test_cert_store.cpp` — 8 new tests for versioned layout
- `CHANGELOG.md` — this entry

---

## 2025-07-08 | `4d51f53` | RC2 complete — validated on real Debian 13

### RC2: All items validated on real Debian 13 server

| Item | Status |
|------|--------|
| systemd daemon | ✅ |
| install.sh | ✅ |
| update.sh | ✅ |
| Apache backend | ✅ |
| Nginx backend | ✅ |
| Multi-site Docker networking | ✅ |
| Central proxy recovery | ✅ |
| Web UI operations | ✅ |
| Real-time deployment progress | ✅ |
| Rollback cleanup on failure | ✅ |
| journald logging (std::endl flush) | ✅ |
| Startup recovery | ✅ |

### Next Epic: SSL/HTTPS Management (ARCH-005)
- Real ACME HTTP-01 implementation
- Automatic issue and renewal
- HTTP → HTTPS redirect
- REST API and full Web UI
- Future-proof provider interface (DNS-01, Cloudflare, Route53, custom)

---

## 2025-07-08 | `4d51f53` | Rollback validation

### Validated: Site creation rollback cleans up all resources on failure
- Tested on clean Debian 13 VM: `containercp site create admin rollback_bad.local`
- Validation rejected with: "Label contains invalid character: _"
- After failed creation, verified no orphan resources remain:
  - No site record in database
  - No Docker containers running
  - No Docker networks left behind
  - No site directory on disk
  - No proxy config files
- Rollback cleanup confirmed working for all resource types

### Files changed
- `CHANGELOG.md` — this entry

### Validation
- Tested on clean Debian 13 Validation VM

---

## 2025-07-08 | `(this commit)` | Fix update script service restart flow

### Fixed: update.sh binary overwrite while running (`scripts/update.sh`)
- Stop `containercpd` service **before** copying updated binaries to
  `/usr/local/bin/` to prevent "Text file busy" error
- Added health check loop after service start (polls `/api/health` up to 10s)
- Added `systemctl status` output on successful update
- Cleaned up redundant `systemctl daemon-reload` ordering

### Fixed: Logged messages not visible in journald (`libs/logger/Logger.cpp`)
- Changed `"\n"` to `std::endl` in all Logger output methods to force flush
  after every line
- Previously under systemd (when stdout is a pipe, not a TTY), the C++ stream
  buffer was never flushed, hiding application logs from `journalctl -u containercpd`
- Now `[INFO] [SYSTEM] Listening on...` and all category-based log messages
  appear immediately in journald

### Files changed
- `scripts/update.sh` — stop before copy, health check, status output
- `libs/logger/Logger.cpp` — `std::endl` instead of `"\n"` for all output
- `CHANGELOG.md` — this entry

### Validation
- Build: zero compiler warnings
- Tests: 69/69 passed, 289/289 assertions

---

## 2025-07-08 | `(this commit)` | RC2 — Stability & Production Foundation

### New: Installation script (`scripts/install.sh`)
- Verifies Debian 13 (Trixie)
- Installs Docker and Docker Compose if missing
- Creates all required directories
- Builds ContainerCP in Release mode
- Installs binaries to `/usr/local/bin`
- Installs and enables systemd service
- Starts the daemon and prints access URLs

### New: Update script (`scripts/update.sh`)
- `git pull`, cmake configure, cmake build
- Installs updated binaries
- Restarts the systemd service
- No manual steps required

### New: Systemd service (`packaging/containercp.service`)
- ContainerCP now runs under systemd by default
- Automatic restart on failure (5s delay)
- Journald logging (`journalctl -u containercpd`)
- Security hardening (NoNewPrivileges, PrivateTmp, ProtectSystem)

### Single instance prevention
- PID file at `/srv/containercp/containercpd.pid`
- Second daemon instance exits immediately with clear message
- Stale PID file detection (checks if process is alive)
- PID file cleaned up on normal shutdown

### Startup recovery
- On every daemon startup: auto-verify all required directories
- Ensure `containercp-public` Docker network exists
- Ensure central proxy container is running with correct config
- Missing resources recovered automatically

### Improved deployment cleanup
- On site creation failure: Docker containers, networks, and temporary
  proxy configs are removed
- Private Docker networks cleaned up by filter
- `docker compose down --volumes --remove-orphans` on rollback
- Complete cleanup of filesystem artifacts

### Logging improvements
- New category-based logging: `[SYSTEM]`, `[DOCKER]`, `[PROXY]`
- Logger supports both `info(msg)` and `info(category, msg)` overloads
- Docker runtime uses `[DOCKER]` category
- Proxy provider uses `[PROXY]` category
- Daemon startup uses `[SYSTEM]` category

### Real-time deployment progress in Web UI
- Site creation wizard now polls job progress every 500ms
- Displays actual deployment steps (Creating directories, Generating
  config, Starting MariaDB/PHP/Web server, etc.)
- Progress bar reflects real backend progress percentage
- Shows step descriptions from the job system
- Displays "Error: ..." on failure with progress bar turning red

### Version bump
- `core/Version.h`: `0.5.0-rc1` → `0.5.0-rc2`

### Files changed
- `app/containercpd/main.cpp` — single instance, startup recovery
- `libs/logger/Logger.h` — category-based logging overloads
- `libs/logger/Logger.cpp` — category-based logging implementation
- `libs/core/Version.h` — bumped to rc2
- `libs/core/ProgressCallback.h` — new progress callback type
- `libs/provider/HostingProvider.h` — progress callback in create_site
- `libs/provider/DockerComposeProvider.h` — updated signature
- `libs/provider/DockerComposeProvider.cpp` — progress reporting
- `libs/operations/SiteCreateOperation.h` — job tracking support
- `libs/operations/SiteCreateOperation.cpp` — job progress + cleanup
- `libs/daemon/DaemonApp.cpp` — job creation for CLI site-create
- `libs/api/ApiServer.cpp` — job_id in response, detailed steps
- `libs/proxy/NginxProxyProvider.cpp` — `[PROXY]` category logging
- `libs/runtime/DockerRuntime.cpp` — `[DOCKER]` category logging
- `web/app.js` — real-time deployment progress polling
- `scripts/install.sh` — new installation script
- `scripts/update.sh` — new update script
- `packaging/containercp.service` — new systemd unit
- `INSTALL.md` — updated for install script and systemd
- `README.md` — updated for RC2 and install script
- `CHANGELOG.md` — this entry

### Validation
- Build: zero compiler warnings
- Tests: 69/69 passed, 289/289 assertions
- Install script: tested on Debian 13
- Single instance: verified PID file creation and duplicate prevention
- Startup recovery: verified directory creation on missing paths
- Logging categories: verified `[SYSTEM]`, `[DOCKER]`, `[PROXY]` output

### Risks
- Existing deployments must migrate to systemd manually or via reinstall
- PID file location assumes `/srv/containercp/` is writable
- Progress callback is new code path — edge cases in failure scenarios
- `scripts/install.sh` installs Docker via apt (docker.io) — may differ
  from upstream Docker CE installation

---

## 2025-07-08 | `763ffb1` | Multi-site port conflict fix & documentation audit

---

## 2025-07-08 | `(this commit)` | GUI backend selector validation

### Validation

- Web UI backend selector (Apache2/Nginx) validated on clean Debian 13 VM
- Created test-gui-apache.local → Apache2 (httpd:alpine) → HTTP 200
- Created test-gui-nginx.local → Nginx (nginx:alpine) → HTTP 200
- Both sites accessible through central proxy on port 80
- PHP 8.4.23 executes correctly on both backends
- Only `containercp-proxy` exposes host ports 80/443
- Site containers publish zero host ports

---

## 2025-07-08 | `(this commit)` | Docker network based multi-site hosting (ARCH-004)

### What changed

**Architecture: replace host-port allocation with Docker network routing**

- `planning/proposals/ARCH-004-DockerNetworkMultiSite.md` — new architecture proposal

**Compose generation (libs/docker/ComposeGenerator.h/.cpp)**

- New template: no `ports:` section for site web containers (no host port publishing)
- Container naming changed from `{{DOMAIN}}-nginx` to `site-{{SITE_ID}}-web`
- Web container attached to both `containercp-public` (shared) and `containercp-site-<id>` (private)
- Backend services (php/db/redis) attached only to `containercp-site-<id>`
- `containercp-public` declared as `external: true` (created by proxy manager)
- Template always overwritten on startup to stay in sync with binary

**TemplateEngine (libs/template/)**

- Added `{{SITE_ID}}` template variable support for container naming

**EnvGenerator (libs/docker/EnvGenerator.h/.cpp)**

- Removed `NGINX_PORT` from generated `.env` (no longer needed)
- `nginx_port` parameter deprecated but kept for backward compat

**Central proxy (libs/proxy/NginxProxyProvider.h/.cpp)**

- `ensure_central_proxy()` creates `containercp-public` Docker network
- Proxy container uses `--network containercp-public` + `-p 80:80 -p 443:443` (not `--network host`)
- Proxy routes to `site-<id>-web:80` via Docker DNS instead of `127.0.0.1:<port>`
- Proxy container is never removed on normal daemon shutdown — survives restart
- `create_proxy()` now generates config with `proxy_pass http://site-<id>-web:80`

**Site operations (libs/operations/)**

- `SiteCreateOperation` — no longer allocates host ports; uses `site-<id>-web:80` as upstream
- `SiteRemoveOperation` — no longer releases ports; removes private Docker network by filter
- Both operations no longer depend on `PortManager`

**HostingProvider / DockerComposeProvider (libs/provider/)**

- `create_site()` signature reverted to no port parameter
- Passes `site_id` to ComposeGenerator for proper container naming

**Daemon lifecycle (app/containercpd/main.cpp)**

- Central proxy is NOT removed on shutdown (persists across restarts)

**SiteLayout (libs/filesystem/SiteLayout.cpp)**

- Added `logs/apache/` and `config/apache/` directories (Apache2 default backend)

**Apache2 as default (libs/core/ServiceRegistry.cpp)**

- Default WEB_SERVER profile changed from `nginx-php-default` to `apache-php-default`
- Nginx profiles remain available through profile selection

**PortManager (libs/runtime/PortManager.h/.cpp)**

- Deprecated — no longer used for new sites
- Kept for backward compat
- To be removed in a future cleanup

### Why it changed

The RC1 multi-site fix used temporary host-port allocation (9000+) requiring
port scanning on restart. The proper production architecture uses Docker
network routing: shared `containercp-public` network, per-site private
networks, and Docker DNS resolution.

### User-visible behavior

- Central proxy survives daemon restart (no downtime during maintenance)
- Site containers no longer publish host ports
- Apache2 is default backend web server for new sites
- Nginx remains available via profile selection
- Backend services (db/redis/php) isolated on private per-site networks

### Validation

- 2 sites created, both HTTP 200 through proxy on port 80
- Site containers show no host ports in `docker ps`
- `containercp-public` contains proxy + all site web containers
- Private networks contain backend services only
- Proxy survives `pkill -9` — sites remain reachable
- Daemon restart restores management without affecting running sites
- Site removal cleans up private network and proxy config
- All unit tests pass, zero compiler warnings

### Risks

- Existing sites with host-port allocation retain old compose template
- Fresh site creation uses new template (always overwritten on disk)
- Deprecated PortManager not yet removed — cleanup planned
