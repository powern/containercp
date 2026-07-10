# Reverse Proxy Management — Design Proposal

## 1. Current limitations

The current Proxy page shows only:

| Column | Source | Problem |
|--------|--------|---------|
| Domain | `ReverseProxy.domain` | Plain text, no link or context |
| Provider | `ReverseProxy.provider` | Always "nginx" — not useful |
| Status | `ReverseProxy.status` | Static text from DB, not actual nginx state |
| Actions | — | Only Delete |

Missing entirely:

- Global proxy health (is the container running?)
- Per-domain upstream/backend information
- SSL/HTTPS status per domain
- Last synchronization time
- Proxy recovery state
- Useful actions (reload, sync, test)

## 2. Goals of the new page

The Reverse Proxy page should become the central management interface
for the entire proxy subsystem.  It should answer these questions for
an administrator:

1. **Is the proxy healthy?** — container status, last reload, errors
2. **What domains are proxied?** — list with upstream, SSL, status
3. **How do I fix issues?** — reload, sync, recover actions

The page communicates with the ContainerCP core only — never with
nginx configuration files directly.

## 3. Proposed layout

```
┌─────────────────────────────────────────────────────────────┐
│  Reverse Proxy                      [Reload] [Sync] [Test]  │
├─────────────────────────────────────────────────────────────┤
│  Global Health                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  containercp-proxy:  Running  since 2025-07-10 14:00 │   │
│  │  nginx version:      1.27.0                          │   │
│  │  Last reload:        2025-07-10 14:05 (success)      │   │
│  │  Last recovery:      2025-07-10 13:50 (auto)         │   │
│  │  Configs:            4 active, 0 pending              │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  Proxy Entries                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  Domain         │ Site     │ Target     │ SSL │ Act. │   │
│  │  ────────────── │ ──────── │ ────────── │ ─── │ ──── │   │
│  │  example.com    │ mysite   │ 172.17.0.1│  ✓  │ […]  │   │
│  │                 │          │ :8081     │     │      │   │
│  │  test.com       │ testsite │ site-3-web│  ✗  │ […]  │   │
│  │                 │          │ :80       │     │      │   │
│  │  admin.domain   │ (admin)  │ 172.17.0.1│  ✓  │ […]  │   │
│  │                 │          │ :8081     │     │      │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## 4. Information that should be displayed

### Global proxy health (top section)

| Field | Source | How obtained |
|-------|--------|-------------|
| Container status | `NginxProxyProvider::central_proxy_running()` | Already public |
| nginx version | `docker exec containercp-proxy nginx -v` | New: one `CommandExecutor` call |
| Last reload time | Tracked in memory (new) | `ServiceRegistry` or `RecoveryManager` |
| Last reload result | Tracked in memory | Success / failure message |
| Last recovery time | Tracked in `RecoveryManager` | New accessor on `RecoveryManager` |
| Active config count | `ReverseProxyManager::list().size()` | Already available |
| Pending changes | Not applicable (core generates instantly) | — |

### Per-domain information (table rows)

| Column | Source | Example |
|--------|--------|---------|
| Domain | `ReverseProxy.domain` | `example.com` |
| Site | `ReverseProxy.site_id` → `SiteManager::find_by_id()` | `mysite` or `(admin)` |
| Upstream/Target | `ReverseProxy.upstream` | `172.17.0.1:8081` or `site-3-web:80` |
| HTTP | Always enabled if proxy exists | `✓` (implicit) |
| HTTPS | `CertificateStore::load_metadata(site_id).https_enabled` | `✓` or `✗` |
| Redirect | `CertificateStore::load_metadata(site_id).redirect_enabled` | `➜` if active |
| Status | `ReverseProxy.status` (from DB) or dynamic check | `active`, `error` |

### What NOT to show

- Raw nginx configuration text
- Config file paths
- nginx directive listings
- SSL certificate private key locations
- Docker inspect output

## 5. Operations available to administrators

| Operation | What it does | Calls |
|-----------|-------------|-------|
| **Reload** | `docker exec containercp-proxy nginx -s reload` | `NginxProxyProvider::reload()` |
| **Sync** | Regenerate all proxy configs from current state | `ServiceRegistry::sync_all_https_configs()` |
| **Recover** | Full proxy self-healing | `RecoveryManager::recover()` (or the three steps directly) |
| **Disable** | Remove nginx config, stop serving the domain | `NginxProxyProvider::disable_proxy()` |
| **Enable** | Regenerate nginx config for the domain | `NginxProxyProvider::create_proxy()` |
| **Remove** | Delete proxy entry and config file | Existing `POST /api/proxy/remove` (now fixed) |

### Actions per domain row

Each domain row should have a dropdown or button group with:

- Open domain (in browser)
- View site (navigate to Site Details)
- Disable / Enable
- Remove

### Global actions (header)

- **Reload** — apply any pending config changes
- **Sync** — regenerate all configs from core state
- **Test** — run `nginx -t` and display result

## 6. Which actions call the ContainerCP core

Every action calls an existing backend method:

| Action | Backend method | Already exists? |
|--------|---------------|-----------------|
| Reload | `services.proxy_provider().reload()` | ✅ `POST /api/runtime/.../reload` would need new route |
| Sync | `services.sync_all_https_configs()` | ✅ Public method, needs API route |
| Recover | `recovery().recover()` or three-step | ✅ Methods exist, needs API route |
| Disable domain | `services.proxy_provider().disable_proxy()` | ✅ Provider method, needs API route |
| Enable domain | `services.proxy_provider().create_proxy()` | ✅ Provider method, needs API route |
| Remove domain | `POST /api/proxy/remove` | ✅ Already exists |
| Proxy status | `GET /api/proxy` (enriched) | ✅ Already exists, needs enrichment |
| Global health | `central_proxy_running()` + new API | Needs new `GET /api/proxy/health` |

## 7. What should explicitly NOT exist

- **Nginx config viewer** — users never edit configs manually
- **Nginx config editor** — core generates configs, users should not bypass
- **Download config** — no legitimate use case
- **Upload custom config** — would bypass validation and rollback
- **Manual upstream editing** — upstream is determined by `SiteManager` / `DaemonApp`
- **SSL certificate editing** — belongs to SSL page
- **Domain record editing** — belongs to Domains page
- **Site management** — belongs to Sites page

## 8. Future expansion ideas

| Feature | Description | Requires |
|---------|-------------|----------|
| Backend health | Check if the upstream (e.g. `site-3-web:80`) is responding | HTTP health check module |
| Traffic metrics | Request count, bandwidth per domain | nginx status module or metrics collection |
| Access logs | View recent nginx access logs per domain | Log streaming module |
| Error logs | View recent nginx error logs | Log streaming module |
| Caching | Enable/configure nginx caching for specific domains | ProxyConfigBuilder extension |
| Rate limiting | Configure rate limits per domain | ProxyConfigBuilder extension |
| WAF rules | Web application firewall via nginx mod_security | Separate module |
| Maintenance mode | Return 503 for a domain during updates | `enable_proxy()` / new mode |
| Staged rollout | Route percentage of traffic to a different upstream | Advanced nginx config |
| Multiple providers | Support Traefik, Caddy alongside nginx | ProxyProvider interface |

## 9. Implementation order (recommended)

1. **Add `GET /api/proxy/health`** — returns global proxy status
2. **Add `POST /api/proxy/reload`** — calls `proxy_provider_.reload()`
3. **Add `POST /api/proxy/sync`** — calls `services.sync_all_https_configs()`
4. **Enrich `GET /api/proxy`** — add site name, SSL status via a `ProxyViewService`
5. **Redesign frontend** — global health card + enriched table + action buttons
6. **Add domain enable/disable** — API routes + frontend actions

## 10. Related documents

- `planning/proxy-page-redesign.md` — technical audit
- `docs/development/api-rules.md` — API design rules
- `docs/startup-architecture-review.md` — startup architecture
- `docs/testing.md` — testing strategy
