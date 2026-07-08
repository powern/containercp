# Multi-Site Architecture Validation

Date: 2025-07-08
Environment: Debian 13 (Trixie) — Validation VM

## Summary

ContainerCP **cannot host more than one site**. This is an RC1 blocker.

## Test results

### Test 1: Create multi-one.local
- **Command:** `containercp site create admin multi-one.local`
- **Result:** ✅ Success
- **Docker:** nginx binds `0.0.0.0:80->80/tcp`, php/mariadb/redis all healthy
- **NGINX_PORT:** 80 (from generated .env)

### Test 2: Create multi-two.local
- **Command:** `containercp site create admin multi-two.local`
- **Result:** ❌ FAIL — `Command failed with exit code 1. Created resources have been rolled back.`
- **Daemon log (actual error):**
  ```
  Error response from daemon: driver failed programming external connectivity on
  endpoint multi-two.local-nginx: Bind for 0.0.0.0:80 failed: port is already allocated
  ```
- **Residual containers:** php/mariadb/redis created but nginx failed; rollback cleaned up all records

### Confirmed: Port conflict prevents multiple sites

## Root cause analysis

### 1. Hardcoded NGINX_PORT=80

`libs/docker/EnvGenerator.cpp:31` generates `.env` with:
```
NGINX_PORT=80
```

Every site gets port 80. The docker-compose template in
`libs/docker/ComposeGenerator.cpp:15` binds:
```yaml
ports:
  - "${NGINX_PORT}:80"
```

Host port 80 can only be bound by one container. The second site's nginx
fails to start.

### 2. No central reverse proxy container

The architecture has a `NginxProxyProvider` class that writes per-site
nginx config files to `/srv/containercp/proxy/sites/<domain>.conf`, but:

- **It is never called.** `SiteCreateOperation.cpp:93` only creates an
  in-memory `ReverseProxyManager` record with `proxies_.create(domain,
  site.id, "", "")` — empty config_path and upstream.
- **No container manages these configs.** The `reload()` method is an
  explicit no-op:
  ```cpp
  core::OperationResult NginxProxyProvider::reload() {
      logger_.info("NginxProxyProvider: Reload requested (no-op)");
      return {true, ""};
  }
  ```
- **No central nginx container exists.** There is no Docker container
  that binds host port 80 and reads proxy configs.

### 3. Generated docker-compose.yml (multi-one.local)

```yaml
services:
  nginx:
    image: nginx:alpine
    ports:
      - "${NGINX_PORT}:80"     # binds host port → port conflict
    networks:
      - site-network           # isolated per-site network
  php:
    image: php:8.4-fpm
    networks:
      - site-network
  mariadb:
    image: mariadb:lts
    networks:
      - site-network
  redis:
    image: redis:alpine
    networks:
      - site-network

networks:
  site-network:                # NOT shared across sites
```

Each site gets its own `site-network` bridge. There is no shared
network for a central proxy to route through.

### 4. NginxProxyProvider config output

Not tested: no proxy config files exist on disk because
`NginxProxyProvider::create_proxy()` is never wired into the site
creation flow.

## Broken architecture diagram

```
Internet
    │
    ▼
┌─────────────────────────────────────┐
│              Host port 80            │
│  ┌────────────────────┐             │
│  │ multi-one.local-nginx │ ← binds 80 │  ✅ Works (first)
│  └────────────────────┘             │
│  ┌────────────────────┐             │
│  │ multi-two.local-nginx │ ← fails 80 │  ❌ Conflict!
│  └────────────────────┘             │
└─────────────────────────────────────┘
```

## Required architecture

```
Internet
    │
    ▼
┌─ port 80 ───────────────────────────┐
│  Central Proxy (nginx container)     │  ← Only binds host port 80/443
│  │                                   │
│  ├─ Host header: multi-one.local     │
│  │  → proxy_pass http://127.0.0.1:8081 │
│  │                                   │
│  ├─ Host header: multi-two.local     │
│  │  → proxy_pass http://127.0.0.1:8082 │
│  │                                   │
│  └─ Host header: ...                 │
└──────────────────────────────────────┘
    │                          │
    ▼                          ▼
┌──────────────┐    ┌──────────────┐
│ Site A nginx  │    │ Site B nginx  │
│ port 8081     │    │ port 8082     │
│ (internal)    │    │ (internal)    │
└──────────────┘    └──────────────┘
```

## Required changes

### 1. Dynamic port allocation
- **New:** `libs/runtime/PortManager` — allocates unique host ports for
  per-site nginx containers
- **Modified:** `libs/docker/EnvGenerator` — accepts port parameter
  instead of hardcoding 80
- **Modified:** `libs/provider/DockerComposeProvider` — passes allocated
  port to EnvGenerator
- **Modified:** `libs/operations/SiteCreateOperation` — stores port and
  passes it to proxy creation

### 2. Central reverse proxy container
- **New:** Manage a central nginx container (`containercp-proxy`) that:
  - Binds host port 80 (and 443 for SSL)
  - Uses `network_mode: host` (simplest: can reach all host-bound ports)
  - Reads configs from `/srv/containercp/proxy/sites/*.conf`
  - Is created on daemon startup if missing
  - Is removed on daemon shutdown
- **Modified:** `NginxProxyProvider::reload()` — call `docker exec
  containercp-proxy nginx -s reload`
- **Modified:** `NginxProxyProvider::create_proxy()` — write config
  pointing to `127.0.0.1:<site_port>` with correct `server_name`

### 3. Wire proxy creation into site lifecycle
- **Modified:** `SiteCreateOperation` — after successful site creation,
  call `NginxProxyProvider::create_proxy()` with the allocated port as
  upstream, and trigger proxy reload
- **Modified:** `SiteRemoveOperation` — call
  `NginxProxyProvider::remove_proxy()` and trigger proxy reload
- **Modified:** `ServiceRegistry` or daemon main — start central proxy
  on boot

### 4. Remove host port from per-site nginx
- Once central proxy is operational, per-site nginx containers should
  NOT bind host ports. With host-network proxy, they do need to publish
  ports for the proxy to reach them. Alternative: use a shared Docker
  network so the proxy can reach containers by name without host ports.

## Files requiring changes

| File | Change |
|------|--------|
| `libs/docker/EnvGenerator.h` | Add port parameter to `generate()` |
| `libs/docker/EnvGenerator.cpp` | Use port param instead of `NGINX_PORT=80` |
| `libs/provider/DockerComposeProvider.h` | Accept port allocation dependency |
| `libs/provider/DockerComposeProvider.cpp` | Pass port to EnvGenerator |
| `libs/operations/SiteCreateOperation.h` | Accept ProxyProvider not just Manager |
| `libs/operations/SiteCreateOperation.cpp` | Call proxy creation with port |
| `libs/proxy/NginxProxyProvider.h` | Add port to create_proxy signature |
| `libs/proxy/NginxProxyProvider.cpp` | Implement proper reload, accept port |
| `libs/runtime/PortManager.h` | New file |
| `libs/runtime/PortManager.cpp` | New file |
| `libs/runtime/CMakeLists.txt` | Add PortManager |
| `libs/daemon/main.cpp` | Start central proxy on boot |
| `libs/core/ServiceRegistry.h/cpp` | Expose ProxyProvider, manage central proxy |

## Validation plan

1. Build and start daemon
2. Verify central proxy container is created on startup
3. `curl http://127.0.0.1/` → returns 404 or central proxy status (no sites yet)
4. Create multi-one.local → site starts, proxy config written, proxy reloaded
5. `curl -H "Host: multi-one.local" http://127.0.0.1/` → returns site content
6. Create multi-two.local → site starts, own port, proxy config written
7. `curl -H "Host: multi-two.local" http://127.0.0.1/` → returns site content
8. Remove multi-one.local → proxy config removed, remaining site still works
9. Daemon restart → central proxy recreated, all sites still work
10. Both sites survive daemon restart

## Severity

**RC1 Blocker.** ContainerCP cannot host more than one site. This
breaks the core hosting workflow for any production use case.

## Existing validation gap

The RC1 validation checklist (`product-validation.md`) does not include
a multi-site test. Items 53-72 test single site creation but never test
creating a second site. This must be added to the validation checklist.
