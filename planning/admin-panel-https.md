# Admin Panel HTTPS

## Objective

Secure the ContainerCP admin panel (Web UI) with HTTPS through the
existing central proxy. Replace direct `http://<server>:8081/` access
with `https://admin.<domain>/` via containercp-proxy.

## Why

- Login credentials currently sent in plaintext over port 8081
- Admin panel should not be directly exposed to the network
- Uses existing ACME infrastructure with auto-renewal
- Hostname change → reissue certificate (no manual steps)

## Architecture

```
Browser
   │
   ▼
https://admin.example.com:443
   │
   ▼
containercp-proxy (port 443, nginx)
   │  ssl_certificate / ssl_certificate_key
   │  proxy_pass http://127.0.0.1:8081
   ▼
WebServer (127.0.0.1:8081, localhost only)
```

## Components

| Component | Purpose | Source |
|-----------|---------|--------|
| Settings → hostname | Configure admin domain | New field in Settings API |
| AdminSite | Virtual "site" for admin panel proxy | Managed in memory |
| CertificateStore | Store admin panel certificate | Existing |
| LetsEncryptProvider | Issue/renew admin cert | Existing |
| NginxProxyProvider | Generate proxy server block | Existing, extended with admin route |
| ensure_central_proxy() | Add admin config to proxy startup | Modified |
| WebServer listen address | Bind to 127.0.0.1 only | Modified (remove 0.0.0.0) |

## Validation status

| Date | Result |
|------|--------|
| 2025-07-09 | ✅ HTTPS on port 443 works (`curl -Ik https://web2.softico.ua` → 200) |
| 2025-07-09 | ✅ ACME challenge served correctly |
| 2025-07-09 | ✅ Admin Panel login through reverse proxy |
| 2025-07-09 | ✅ Bootstrap simplified (no SSL step) |

## Steps

### Step 1 — Config + Settings API ✅
- Add `server_hostname` field to Config (env + file storage)
- GET /api/settings returns server_hostname
- POST /api/settings saves server_hostname
- Commit: `b9581c7`

### Step 2 — Admin proxy route ✅
- On daemon startup, if server_hostname is set:
  - Create ReverseProxy for domain → 127.0.0.1:8081
  - Call create_proxy() to generate nginx server block
  - If SSL cert exists (site_id=0), call attach_certificate()
- WebServer binds to 127.0.0.1 instead of 0.0.0.0

### Step 3 — SSL for admin panel ✅
- Settings page: "Issue SSL" and "Renew SSL" buttons
- Uses existing POST /ssl/<domain>/issue and /renew endpoints
- Certificate stored in CertificateStore (site_id=0 for admin)
- Auto-renew handled by existing RenewalScheduler

### Step 4 — Proxy cleanup ✅
- Admin proxy created on startup if hostname is set
- Hostname change: save via Settings API, restart daemon
- Old config replaced on next startup

## Files to change
- `libs/api/ApiServer.cpp` — settings endpoints
- `app/containercpd/main.cpp` — WebServer bind address
- `libs/api/WebServer.cpp` — bind to 127.0.0.1
- `libs/core/ServiceRegistry.cpp` — startup admin proxy setup
- `libs/proxy/NginxProxyProvider.cpp` — admin route handling
- `web/app.js` — Settings page hostname field + buttons
- `web/index.html` — minor updates if needed
- `docs/WEB-UI.md` — update access instructions

## Risks
- Daemon restart needed after hostname change
- Old hostname cert becomes invalid after DNS change (must reissue)
- Port 8081 must still be accessible locally for SSH tunnel
