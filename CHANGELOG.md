# Changelog

All notable changes to ContainerCP are documented here.

Format: date | commit | summary

---

## 2025-07-08 | `763ffb1` | Multi-site port conflict fix & documentation audit

### What changed

**Architecture: multi-site hosting support**

- `libs/runtime/PortManager.h/.cpp` — new: dynamic port allocation starting from 9000, scans existing sites on daemon restart to reclaim ports
- `libs/docker/EnvGenerator.h/.cpp` — added `nginx_port` parameter, `NGINX_PORT` in .env is now unique per site
- `libs/provider/DockerComposeProvider.h/.cpp` — passes allocated port to EnvGenerator
- `libs/provider/HostingProvider.h` — updated `create_site` signature to accept port
- `libs/operations/SiteCreateOperation.h/.cpp` — allocates port, calls proxy provider after success, releases on rollback
- `libs/operations/SiteRemoveOperation.h/.cpp` — releases port, removes proxy config
- `libs/proxy/NginxProxyProvider.h/.cpp` — `reload()` now executes `docker exec nginx -s reload`; manages central proxy container lifecycle (create on startup, remove on shutdown)
- `libs/proxy/ProxyProvider.h` — added `ensure_central_proxy()` / `remove_central_proxy()` virtual methods (default no-op)
- `libs/core/ServiceRegistry.h/.cpp` — exposes `PortManager`, scans existing sites on startup
- `libs/daemon/DaemonApp.cpp` — wires new dependencies to operations
- `libs/api/ApiServer.cpp` — wires new dependencies to operations
- `app/containercpd/main.cpp` — creates database directory on startup, ensures central proxy, removes proxy on shutdown
- `CMakeLists.txt` — adds PortManager.cpp

**Bug fix: database directory not created on startup**

- `app/containercpd/main.cpp` — `/srv/containercp/database/` now created before any service writes to it. Previously all persistence silently failed on fresh install.

**Bug fix: central proxy detection**

- `libs/proxy/NginxProxyProvider.cpp` — `central_proxy_running()` now uses `docker inspect` exit code instead of `docker ps --filter` (which always returned exit 0 even when container was missing).

**Documentation audit (13 files)**

- `AGENTS.md` — reflects RC1 completion, updates milestone to RC2/stability
- `planning/PRODUCT_VISION.md` — v1.0 target Debian 12 -> 13, checklist 114 -> 137
- `planning/product-roadmap.md` — acceptance criteria Debian 12 -> 13
- `planning/product-validation.md` — removed duplicate summary table, added Multi-Site Hosting section (146 items total)
- `planning/TEST_ENVIRONMENT.md` — checklist count 126 -> 146
- `planning/backlog.md` — Sprint 6 Access items marked completed
- `README.md` — validation count, Debian version, dual-port listener description
- `INSTALL.md` — fixed Debian codename errors, port descriptions, binary size
- `docs/WEB-UI.md` — fixed stale build path
- `planning/proposals/ARCH-001/002/003` — status Draft -> Implemented
- `planning/v0.5-completion-review.md` — updated to reflect RC1 actual results

**New documents**

- `planning/bugs/BUG-014-multi-site-port-conflict.md` — bug report
- `planning/multi-site-validation.md` — architecture report

### Why it changed

ContainerCP could not host more than one site (RC1 blocker). Every site's nginx container hardcoded host port 80. No central reverse proxy existed. The NginxProxyProvider was a no-op. Database persistence silently failed on fresh installs.

### User-visible behavior

- Multiple sites can coexist on one server
- Each site gets a unique port (starting at 9000)
- A central nginx proxy binds host port 80 and routes by Host header
- Sites survive daemon restart
- Site removal cleans up proxy config
- No port conflicts when creating the second, third, etc. site

### Validation

- 2 sites created (multi-one.local, multi-two.local)
- Both return HTTP 200 through proxy on port 80
- Both survive daemon restart (verified with `pkill` + restart)
- Removal of one site does not affect the other
- Proxy config and port released on removal
- All unit tests pass (0 failures)
- Zero compiler warnings (Debug + Release)

### Risks

- Port 9000+ range may conflict with other services on the host. If needed, the start port can be changed in `PortManager` constructor.
- The central proxy container (`containercp-proxy`) is removed on daemon shutdown. On `kill -9` it may remain running; on next startup it is reused.
- Port allocation is in-memory only; on restart, `scan_existing_sites()` reads `.env` files from `/srv/containercp/sites/*/` to reclaim ports.
- The proxy config for a newly created site may not be immediately active if nginx reload fails; site is still accessible directly on its unique port.
