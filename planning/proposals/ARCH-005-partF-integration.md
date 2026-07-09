## Proxy Integration

### Responsibility split

| Component | Responsibility |
|-----------|---------------|
| LetsEncryptProvider | ACME lifecycle, disk I/O, certificate files |
| RenewalScheduler | Background renewal checks |
| NginxProxyProvider | Generate nginx config with/without SSL, reload |

### Nginx config generation

**HTTP-only (default):**
```nginx
server {
    listen 80;
    server_name example.com;
    root /var/www/html;
    ...
}
```

**HTTPS enabled (no redirect):**
```nginx
server {
    listen 80;
    server_name example.com;
    root /var/www/html;
    ...
}

server {
    listen 443 ssl;
    server_name example.com;
    ssl_certificate /srv/containercp/ssl/example.com/fullchain.pem;
    ssl_certificate_key /srv/containercp/ssl/example.com/privkey.pem;
    root /var/www/html;
    ...
}
```

**HTTPS enabled (with redirect):**
```nginx
server {
    listen 80;
    server_name example.com;
    return 301 https://$host$request_uri;
}

server {
    listen 443 ssl;
    server_name example.com;
    ssl_certificate /srv/containercp/ssl/example.com/fullchain.pem;
    ssl_certificate_key /srv/containercp/ssl/example.com/privkey.pem;
    root /var/www/html;
    ...
}
```

### ACME challenge serving

The central nginx proxy must serve `/.well-known/acme-challenge/` from
disk. This is configured once at proxy setup:

```nginx
location /.well-known/acme-challenge/ {
    root /srv/containercp/ssl;
}
```

This allows the LetsEncryptProvider to write challenge tokens to
`/srv/containercp/ssl/<domain>/.well-known/acme-challenge/<token>`
and have them served immediately on port 80.

### Future reverse proxy compatibility

Business logic must not contain nginx-specific SSL code. The
`ProxyProvider` interface abstracts all proxy operations:

```cpp
class ProxyProvider {
    virtual OperationResult attach_certificate(const std::string& domain,
                                                const std::string& cert_path,
                                                const std::string& key_path) = 0;
    virtual OperationResult detach_certificate(const std::string& domain) = 0;
};
```

Future proxy implementations:

| Proxy | Class | Status |
|-------|-------|--------|
| nginx | `NginxProxyProvider` | Current, implements SSL config |
| Caddy | `CaddyProxyProvider` | Future: uses Caddyfile for SSL |
| Traefik | `TraefikProxyProvider` | Future: uses Traefik labels/config |
| HAProxy | `HAProxyProxyProvider` | Future: uses HAProxy PEM + bind |

Each implementation handles SSL config in its own format.
`CertificateProvider` and `RenewalScheduler` are unchanged.

### Automatic certificate invalidation

When domains attached to a site change (currently single domain,
future multi-domain), the certificate becomes stale.

Detection:
- On daemon startup and after any site domain change, compare
  `SslCertificate.domains` against the site's current domains.
- If they differ and status is `active`, transition to `error` with:
  `"Certificate does not match current domains. Reissue required."`.
- HTTPS continues working with the old certificate until the user
  reissues or the cert expires, whichever comes first.

This ensures the platform never silently uses an incomplete
certificate. The GUI shows a clear warning and prompts reissue.

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `CONTAINERCP_SSL_DIR` | `/srv/containercp/ssl` | Certificate storage root |
| `CONTAINERCP_ACME_EMAIL` | (unset) | Email for Let's Encrypt account registration |
| `LETSENCRYPT_STAGING` | (unset) | When set, use ACME staging directory |
| `CONTAINERCP_RENEWAL_INTERVAL` | `24` | Hours between renewal checks |

## Implementation Order

The implementation follows 8 sequential steps. Each step must compile,
include backend + GUI where applicable, be committed, pushed, and
validated on a real server before the next step begins.

```
Step 1: CertificateProvider abstraction + interfaces
    ↓
Step 2: Storage + metadata
    ↓
Step 3: REST API
    ↓
Step 4: LetsEncryptProvider (real ACME HTTP-01)
    ↓
Step 5: Proxy integration
    ↓
Step 6: RenewalScheduler
    ↓
Step 7: GUI
    ↓
Step 8: Real server validation
```

### Step 1 — CertificateProvider abstraction
- Define `CertificateProvider` abstract interface
- Define `ChallengeProvider` abstract interface
- Define `HTTP01ChallengeProvider` concrete class (placeholder that
  logs, no real ACME yet)
- Define `CustomCertificateProvider` concrete class (manages files)
- Register providers in `ServiceRegistry` by string key
- Update `SslCertificate` with new fields (domains, challenge_type,
  last_validation, renew_attempts, version)
- Update `SslCertificateManager` with extended methods
- Unit tests for state transitions
- Commit and push

### Step 2 — Storage + metadata
- Implement `CertificateStore` class for disk I/O
- Read/write `fullchain.pem`, `privkey.pem`, `chain.pem`
- Read/write `metadata.json` with all fields (version, domains[],
  challenge_type, last_validation, renew_attempts, etc.)
- Read existing `ssl_certificates.db` on startup with backward compat
- Unit tests for file I/O and metadata parse/serialize
- Commit and push

### Step 3 — REST API (Complete)
- Implemented all `/api/ssl/` endpoints on `CertificateProvider` (abstract)
- `GET /api/ssl` — list all sites with SSL state (including HTTP_ONLY)
- `GET /api/ssl/<domain>` — certificate details
- `GET /api/ssl/<domain>/status` — quick status check
- `POST /api/ssl/<domain>/issue` — async certificate issuance via provider
- `POST /api/ssl/<domain>/renew` — async renewal via stored provider
- `POST /api/ssl/<domain>/enable` — enable HTTPS with state validation
- `POST /api/ssl/<domain>/disable` — disable HTTPS, keep files
- `POST /api/ssl/<domain>/redirect/enable` — enable redirect (requires HTTPS)
- `POST /api/ssl/<domain>/redirect/disable` — disable redirect
- `GET /api/ssl/providers` — list providers from ServiceRegistry
- Consistent JSON error format with code/message/details
- Issue/renew return job_id with async status
- Private key content/paths never exposed in responses
- Router extended with add_prefix() for domain-based path matching
- All route through `CertificateProvider`, never a concrete class

### Step 4 — AcmeClient + ACME engine layers (Complete)

Created reusable ACME protocol engine (AcmeClient) with layered architecture:

```
AcmeClient
  ├── discover_directory()
  ├── load_or_create_account()    ← P-256 key, JWT, ACME registration
  ├── create_order()              ← Order with domain identifiers
  ├── get_authorization()         ← Authorization with challenges
  ├── respond_to_challenge()      ← Signal readiness to ACME
  ├── poll_challenge()            ← Wait for validation result
  ├── poll_authorization()
  ├── poll_order()
  ├── finalize_order()            ← Submit CSR
  └── download_certificate()      ← Retrieve fullchain PEM
```

LetsEncryptProvider rewritten as adapter around AcmeClient:
- `issue_certificate()` orchestrates: preflight → account → order → authz
  → challenge (via ChallengeProvider) → finalize → download → store
- Preflight validation: reject localhost, .local, .test before ACME
- Staging mode via `set_staging(true)`
- HTTP-01 implementation deferred (uses ChallengeProvider abstraction)

All ACME protocol details are inside AcmeClient — LetsEncryptProvider
only orchestrates the lifecycle. ChallengeProvider handles transport.

### Step 5 — Proxy integration (Complete)
- `ProxyProvider::attach_certificate()` — generates HTTPS nginx config with
  `ssl_certificate` + `ssl_certificate_key` directives
- `ProxyProvider::detach_certificate()` — removes SSL config, reverts to HTTP
- Transactional config: write temp file, validate syntax, atomic rename, reload
- `POST /ssl/<domain>/enable` calls `attach_certificate()` via proxy
- `POST /ssl/<domain>/disable` calls `detach_certificate()` via proxy
- HTTPS config never breaks HTTP — HTTP block always present
- Certificate files kept on disk after disable
- No nginx-specific SSL logic outside `NginxProxyProvider`
- ProxyConfigBuilder helper extracts config generation from string building
  - build_http_block(), build_https_block(), build_redirect_block()
  - Param-based build() for combined HTTP+HTTPS+redirect
  - Future: HSTS, HTTP/2, HTTP/3, OCSP, mTLS
- JobExecutor replaces detached `std::thread` for all async work
  - Configurable worker pool (2), bounded queue (64)
  - Graceful shutdown with pending task cancellation

### Step 6 — RenewalScheduler (Complete)
- Implemented `RenewalScheduler` with 24-hour background thread
- Scans all certificates, renews due ones via JobExecutor
- Skips HTTP_ONLY, disabled, auto_renew=false, ERROR, unsupported providers
- Exponential backoff: 1h, 2h, 4h, 8h, 16h, 24h...
- After 7 consecutive failures, status → ERROR
- Updates metadata.json (renew_attempts, last_error)
- Structured log events for all operations
- Integrated with ServiceRegistry::start()/shutdown() lifecycle

### Step 7 — GUI
- Redesign SSL page: show ALL sites with state column
- State badges with colors (HTTP_ONLY gray, ACTIVE green, ERROR red,
  DISABLED gray, ISSUING blue)
- Context-dependent action buttons per state
- Per-domain detail view with expiration, days remaining, last error
- HTTPS toggle (enable/disable)
- Redirect toggle
- Auto-renew toggle
- Dashboard card with SSL metrics
- Site detail page SSL tab integration
- Error display (never blocks site)
- Commit and push

### Step 8 — Real server validation
- Build with zero compiler warnings
- All tests pass
- Deploy on real Debian 13 with a real domain
- Run full validation plan (see Validation Plan section)
- Fix issues, commit, push
- Return to Step 1 if architecture changes needed

## Migration Strategy

Existing `SslCertificate` resources with `provider == "placeholder"`
are treated as not issued. The Web UI shows "HTTP_ONLY" for these
sites. Users click "Issue Certificate" to get a real certificate.

No migration script needed. Old records remain valid and are displayed
on the SSL page with their original status.

## Backward Compatibility

- `SslCertificateManager` existing methods remain with same signatures.
- Old `ssl_certificates.db` entries are read on startup; new fields
  default to empty/false.
- CLI command names stay the same where possible; `ssl request` becomes
  `ssl issue` but old name is aliased.
- REST API endpoints are additive; old `/api/ssl/*` endpoints kept but
  deprecated.
- `LetsEncryptProvider` class name unchanged; method signatures unchanged.
- Existing sites continue working HTTP-only until user enables SSL.
- `SiteCreateOperation` unchanged — no new SSL dependency.

## Rejected Alternatives

1. **SSL required at site creation** — rejected because many sites are
   created before DNS propagates. SSL must be optional, not mandatory.

2. **Use certbot as subprocess** — rejected because it adds external
   dependency, ties the project to certbot's behavior, and complicates
   error handling. Built-in ACME gives full control.

3. **Store certificates in Docker volumes** — rejected because the
   proxy provider needs direct filesystem access. Disk storage at
   `/srv/containercp/ssl/` is simpler and accessible by all components.

4. **Single certificate for all domains (SAN)** — rejected because each
   site needs independent certificate lifecycle, renewal, and future
   wildcard support.

5. **Implement DNS-01 first** — rejected because HTTP-01 is simpler,
   faster, and works immediately with the existing central proxy on
   port 80. DNS-01 is a future provider extension.

6. **Merge SSL lifecycle into ProxyProvider** — rejected because it
   violates single responsibility. ProxyProvider generates nginx config.
   LetsEncryptProvider manages certificates. Clear separation.

7. **Auto-issue SSL on site creation** — rejected because it would
   break sites that don't have DNS configured yet. Manual issuance
   puts the user in control.

8. **Blocking ACME during API call** — rejected. Issue/renew return
   immediately with status ISSUING. The GUI polls for completion.

## Risks

| Risk | Mitigation |
|------|------------|
| ACME rate limits (50 certs/week, 5/duplicate/week) | Staging for development; production shows clear error on rate limit |
| Port 80 must be reachable from internet | Central proxy already binds port 80; documented requirement |
| ACME protocol complexity | Only HTTP-01 initially; simplest ACME flow (~500 lines) |
| libcurl dependency | Check at build time; optional with fallback to built-in HTTP |
| Private key permissions | `privkey.pem` written with mode 600, owned by containercpd |
| Renewal failure during long outage | Old cert kept until expiry; retried daily; site reverts to HTTP gracefully |
| Scheduler thread safety | Mutex around certificate state; scheduler is simple timer loop |
| User issues cert before DNS propagates | ACME will fail → ERROR state → site still works HTTP → user retries later |
| Two instances issue cert simultaneously | Lock file per domain in `/srv/containercp/ssl/<domain>/.issue.lock` |

## Validation Plan

1. Build with zero compiler warnings
2. All existing unit tests pass
3. New unit tests for:
   - SslCertificateManager extended methods
   - State transitions (valid and invalid)
   - metadata.json read/write
   - RenewalScheduler check logic
4. On real Debian 13 with a real domain:
   - Create site → HTTP_ONLY state, works on port 80
   - POST /ssl/<domain>/issue → certificate issued
   - Verify fullchain.pem, privkey.pem, chain.pem on disk
   - Verify metadata.json contents
   - Verify nginx config has SSL directives
   - `curl https://<domain>/` → TLS + HTTP 200
   - `curl http://<domain>/` → HTTP 200 (no redirect by default)
   - POST /ssl/<domain>/redirect/enable → HTTP redirects to HTTPS
   - POST /ssl/<domain>/disable → HTTPS disabled, HTTP works
   - POST /ssl/<domain>/enable → HTTPS restored
   - POST /ssl/<domain>/renew → new certificate issued
   - CLI `ssl list` shows all sites including HTTP_ONLY
   - CLI `ssl show <domain>` shows all metadata
   - Web UI SSL page shows all sites with correct states
   - Web UI issue/enable/disable/redirect buttons work
5. Failure scenarios:
   - Issue with unreachable domain → ERROR state, site still HTTP
   - Disable HTTPS → site still HTTP
   - Set renew_after to past → scheduler triggers renewal
   - Daemon restart preserves all certificate state
6. Multiple sites with independent SSL states
7. Zero orphan files or stale nginx configs

## Required files

### New files (created)
- `libs/ssl/CertificateProvider.h` — abstract interface with provider_id, provider_name, supports_auto_renew
- `libs/ssl/ChallengeProvider.h` — abstract ACME challenge interface with can_validate()
- `libs/ssl/HTTP01ChallengeProvider.h/.cpp` — HTTP-01 challenge implementation (placeholder, real in Step 4)
- `libs/ssl/LetsEncryptProvider.h/.cpp` — ACME certificate lifecycle (placeholder, real in Step 4)
- `libs/ssl/PemCertificateProvider.h/.cpp` — manages PEM certificate files, no auto-renew
- `libs/ssl/CertificateStore.h/.cpp` — provider-independent disk I/O for cert files + metadata.json
- `libs/ssl/RenewalScheduler.h/.cpp` — background renewal scheduler (Step 6)
- `libs/ssl/AcmeClient.h/.cpp` — ACME protocol HTTP-01 implementation (Step 4)
- `libs/ssl/SslCertificateView.h/.cpp` — view model combining site + ssl state (Step 7)

### Modified files
- `libs/ssl/CertificateProvider.h` — extended interface with provider metadata
- `libs/ssl/LetsEncryptProvider.h/.cpp` — real ACME implementation
- `libs/ssl/SslCertificate.h/.cpp` — new fields (issued_at, expires_at, renew_after, last_error, https_enabled, redirect_enabled)
- `libs/ssl/SslCertificateManager.h/.cpp` — extended methods
- `libs/proxy/ProxyProvider.h` — attach_certificate / detach_certificate
- `libs/proxy/NginxProxyProvider.h/.cpp` — HTTPS config + ACME challenge path
- `libs/api/ApiServer.cpp` — new SSL endpoints
- `libs/daemon/DaemonApp.cpp` — new CLI handlers
- `app/containercpd/main.cpp` — start RenewalScheduler
- `web/app.js` — SSL page, detail view, action buttons
- `web/index.html` — SSL page HTML updates
- `libs/core/ServiceRegistry.cpp` — wire up providers
- `libs/operations/SiteCreateOperation.cpp` — no change needed (SSL stays optional)
- `libs/operations/SiteRemoveOperation.cpp` — cleanup SSL files on site removal
- `docs/ADR/ADR-003-LetsEncrypt.md` — update with implementation details
- `CHANGELOG.md` — entry for this Epic

---
Back to [ARCH-005 index](ARCH-005-SSL-Management.md)
