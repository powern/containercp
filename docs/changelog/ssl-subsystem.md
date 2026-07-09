# SSL & HTTPS Subsystem

Detailed changelog entries for SSL/HTTPS development.

---

## 2025-07-08 | `c16c776` | ACME challenge via Web UI + Bootstrap SSL removed

### ✅ HTTPS on port 443 — validated
```
curl -Ik https://web2.softico.ua
→ HTTP/1.1 200 OK
→ Admin Panel login page
```

### Bootstrap simplified
- Removed SSL step from Setup Wizard (Bootstrap only saves hostname)
- Admin panel SSL is configured after Bootstrap via Settings → Admin Panel HTTPS
- The issue SSL + renew buttons remain in Settings for post-setup HTTPS

## 2025-07-08 | `1cc6a09` | Fix 403 on ACME challenge — alias + permissions

### Debug commands (403 Forbidden — nginx can't read file)
```bash
# 1. Fix directory permissions (nginx runs as 'nginx' user in container)
chmod -R 755 /srv/containercp/ssl/0/

# 2. Verify file exists inside container
docker exec containercp-proxy cat /srv/containercp/ssl/0/.well-known/acme-challenge/<TOKEN>

# 3. Check nginx error log
docker exec containercp-proxy cat /var/log/nginx/error.log

# 4. Test with correct Host header
wget --header="Host: web2.softico.ua" http://127.0.0.1/.well-known/acme-challenge/<TOKEN>

# 5. Validate config + reload
docker exec containercp-proxy nginx -t && docker exec containercp-proxy nginx -s reload
```

## 2025-07-08 | `a8f6207` | Always regenerate admin proxy config on startup
- Admin nginx config now regenerated on EVERY daemon restart
- Previously only created once — config fixes never took effect

### Debug commands (after config change)
```bash
# 1. Reload nginx inside container
docker exec containercp-proxy nginx -s reload

# 2. Test challenge (127.0.0.1 + Host header bypasses DNS)
wget --header="Host: web2.softico.ua" http://127.0.0.1/.well-known/acme-challenge/<TOKEN>

# 3. Check if challenge file exists inside container
docker exec containercp-proxy ls -la /srv/containercp/ssl/0/.well-known/acme-challenge/

# 4. Read current nginx config
docker exec containercp-proxy cat /etc/nginx/conf.d/web2.softico.ua.conf

# 5. Validate nginx config
docker exec containercp-proxy nginx -t
```

## 2025-07-08 | `a2e4356` | Fix HTTP-01 challenge verification (missing Host header)
- Docker exec wget now includes `--header='Host: <domain>'` so nginx
  correctly matches the admin server block
- Added Step B2: curl via host 127.0.0.1:80 with correct Host header
- Steps B and B2 added between file check and ACME notify

### Debug commands (HTTP-01 challenge)
```bash
# 1. Check if admin config exists inside proxy container
docker exec containercp-proxy ls -la /etc/nginx/conf.d/web2.softico.ua.conf

# 2. Read the generated nginx config
docker exec containercp-proxy cat /etc/nginx/conf.d/web2.softico.ua.conf

# 3. Test challenge with correct Host header (bypasses DNS)
wget --header="Host: web2.softico.ua" http://127.0.0.1/.well-known/acme-challenge/<TOKEN>

# 4. If step 3 works but ACME fails — check DNS/public port 80
# 5. Check nginx config is valid
docker exec containercp-proxy nginx -t
# 6. reload nginx after config changes
docker exec containercp-proxy nginx -s reload
```

## 2025-07-08 | `7dc9ee1` | Bootstrap/Normal mode architecture

- StartupManager selects bootstrap or normal mode
- Bootstrap: lightweight HTTP server on 0.0.0.0:80 with Setup Wizard
- Normal: full daemon on 127.0.0.1:8080/8081, through proxy
- Recovery: if proxy fails → mark incomplete → next boot goes to bootstrap
- New files: StartupManager.h/.cpp, bootstrap_server.cpp

## 2025-07-08 | `611123f` | Steps 2-4: Admin Panel HTTPS — proxy route, SSL, cleanup
- Admin proxy created on startup if server_hostname is set (→ 127.0.0.1:8081)
- SSL cert auto-attached if exists (site_id=0)
- WebServer binds to 127.0.0.1 (port 8081 closed for external access)
- Settings page: Save Hostname + Issue SSL + Renew SSL buttons
- Updated WEB-UI.md, admin-panel-https.md, project-status.md

## 2025-07-08 | `b9581c7` | Step 1: Admin Panel HTTPS — Config + Settings API
- Config::server_hostname() with env var + file storage
- GET/POST /api/settings endpoints
- Settings GUI: hostname input + SSL action buttons
- WebServer bind address changed to 127.0.0.1

## 2025-07-08 | `19c1bda` | Fix SSL auto-renew display for HTTP-only sites + add tests
- API returns auto_renew=false for HTTP_ONLY sites
- GUI shows N/A for HTTP_ONLY, Yes/No for active
- 8 new test cases for auto-renew persistence

## 2025-07-08 | `7a49592` | Fix: reload nginx after certificate issue/renew
- Job lambda now calls proxy_provider().reload() after issue/renew
- Nginx picks up new certificate via updated current symlink

## 2025-07-08 | `bf70072` | Detect staging certificates in production mode
- Parse issuer/subject from PEM via X509_NAME
- Startup log: env + issuer + decision (keep/reissue)
- Warning if production env but staging issuer

## 2025-07-08 | `9074557` | Production-ready SSL finalization
- Production Let's Encrypt as default (staging via LETSENCRYPT_STAGING=1)
- PEM date parsing (notBefore/notAfter via OpenSSL)
- Environment field in metadata + GUI
- Dead code removed: extract_upstream, find_json_string_array, fallbacks
- Startup upstream normalization fixes legacy DB values

## 2025-07-08 | `441c1ab` | Comprehensive SSL/Proxy subsystem cleanup

### Architectural fixes

1. **Canonical upstream source of truth** — upstream resolved from
   ReverseProxyManager, never parsed from nginx config files.
   `attach_certificate()`/`detach_certificate()` use
   `proxy_mgr_.find_by_domain(domain)->upstream`.

2. **SSL mount in central proxy** — `ensure_central_proxy()` now mounts
   `/srv/containercp/ssl/` as read-only volume. Missing mount detected
   and container recreated automatically.

3. **Transactional API handlers** — enable/disable/redirect now validate
   nginx config BEFORE saving metadata. Metadata only saved after
   successful proxy change and reload.

4. **No fallback upstream** — removed all `site-0-web:80` fallbacks.
   Missing upstream = clear error, not silent wrong value.

5. **SiteCreateOperation validates proxy** — checks `create_proxy()` and
   `reload()` results. Failure = site creation fails.

6. **Reload checks exit code** — checks `WEXITSTATUS(rc)` instead of
   parsing stderr. Returns real nginx error messages.

7. **Removed `extract_upstream()`** — no more config file parsing.

### Files changed
- `libs/proxy/NginxProxyProvider.h/.cpp` — ReverseProxyManager ref,
  canonical upstream, SSL mount, no fallbacks
- `libs/core/ServiceRegistry.cpp` — pass ReverseProxyManager
- `libs/api/ApiServer.cpp` — transactional handlers
- `libs/operations/SiteCreateOperation.cpp` — check proxy results
- `CHANGELOG.md` — this entry

---

## 2025-07-08 | `(this commit)` | SSL Step 8A: real ACME HTTP-01 staging implementation

### New: Real ACME HTTP-01 client
- Full ACME protocol implementation via libcurl + OpenSSL
- JWS/JWT signing with ES256 (P-256 ECDSA) for ACME authentication
- Account key generation (P-256) and registration
- Directory discovery (staging + production Let's Encrypt)
- Order creation with domain identifiers
- Authorization fetch with HTTP-01 challenge selection
- Challenge response and polling (up to 15 retries, 2s interval)
- CSR generation (P-256 key, CN-based)
- Order finalization and certificate download

### Updated: HTTP01ChallengeProvider
- Real challenge token write to `/.well-known/acme-challenge/<token>`
- Token verification via HTTP (checks reachability)
- Preflight validation: domain reachability check
- Cleanup after validation

### Updated: LetsEncryptProvider
- `request()` now resolves site_id from CertificateStore and calls `issue_certificate()`
- Staging mode by default (`LETSENCRYPT_STAGING=0` for production)
- Full ACME lifecycle orchestration
- API handler saves placeholder metadata before queuing the job

### Build dependencies
- Added libcurl4-openssl-dev + libssl-dev
- CMakeLists: link curl, ssl, crypto libraries for containercpd and tests

### Files changed
- `libs/ssl/AcmeClient.h/.cpp` — complete ACME HTTP-01 implementation
- `libs/ssl/HTTP01ChallengeProvider.h/.cpp` — real challenge file I/O
- `libs/ssl/LetsEncryptProvider.h/.cpp` — wired to real ACME flow
- `libs/core/ServiceRegistry.cpp` — staging config, HTTP01ChallengeProvider ssl_root
- `libs/api/ApiServer.cpp` — save metadata before issue job
- `CMakeLists.txt`, `tests/CMakeLists.txt` — libcurl + OpenSSL linkage

---

## 2025-07-08 | `6f8917a` | SSL Step 7: minimal production GUI

### Updated: SSL Web UI page (`web/app.js`)
- Complete rewrite of the SSL page to use the new REST API
- Table shows ALL sites with: Domain, Status, HTTPS, Provider, Expires, Auto Renew
- Status badges: HTTP_ONLY (info), ACTIVE (green), ERROR (red), DISABLED (gray), ISSUING (yellow)
- Action buttons per SSL state:
  - HTTP_ONLY/ERROR → [Issue Certificate]
  - ACTIVE + HTTPS ON → [Disable HTTPS], [Renew], [Toggle Redirect]
  - ACTIVE + HTTPS OFF → [Enable HTTPS], [Renew]
  - DISABLED → [Enable HTTPS]
- All actions call path-based REST endpoints (`/api/ssl/<domain>/<action>`)
- Site detail SSL card is clickable — navigates to SSL page
- `navigateTo()` helper for programmatic page navigation
- Error messages parsed correctly from new `{code, message, details}` format

### Files changed
- `web/app.js` — rewrite of SSL page, action functions, status badges
- `CHANGELOG.md` — this entry

---

## 2025-07-08 | `492558a` | SSL Step 6: automatic renewal scheduler

### New: RenewalScheduler (`libs/ssl/RenewalScheduler.h/.cpp`)
- Fully automatic certificate renewal inside the daemon process
- Periodic background thread (default: 24h interval)
- Scans all certificates, renews due ones via JobExecutor
- Skips: HTTP_ONLY sites, disabled HTTPS, auto_renew=false, ERROR state,
  providers without auto-renew support
- Exponential backoff on failure (1h, 2h, 4h, 8h, 16h, 24h, 24h...)
- After 7 consecutive failures, status → ERROR with clear message
- Updates metadata.json after success/failure (renew_attempts, last_error)
- Manages next_attempt timestamp for backoff tracking
- Structured log events: renewal scheduled/started/succeeded/failed/skipped
- `check_now()` for manual/forced immediate check

### Improvements
- Explicit ServiceRegistry::start()/shutdown() lifecycle:
  `services.start()` called after daemon init
  `services.shutdown()` called before daemon exit
  JobExecutor and RenewalScheduler shut down cleanly in order
- ProxyConfigBuilder helper class extracts nginx config generation:
  build_http_block, build_https_block, build_redirect_block, build()
  NginxProxyProvider delegates to ProxyConfigBuilder
  Future: HSTS, HTTP/2, HTTP/3, OCSP, mTLS

### Files changed
- `libs/ssl/RenewalScheduler.h/.cpp` — new automatic renewal scheduler
- `libs/core/ServiceRegistry.h/.cpp` — wired RenewalScheduler, start/shutdown
- `app/containercpd/main.cpp` — services.start()/shutdown() calls
- `libs/proxy/ProxyConfigBuilder.h/.cpp` — new config generation helper
- `libs/proxy/NginxProxyProvider.h/.cpp` — uses ProxyConfigBuilder
- `libs/proxy/ProxyProvider.h` — unused parameter warnings fixed
- `CMakeLists.txt` — added new sources

---

## 2025-07-08 | `31e8820` | SSL Step 5: JobExecutor + transactional HTTPS proxy integration

### New: JobExecutor (`libs/jobs/JobExecutor.h/.cpp`)
- Reusable background task executor with worker pool
- Configurable worker count (default 2) and bounded task queue (default 64)
- Thread-safe submission via `submit(job_id, task)` — returns false if queue full
- Graceful shutdown: waits for running workers, cancels pending tasks
- Replaces all detached `std::thread` usage in ApiServer SSL handlers
- Shared by all subsystems (SSL issuance, renewal, future backup, etc.)

### New: ProxyProvider certificate attachment
- `ProxyProvider::attach_certificate(domain, cert_path, key_path)` — adds HTTPS
  server block with ssl_certificate directives to nginx config
- `ProxyProvider::detach_certificate(domain)` — removes HTTPS block,
  reverts to HTTP-only. Certificate files kept on disk.
- Transactional config generation: write temp file, validate nginx syntax,
  atomic rename, reload
- HTTPS config never breaks HTTP — HTTP block always present
- `POST /ssl/<domain>/enable` calls `attach_certificate()` via proxy
- `POST /ssl/<domain>/disable` calls `detach_certificate()` via proxy

### Files changed
- `libs/jobs/JobExecutor.h/.cpp` — new worker pool executor
- `libs/proxy/ProxyProvider.h` — attach_certificate / detach_certificate
- `libs/proxy/NginxProxyProvider.h/.cpp` — nginx HTTPS config generation
- `libs/api/ApiServer.cpp` — proxy calls in enable/disable handlers
- `libs/core/ServiceRegistry.h/.cpp` — JobExecutor wired in
- `app/containercpd/main.cpp` — JobExecutor started on daemon init
- `CMakeLists.txt`, `tests/CMakeLists.txt` — added JobExecutor source

---

## 2025-07-08 | `efc66e6` | SSL Step 4: reusable ACME client foundation

### New: AcmeClient (`libs/ssl/AcmeClient.h/.cpp`)
- RFC 8555 ACME protocol engine with layered architecture:
  - `discover_directory()` — fetch ACME directory URLs
  - `load_or_create_account()` — manage P-256 account key
  - `create_order()` — request new certificate order for domains
  - `get_authorization()` — get domain authorization with challenges
  - `respond_to_challenge()` — signal readiness to ACME server
  - `poll_challenge()` / `poll_authorization()` / `poll_order()` — wait for validation
  - `finalize_order()` — submit CSR and finalize
  - `download_certificate()` — retrieve fullchain PEM
  - `generate_csr()` / `generate_account_key()` — OpenSSL helpers (TODO)
  - `url64()` / `sha256_base64()` — ACME encoding utilities (TODO)
- Staging mode via `set_staging(true)` — uses acme-staging-v02 endpoint

### Updated: LetsEncryptProvider (adapter pattern)
- Now holds `AcmeClient`, `ChallengeProvider`, and `CertificateStore` references
- `request()` / `renew()` call `preflight_validation()` then `issue_certificate()`
- `issue_certificate()` orchestrates full ACME lifecycle: account → order →
  authz → challenge (via ChallengeProvider) → finalize → download → store
- Preflight checks: rejects localhost, .local, .test before ACME contact
- All ACME protocol logic isolated in AcmeClient — provider is just an adapter

### Improvements
- Issue/renew jobs are truly async: HTTP 202 + detached worker thread
- provider_id validated before job creation (400 INVALID_PROVIDER)
- POST /enable uses CertificateStore::validate() for integrity check
- POST /redirect/enable requires active cert + HTTPS enabled

### Files changed
- `libs/ssl/AcmeClient.h/.cpp` — new ACME protocol engine (placeholder methods)
- `libs/ssl/LetsEncryptProvider.h/.cpp` — rewritten as AcmeClient adapter
- `libs/core/ServiceRegistry.cpp` — pass CertificateStore to provider
- `libs/api/ApiServer.cpp` — async job threads, provider_id validation
- `CMakeLists.txt`, `tests/CMakeLists.txt` — added AcmeClient source

---

## 2025-07-08 | `a45067d` | Improvements: async jobs, provider_id lookup, validate() checks

## 2025-07-08 | `f606dc2` | SSL Step 3: certificate management REST API

### New: SSL REST API endpoints
- `GET /api/ssl` — lists ALL sites (including HTTP_ONLY) with SSL state
- `GET /api/ssl/providers` — lists available certificate providers
- `GET /api/ssl/<domain>` — full certificate details for a domain
- `GET /api/ssl/<domain>/status` — quick status check
- `POST /api/ssl/<domain>/issue` — async certificate issuance via provider
- `POST /api/ssl/<domain>/renew` — async renewal via stored provider
- `POST /api/ssl/<domain>/enable` — enable HTTPS for active certificate
- `POST /api/ssl/<domain>/disable` — disable HTTPS, keep certificate files
- `POST /api/ssl/<domain>/redirect/enable` — enable HTTP→HTTPS redirect
- `POST /api/ssl/<domain>/redirect/disable` — disable redirect

### API design features
- Consistent JSON error format: `{"success":false,"error":{"code":"...","message":"...","details":{}}}`
- HTTP status codes: 200 success, 400 invalid, 404 not found, 409 state conflict
- Issue/renew return `job_id` with async status (jobs exist but ACME is placeholder until Step 4)
- Missing metadata for existing sites → `HTTP_ONLY` status, not error
- Corrupted metadata → `ERROR` status with descriptive message
- Enable fails safely (409) if no valid certificate exists
- Redirect enable fails (409) if HTTPS is not active
- Provider selection via `provider_id` in request body (default: "letsencrypt")
- Private key content/paths never exposed in responses

### Router enhancement
- `Router::add_prefix(method, prefix, handler)` — prefix-based route matching
- Exact routes take priority over prefix routes (checked in order)

### Files changed
- `libs/api/ApiServer.cpp` — all SSL endpoints, json_error helper, ssl_domain_from_path
- `libs/api/Router.h/.cpp` — add_prefix method for prefix-based routing
- `tests/test_api.cpp` — Router prefix tests, SSL response format tests, CertificateStore integration test

---

## 2025-07-08 | `a82abce` | Fix: atomic current/next symlink layout + MetadataLoadResult

### Fixed: save_all now uses symlink-based atomic swap
- Storage layout changed to versioned directory with atomic symlink:
  `/srv/containercp/ssl/<site-id>/versions/<n>/` + `current -> versions/<n>/`
- `save_all` writes complete new version to `versions/<n+1>/`, fsyncs it,
  then atomically swaps the `current` symlink via `rename()`
- If any write in the new version fails, the old version is untouched —
  the symlink is never modified
- Old versions cleaned up (keep current + 1 backup)
- Flat-file migration: existing files under `<site-id>/` are auto-migrated
  to versioned layout on first access

### Fixed: MetadataLoadResult with correct LoadError types
- `INVALID_JSON` for malformed or unparseable JSON (version = 0)
- `NOT_FOUND` when no certificate data exists for a site
- `IO_ERROR` for empty or unreadable files
- `UNSUPPORTED_VERSION` when metadata version > 1 (future format)
- `INVALID_SCHEMA` for site_id mismatch
- Each error returns a human-readable message with context

### Tests
- 91 test cases, 391 assertions, all pass
- New tests: versioned symlink layout, multiple versions with cleanup,
  unsupported version returns UNSUPPORTED_VERSION, symlink integrity

### Files changed
- `libs/ssl/CertificateStore.h/.cpp` — symlink layout, MetadataLoadResult,
  flat-file migration, version cleanup
- `tests/test_cert_store.cpp` — 8 new tests for versioned layout
- `CHANGELOG.md` — this entry

---


---
Back to [CHANGELOG.md](../../CHANGELOG.md)
