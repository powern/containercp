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

## Steps

### Step 1 — Config + Settings API (~1 commit)
- Add `server_hostname` field to Config (stored in settings.db or env)
- GET /api/settings returns server_hostname
- POST /api/settings saves server_hostname

### Step 2 — Admin proxy route (~1 commit)
- On daemon startup, if server_hostname is set:
  - Create ReverseProxy for domain → 127.0.0.1:8081
  - Call create_proxy() to generate nginx server block
  - If SSL cert exists, call attach_certificate()
- WebServer binds to 127.0.0.1 instead of 0.0.0.0

### Step 3 — SSL for admin panel (~1 commit)
- Settings page: "Issue SSL" button
- Uses existing POST /ssl/<domain>/issue
- Uses existing POST /ssl/<domain>/renew
- Certificate stored in CertificateStore like any other site

### Step 4 — Proxy cleanup (~1 commit)
- Ensure admin site_id is consistent
- Remove admin proxy config on hostname change
- Regenerate on restart if missing

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
