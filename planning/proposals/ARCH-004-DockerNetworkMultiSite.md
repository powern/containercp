# ARCH-004: Docker Network Based Multi-Site Hosting

Status: Approved

## Problem

The RC1 multi-site fix (BUG-014) uses host port allocation (9000+)
and `--network host` for the central proxy. This is not production
quality — it consumes host ports, requires port scanning on restart,
and creates coupling between site identity and ephemeral port numbers.

## Motivation

The Product Vision requires ContainerCP to host multiple websites
on a single server. The architecture must be clean, secure, and
scalable. Host port allocation is a temporary workaround.

## Current Architecture (RC1 temporary)

```
Internet → host:80
  └── containercp-proxy (network host, mounts configs)
        └── 127.0.0.1:9000 → site-1-web (host port 9000)
        └── 127.0.0.1:9001 → site-2-web (host port 9001)

Each site's docker-compose.yml:
  - web container publishes 0.0.0.0:<unique-port>:80
  - all containers on private site-network bridge
  - no shared Docker network
```

Problems:
- Consumes host port per site
- PortManager must scan .env files on restart
- Central proxy uses `--network host` (no Docker DNS)
- Proxy routing uses IP:port, not container names
- Site containers named by domain ({{DOMAIN}}-nginx)

## Target Architecture

```
Internet → host:80/443
  └── containercp-proxy
        └── containercp-public (shared bridge)
              ├── site-1-web:80
              └── site-2-web:80
                        │
              ┌─────────┴─────────┐
              │ containercp-site-1 │  (private bridge)
              │  ├── site-1-php   │
              │  ├── site-1-db    │
              │  └── site-1-redis │
              └───────────────────┘
```

### Containers

| Container | Networks | Host ports |
|-----------|----------|------------|
| containercp-proxy | containercp-public | 80, 443 |
| site-<id>-web | containercp-public, containercp-site-<id> | none |
| site-<id>-php | containercp-site-<id> | none |
| site-<id>-db | containercp-site-<id> | none |
| site-<id>-redis | containercp-site-<id> | none |

### Networks

| Network | Type | Purpose |
|---------|------|---------|
| containercp-public | bridge (external) | proxy to site web containers |
| containercp-site-<id> | bridge (per-site) | internal site communication |

### Proxy lifecycle

- Created on daemon startup if missing
- Connects to `containercp-public` network
- Mounts `/srv/containercp/proxy/sites/` as config volume
- NEVER removed on normal daemon shutdown
- Only removed on explicit reset/uninstall
- Survives daemon restart
- Reloaded when site configs change

## New Resources

None.

## Managers

No new managers.

## Storage

No storage changes.

## Providers

### NginxProxyProvider changes

- `ensure_central_proxy()` — creates `containercp-public` network,
  creates proxy container attached to that network (not `--network host`)
- `create_proxy(ReverseProxy)` — proxy_pass uses Docker service name
  `http://site-<site_id>-web:80` instead of `http://127.0.0.1:<port>`
- `remove_central_proxy()` — preserved but NEVER called on normal
  shutdown; only for explicit cleanup
- Proxy container uses `--restart unless-stopped` so it survives
  daemon restart

### DockerComposeProvider changes

- `create_site(site)` — no port parameter
- Generates compose with:
  - No `ports:` section for web container
  - Container names: `site-<id>-web`, `site-<id>-php`, etc.
  - Web on `containercp-public` AND private network
  - Backend services only on private network
  - `containercp-public` declared as `external: true`
  - Private network declared inline

## REST API

No API changes.

## Web UI

No immediate UI changes. Site creation uses default Apache2 backend.
Future UI will expose backend web server selection.

## CLI

No CLI changes.

## Configuration

No new configuration values.

## Migration Strategy

Existing sites using host-port allocation will continue working
until manually removed and recreated. No migration script needed.

## Backward Compatibility

- `EnivGenerator` still accepts `nginx_port` parameter (ignored in
  new compose) for backward compat with any external callers
- `PortManager` kept but deprecated — not used for new sites
- Old templates on disk at `/etc/containercp/templates/` may be
  outdated; fresh deploy gets new template

## Rejected Alternatives

1. **Keep host port allocation** — rejected because it does not
   scale, requires port scanning, and couples site identity to
   ephemeral ports.

2. **Single shared network for all containers** — rejected because
   it removes isolation between sites (db/redis would be reachable
   across sites).

3. **Docker Compose network per site with central proxy on host
   network** — this is the RC1 temporary fix; rejected for production.

4. **Traefik/Caddy as central proxy** — rejected for now because
   nginx is simpler and already the intended proxy; can be swapped
   later via provider interface.

## Risks

- `containercp-public` network must be created before any site
  containers. If creation fails, site creation must handle rollback.
- Private networks may accumulate if site removal fails mid-way.
  Implement cleanup in SiteRemoveOperation.
- Docker DNS resolution across compose projects works for containers
  on the same network. Since sites are deployed as separate compose
  projects, the proxy container (started manually) and site web
  containers (started via compose) both attach to `containercp-public`.
  Docker DNS should resolve `site-<id>-web` from the proxy container
  as long as both are on the same network.

## Validation Plan

1. Clean deploy on Debian 13 VM
2. Daemon starts → proxy created on `containercp-public`
3. Create site-1 (multi-one.local) → compose has no host ports
4. Verify `docker network inspect containercp-public` shows proxy and site-1-web
5. `curl -H "Host: multi-one.local" http://127.0.0.1/` → HTTP 200
6. Create site-2 (multi-two.local) → compose has no host ports
7. Verify `docker network inspect containercp-public` shows proxy, site-1-web, site-2-web
8. Verify `docker network ls` shows `containercp-site-<id>` private networks
9. `curl -H "Host: multi-two.local" http://127.0.0.1/` → HTTP 200
10. Kill daemon → proxy stays running, sites remain reachable
11. Restart daemon → proxy detected as running, sites remain reachable
12. Remove site-1 → private network removed, proxy config removed
13. site-2 still reachable
14. No host ports published by site containers (`docker ps` shows no `0.0.0.0:PORT->PORT`)
15. Apache2 is default backend (verify generated nginx/apache config)
16. All unit tests pass
17. Zero compiler warnings

## Required files changed

| File | Change |
|------|--------|
| libs/template/TemplateEngine.h/.cpp | Add SITE_ID to render() |
| libs/docker/ComposeGenerator.h/.cpp | New template: no ports, network routing, site-ID naming |
| libs/docker/EnvGenerator.h/.cpp | Remove NGINX_PORT generation (deprecate) |
| libs/provider/DockerComposeProvider.h/.cpp | Remove port parameter, pass site_id |
| libs/provider/HostingProvider.h | Revert create_site signature |
| libs/proxy/NginxProxyProvider.h/.cpp | Network routing, persistent proxy, public network |
| libs/proxy/ProxyProvider.h | Default virtual methods for ensure/remove |
| libs/operations/SiteCreateOperation.h/.cpp | Remove PortManager, use site_id upstream |
| libs/operations/SiteRemoveOperation.h/.cpp | Remove PortManager, clean private network |
| libs/daemon/DaemonApp.cpp | Remove PortManager from constructors |
| libs/api/ApiServer.cpp | Remove PortManager from constructors |
| app/containercpd/main.cpp | Don't remove proxy on shutdown |
| libs/filesystem/SiteLayout.cpp | Add config/apache directory |
| libs/template/web_templates.h | Apache2 default (change is_default check) |
| libs/core/ServiceRegistry.cpp | Change default profile to apache |
| planning/multi-site-validation.md | Update architecture description |
| planning/bugs/BUG-014.md | Update with new architecture |
| CHANGELOG.md | Add entry for this change |
