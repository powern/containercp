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

## Final architecture (ARCH-004)

Implemented: 2025-07-08

```
Internet
    │
    ▼
host:80/443
    │
    ▼
┌──────────────────────────────────────────┐
│ containercp-proxy (nginx:alpine)         │
│ network: containercp-public              │
│ ports: 80:80, 443:443                    │
│ reads: /srv/containercp/proxy/sites/     │
│ never removed on daemon shutdown         │
└──────────┬───────────────────────────────┘
           │ Docker DNS
     ┌─────┴─────┐
     ▼           ▼
┌──────────┐ ┌──────────┐
│ site-1-web│ │ site-2-web│
│:80       │ │:80       │
│ network: │ │ network: │
│  public  │ │  public  │
│  +site-1 │ │  +site-2 │
└────┬─────┘ └────┬─────┘
     │            │
     ▼            ▼
┌──────────┐ ┌──────────┐
│ site-1   │ │ site-2   │
│ private  │ │ private  │
│ network  │ │ network  │
│  - php   │ │  - php   │
│  - db    │ │  - db    │
│  - redis │ │  - redis │
└──────────┘ └──────────┘
```

## Key design decisions

1. **Shared public network** (`containercp-public`):
   - Created by `ensure_central_proxy()` on daemon startup
   - Proxy + all site web containers join this network
   - Docker DNS resolves `site-<id>-web:80` from proxy

2. **Per-site private networks** (`containercp-site-<id>`):
   - Created by Docker Compose inline
   - Backend services (php/db/redis) are ONLY on private network
   - Web container bridges public + private networks

3. **No host ports for site containers**:
   - Compose template has no `ports:` section
   - Only proxy publishes host ports (80, 443)

4. **Apache2 default backend**:
   - Default WEB_SERVER profile: `apache-php-default`
   - Nginx remains selectable via profile system

5. **PortManager deprecated**:
   - No host ports allocated per site
   - Kept for backward compat only

## Files changed (ARCH-004)

| File | Change |
|------|--------|
| libs/template/TemplateEngine.h/.cpp | Add SITE_ID to render() |
| libs/docker/ComposeGenerator.h/.cpp | New template: no ports, network routing, site-ID naming |
| libs/docker/EnvGenerator.h/.cpp | Remove NGINX_PORT generation (deprecate) |
| libs/provider/DockerComposeProvider.h/.cpp | Remove port parameter, pass site_id |
| libs/provider/HostingProvider.h | Revert create_site signature |
| libs/proxy/NginxProxyProvider.h/.cpp | Network routing, persistent proxy, public network |
| libs/proxy/ProxyProvider.h | Default virtual methods |
| libs/operations/SiteCreateOperation.h/.cpp | Remove PortManager, use site_id upstream |
| libs/operations/SiteRemoveOperation.h/.cpp | Remove PortManager, clean private network |
| libs/daemon/DaemonApp.cpp | Remove PortManager from constructors |
| libs/api/ApiServer.cpp | Remove PortManager from constructors |
| app/containercpd/main.cpp | Don't remove proxy on shutdown |
| libs/filesystem/SiteLayout.cpp | Add config/apache directory |
| libs/core/ServiceRegistry.cpp | Apache2 default backend |
| planning/proposals/ARCH-004-DockerNetworkMultiSite.md | New architecture proposal |
