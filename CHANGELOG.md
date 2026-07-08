# Changelog

All notable changes to ContainerCP are documented here.

Format: date | commit | summary

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
