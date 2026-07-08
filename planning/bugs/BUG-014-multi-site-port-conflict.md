# BUG-014: Multi-site port conflict prevents hosting more than one site

## Severity
Critical — RC1 blocker

## Description

Every site's nginx container binds to host port 80 (via hardcoded
NGINX_PORT=80 in generated .env). Only one container can bind to host
port 80. Creating a second site fails because port 80 is already
allocated.

## Architecture cause

1. **Hardcoded NGINX_PORT=80 in EnvGenerator** — all sites compete
   for host port 80.

2. **No central reverse proxy container** — NginxProxyProvider only
   writes config files to disk but never calls `reload()` (it was a
   no-op). No central nginx container exists to route traffic by
   Host header.

3. **Proxy creation never wired** — SiteCreateOperation called
   `proxies_.create()` with empty upstream/config_path but never
   called `NginxProxyProvider::create_proxy()`.

4. **Database directory not created on startup** — `/srv/containercp/
   database/` was never created, so all persistence silently failed.
   This prevented sites from surviving daemon restart.

## Files changed

- `libs/runtime/PortManager.h/.cpp` — new: dynamic port allocation
  starting from 9000, scans existing sites on startup to reclaim ports
- `libs/docker/EnvGenerator.h/.cpp` — added nginx_port parameter
- `libs/provider/DockerComposeProvider.h/.cpp` — pass nginx_port to
  EnvGenerator
- `libs/provider/HostingProvider.h` — updated create_site signature
- `libs/operations/SiteCreateOperation.h/.cpp` — allocate port, call
  proxy provider after successful creation, release port on rollback
- `libs/operations/SiteRemoveOperation.h/.cpp` — release port, remove
  proxy config
- `libs/proxy/NginxProxyProvider.h/.cpp` — implement reload (docker
  exec nginx -s reload), manage central proxy container lifecycle
- `libs/proxy/ProxyProvider.h` — added ensure/remove_central_proxy
  virtual methods
- `libs/core/ServiceRegistry.h/.cpp` — expose PortManager, scan
  existing sites on startup
- `libs/daemon/DaemonApp.cpp` — wire new dependencies to operations
- `libs/api/ApiServer.cpp` — wire new dependencies to operations
- `app/containercpd/main.cpp` — create central proxy on startup,
  database directory on startup, remove proxy on shutdown
- `CMakeLists.txt` — add PortManager.cpp

## RC1 fix (commit 763ffb1, superseded)

Initial fix used PortManager with host port allocation (9000+). Superseded
by ARCH-004 Docker network based routing.

## Final fix (ARCH-004, current)

Replaced host-port allocation with Docker network routing:
- Shared `containercp-public` bridge network
- Per-site private `containercp-site-<id>` networks
- Proxy routes via Docker DNS to `site-<id>-web:80`
- No host ports consumed per site
- PortManager deprecated

## Validation results (final architecture)

- Two sites created (multi-one.local, multi-two.local)
- Both return HTTP 200 through proxy on port 80
- Site containers show no host ports in `docker ps`
- `containercp-public` contains proxy + all site web containers
- Private networks contain backend services only
- Proxy survives daemon kill — sites remain reachable
- Daemon restart restores management without affecting running sites
- Site removal cleans up private network

## Status
Resolved
