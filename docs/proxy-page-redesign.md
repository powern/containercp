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

## 9. Stage 1 implementation (completed)

### Implemented endpoints

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/proxy` | Enriched proxy entry list (was: raw ReverseProxy list) |
| GET | `/api/proxy/health` | Global proxy health (container, config test, entry counts) |
| POST | `/api/proxy/test` | Validate nginx configuration (`nginx -t`) |
| POST | `/api/proxy/reload` | Validate then reload nginx |
| POST | `/api/proxy/sync` | Regenerate all HTTPS configs from core state |
| POST | `/api/proxy/recover` | Full proxy self-healing (calls `RecoveryManager::recover_now()`) |

### New core methods

| Method | Class | Purpose |
|--------|-------|---------|
| `test_config()` | `NginxProxyProvider` | Runs `nginx -t` inside the proxy container |
| `recover_now()` | `RecoveryManager` | Synchronous recovery with concurrency protection |
| `build_enriched_json()` | `ProxyViewService` | Enriched proxy list with site name, SSL, protection flag |
| `build_health_json()` | `ProxyViewService` | Global health status |

### Protected admin entry

- `ReverseProxy` records with `site_id == 0` are classified as `"system"` (admin)
- The `protected` field is `true` for admin entries
- The frontend hides Remove/Disable buttons for protected entries
- System entries show a `system` badge in the Domain column

### Frontend changes

- **Global health card** at top of page: container state, provider, config
  test result, entry counts, action buttons (Test, Reload, Sync, Recover)
- **Enriched table**: Domain (with system badge), Type/Site, Upstream,
  HTTP, HTTPS, State, Backend Health (Unknown), Actions
- **Action buttons**: Test, Reload, Sync, Recover — all call the new API
  endpoints, show progress toast, refresh page on completion
- Provider column removed (was always "nginx")

### Stage 1 Polish Final (UX improvements)

- **Recovery info in health card**: Recovery Manager status (Running/Stopped),
  recovery in progress indicator, last recovery timestamp, last recovery
  result (Success/Failed).  Data sourced from `RecoveryManager::status()`.
- **Auto-refresh after actions**: Action buttons disable on click, show
  spinner text, refresh health card and proxy entries via API (no full
  page reload).  Uses `_proxyActionPending` guard to prevent double-clicks.
- **Better health labels**: "Not tested" instead of "Unknown" for config
  test and backend health.  "Never since daemon start" for recovery time
  when no recovery has occurred.
- **Health card layout**: 3-column responsive grid with 10 data fields:
  Container, Provider, Configuration, Config Detail, Recovery Manager,
  Recovery In Progress, Last Recovery, Last Result, Proxy Entries.
  Action buttons placed below a separator.
- **Button feedback**: Each button shows action-specific text while running
  (e.g. "Testing...", "Reloading...").  Buttons are disabled during the
  operation and the `_proxyActionPending` flag prevents concurrent clicks.
- **Loading guard fix**: `p._loading` always cleared on error (API calls
  in separate try block before rendering).
- **All buttons disabled during operation**: `setProxyButtonsEnabled()`
  disables Test/Reload/Sync/Recover while any proxy action is running.
- **Health column → Backend**: Column renamed from "Health" to "Backend",
  displays "Not checked".  Accurate until real backend probing is added.
- **nginx version**: One-shot `docker exec nginx -v` on first health fetch,
  cached for daemon lifetime.  Displayed as "nginx 1.27.x" in Global Health.
- **Entry badges**: Proxy entries summary uses individual badges
  ("3 Total", "1 System", "2 Sites") instead of inline text.

### Final page layout (approximate)

```
┌─────────────────────────────────────────────────────────────┐
│  Reverse Proxy                              [admin panel]   │
├─────────────────────────────────────────────────────────────┤
│  ┌─ Global Health ───────────────────────────────────────┐  │
│  │  Container     Running     │ Provider    nginx         │  │
│  │  Configuration Valid       │ Config Det. config valid  │  │
│  │  Recovery Mgr  Yes         │ Recov. Prog. No           │  │
│  │  Last Recovery Never...    │ Last Result None          │  │
│  │  Proxy Entries  3 total (1 system, 2 site)             │  │
│  │  ───────────────────────────────────────────────────  │  │
│  │  [Test] [Reload] [Sync] [Recover]                     │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌─ Proxy Entries ───────────────────────────────────────┐  │
│  │  Domain       │ Type   │ Upstream      │ H │ S │ Act  │  │
│  │  web2.sof...  │ System │ 172.17.0.1:80 │ ✓ │ ✓ │ […]  │  │
│  │  site.com     │ Site   │ site-3-web:80 │ ✓ │ ✗ │ […]  │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### Final Review (all items resolved)

| # | Issue | Resolution |
|---|-------|-----------|
| 1 | Configuration state contradiction | Fixed: when `last_test_result()` defaults to `{false, "Not tested..."}`, the frontend checks the message content and shows "Not tested" badge instead of "Failed". Only an explicit failed test shows "Failed". |
| 2 | Backend column (placeholder) | Column renamed from "Health" to "Backend" with "Not checked" text. This is a deliberate placeholder for future backend probing. A probe would perform `HEAD /` through the Docker network with timeout and display Healthy/Down/Timeout. |
| 3 | Future backend probe | Technically feasible: `docker exec containercp-proxy curl -sI --connect-timeout 5 http://site-3-web:80/` from the nginx container. RuntimeActionExecutor could perform this. Recommended for Stage 2 or a future Observability module. |
| 4 | Type / Site column duplication | Fixed: now shows "Admin Panel" for system entries (site_id=0) and site owner name for site entries. No longer duplicates the Domain column. |
| 5 | Recovery labels | Changed: "Yes" → "Running", "No" → "Idle" (Recovery In Progress), "None" → "Never" (Last Result). More user-friendly and operationally descriptive. |
| 6 | Final UX review | Configuration/Config Detail consistency, Recovery Manager labels, Type/Site column deduplication, badge colors reviewed. All resolved in commit `b38b7fa`. |

### Backend column future design

The Backend column is a placeholder for real upstream health probing.

A future implementation would:

1. Perform `HEAD /` or `GET /` through the Docker network to each upstream
   (e.g. `http://site-3-web:80/`), with a short timeout (e.g. 5 seconds).
2. Map results: HTTP 2xx/3xx → "Healthy", connection refused → "Down",
   timeout → "Timeout".
3. The probe could use `RuntimeActionExecutor` or `CommandExecutor`
   from inside the nginx proxy container via `docker exec`.
4. Cache results with a TTL (e.g. 60 seconds) to avoid hammering backends.
5. This is explicitly **Stage 2 / deferred** — not implemented yet.

### Deferred (Stage 2)

- Per-domain Enable/Disable
- Per-domain Remove for non-protected entries (already exists via
  `POST /api/proxy/remove`)
- Real backend health probing (currently "Not checked" placeholder)
- Recovery event persistence (currently daemon-start scope)

## 10. Related documents

- `planning/proxy-page-redesign.md` — technical audit
- `docs/development/api-rules.md` — API design rules
- `docs/startup-architecture-review.md` — startup architecture
- `docs/testing.md` — testing strategy
