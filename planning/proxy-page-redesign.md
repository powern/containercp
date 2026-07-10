# Proxy Page — Current State & Future Plan

## Current state (audit completed)

### Where proxy configs are stored

| Location | Format | Purpose |
|----------|--------|---------|
| `/srv/containercp/database/reverse_proxies.db` | Pipe-delimited text | Persisted records (id, domain, site_id, provider, config_path, upstream, enabled, status) |
| `/srv/containercp/proxy/sites/{domain}.conf` | nginx config | Generated server blocks, mounted into `containercp-proxy` container |
| `ReverseProxyManager` (in-memory) | `std::vector<ReverseProxy>` | Runtime state, loaded at startup from `.db` |

### How generated nginx configs are created

`ProxyConfigBuilder::build(Params)` → generates one of:

- **HTTP-only** — single `server { listen 80; ... location / { proxy_pass ... } }`
- **HTTP + HTTPS** — two server blocks (HTTP proxy + HTTPS with SSL cert)
- **Redirect + HTTPS** — HTTP 301 + HTTPS server block

ACME challenge location is defined in `acme_challenge_location()` but **never emitted** — ACME is served by the Web UI server directly, not nginx.

Configs are written to disk, validated (`docker exec nginx -t`), and activated (`docker exec nginx -s reload`).

### Admin domain proxy vs site domain proxy

| Aspect | Admin (site_id=0) | Site (site_id=N) |
|--------|-------------------|-------------------|
| Upstream | Docker gateway IP:8081 (Web UI) | `site-N-web:80` (site container) |
| Created by | `ServiceRegistry::start()` on daemon boot | Site creation logic |
| SSL cert | site_id=0 | Site's actual ID |
| Regeneration | Always on startup | Only if HTTPS active |
| Config path | `{data_root}/proxy/sites/{hostname}.conf` | `{data_root}/proxy/sites/{domain}.conf` |

### What the Proxy page currently shows

| Column | Source | Display |
|--------|--------|---------|
| Domain | `ReverseProxy.domain` | Plain text |
| Provider | `ReverseProxy.provider` | Always "nginx" |
| Status | `ReverseProxy.status` | Plain text |
| Actions | — | Only Delete (X button) |

### What actions are safe or unsafe

| Action | Safe? | Notes |
|--------|-------|-------|
| Delete record | ❌ Unsafe | Deletes DB record only — **does not** remove nginx config file or reload nginx. Stale `.conf` remains on disk. |
| View configs | ✅ Safe | Read-only |
| Add proxy | ❌ No API endpoint | `POST /api/proxy/create` does not exist. Proxies are only created programmatically. |

### What belongs to Proxy vs other modules

| Page | Owns | Should not duplicate |
|------|------|---------------------|
| **Sites** | Site creation (auto-creates proxy) | Proxy config details, SSL actions |
| **SSL** | HTTPS enable/disable, certificate attach/detach | Proxy upstream, domain binding |
| **Domains** | Domain records and types | Proxy config, SSL status |
| **Proxy** | Proxy entries, upstream management, SSL certificate binding (summary) | Certificate issuance, domain CRUD |

### Redirect bug

`redirect/enable` API endpoint calls `attach_certificate()` which always uses `redirect=false`. The `redirect=true` code path in `ProxyConfigBuilder::build()` is never reached — the redirect nginx config is never generated.

---

## Future direction for the Proxy page

### Target table columns

| Column | Source | Display |
|--------|--------|---------|
| Domain | `ReverseProxy.domain` | Clickable link, copy |
| Site | `ReverseProxy.site_id` → `SiteManager.find_by_id()` | Site name + domain, or "Admin Panel" for site_id=0 |
| Upstream | `ReverseProxy.upstream` | Human-readable target (site container, admin gateway) |
| Type | Derived | HTTP-only, HTTPS, HTTPS+Redirect |
| SSL | `CertificateStore.load_metadata(site_id)` | Badge: Active, Disabled, Expired, Error |
| Status | `ReverseProxy.status` + nginx check | Badge: active, error, disabled |
| Actions | — | Open, View site, Remove |

### Actions (safe)

- **Open domain** — open `https://{domain}` in browser
- **View site** — navigate to site detail (if site_id > 0)
- **Remove** — fix to properly delete config file + reload nginx
- **Copy domain** — clipboard

**Do NOT add:**

- SSL issue/renew/enable/disable buttons — those belong on SSL page
- Edit upstream — no update endpoint exists yet
- Add proxy — no create endpoint exists yet

### Data enrichment pattern

Following the `DomainViewService` pattern, proxy data should be enriched at the API layer:

```
ApiServer (thin)
  → ProxyViewService (read-only)
    → ReverseProxyManager (proxy records)
    → SiteManager (site name)
    → CertificateStore (SSL status)
  → enriched JSON → Web UI
```

### Bugs fixed (July 2026)

1. **`POST /api/proxy/remove`** — ✅ now deletes the `.conf` file, validates nginx config, reloads nginx. Previously only removed the DB record.
2. **`redirect/enable`** — ✅ now passes `redirect=true` to `attach_certificate()`, which generates the redirect+HTTPS nginx config. `ProxyConfigBuilder::Params.redirect` is no longer dead code.

### What NOT to change

- SSL certificate management stays in SSL module
- Site proxy creation stays in site creation flow
- Admin panel proxy stays in ServiceRegistry startup
- The central proxy container management stays in NginxProxyProvider
- nginx remains the only provider for now

---

## Implementation order (future)

1. **Fix bugs first** — `POST /api/proxy/remove` (config cleanup), `redirect/enable` (config generation)
2. **Add `ProxyViewService`** — enriched read-only API response
3. **Update frontend** — new table with all columns, safe actions only
4. **Add `POST /api/proxy/create`** — API-first compliance (separate epic)

---

## Related documents

- `docs/api/API_REFERENCE.md` — API endpoint reference
- `docs/development/api-rules.md` — API design rules
- `docs/development/single-source-of-truth.md` — SSOT rules
- `planning/product-roadmap.md` — version milestones
