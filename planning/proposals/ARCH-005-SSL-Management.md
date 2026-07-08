# ARCH-005: SSL/HTTPS Management

Status: Draft

## Problem

ContainerCP has no real SSL implementation. The existing
`LetsEncryptProvider` is a placeholder that logs actions and returns
success. Sites are served over HTTP only. There is no automatic
certificate issuance, renewal, or HTTPS enforcement. A hosting control
panel without HTTPS is not production-ready.

## Motivation

SSL/HTTPS is a core requirement for any hosting platform. Every website
deployed with ContainerCP must be served over HTTPS by default. The
Product Vision requires automatic SSL provisioning (see Principle 4:
Providers isolate external systems, and the v1.0 goal of "provision SSL"
from the Web UI).

This Epic closes the gap between the existing ADR-003 (Let's Encrypt
architecture decision) and a working implementation.

## Current Architecture

### Existing resources
- `SslCertificate` struct with fields: `domain_id`, `domain`, `provider`,
  `certificate_path`, `key_path`, `expires_at`, `status`, `auto_renew`,
  `enabled`
- `SslCertificateManager` with CRUD methods (create, remove, find,
  find_by_domain, list)
- Storage in `ssl_certificates.db` (pipe-delimited)

### Existing providers
- `CertificateProvider` interface: `request()` / `renew()` / `revoke()` / `status()`
- `LetsEncryptProvider` — **placeholder only**: every method logs and
  returns `{true, ""}`. No ACME calls, no file I/O, no certificate files.

### Existing integration
- CLI commands: `ssl list`, `ssl show`, `ssl request`, `ssl renew`,
  `ssl revoke`, `ssl enable`, `ssl disable` — all route through the
  placeholder provider
- REST API endpoints: `POST /api/ssl/enable`, `/api/ssl/disable`,
  `/api/ssl/remove`
- ProxyProvider has no SSL integration — no cert attachment, no HTTPS
  config generation

## Proposed Architecture

### Overview

A real ACME HTTP-01 implementation replaces the placeholder. The
architecture follows the established pattern:

```
LetsEncryptProvider (ACME HTTP-01)
    ↓
Certificate storage on disk (/srv/containercp/ssl/<domain>/)
    ↓
ProxyProvider reads certs and generates HTTPS nginx config
    ↓
nginx reload → site served over HTTPS
```

Each site owns exactly one certificate. Certificates are stored on
disk (not inside Docker containers). The provider interface is
designed to support future challenge types (DNS-01) and external
providers (Cloudflare, Route53, custom PEM import).

### ACME HTTP-01 flow

```
1. POST /ssl/<domain>/issue
2. LetsEncryptProvider creates account key (if first run)
3. Provider requests challenge from ACME server
4. Provider writes challenge token to /srv/containercp/ssl/<domain>/.well-known/acme-challenge/
5. Provider serves challenge via HTTP (port 80 through proxy)
6. ACME server validates → certificate issued
7. Provider downloads fullchain.pem, privkey.pem, chain.pem
8. Provider writes metadata.json
9. Provider calls ProxyProvider::attach_certificate(domain, cert_path, key_path)
10. ProxyProvider reloads nginx → HTTPS enabled
```

### Auto-renewal

- A `RenewalScheduler` runs as a background thread in the daemon
- Checks all certificates daily
- Renews any certificate where `renew_after` date has passed
- Renewal follows the same HTTP-01 flow as initial issue
- Failed renewals set `last_error` in metadata and keep the old
  certificate until expiry

## New Resources

None. The existing `SslCertificate` resource is extended with new
status values:

- `status` values extended:
  - `"pending"` — not yet issued
  - `"requesting"` — ACME challenge in progress
  - `"active"` — certificate issued and valid
  - `"expiring"` — within 30 days of expiry
  - `"expired"` — past expiry date
  - `"failed"` — last ACME operation failed
  - `"renewing"` — renewal in progress
  - `"disabled"` — HTTPS disabled by user

- New fields added to `SslCertificate`:
  - `issued_at` — ISO-8601 date of issuance
  - `renew_after` — ISO-8601 date when renewal should trigger
  - `last_error` — last ACME error message (empty if none)

## Managers

### SslCertificateManager (extended)

Existing methods remain. New methods:

| Method | Description |
|--------|-------------|
| `update_status(id, status)` | Update certificate status |
| `find_expiring_before(date)` | Find certificates expiring before date |
| `find_due_for_renewal()` | Find certificates where `renew_after <= now` |
| `set_metadata(id, issued_at, expires_at, renew_after)` | Update dates after issue/renew |
| `set_error(id, error_msg)` | Set last error and mark status failed |

### RenewalScheduler (new)

| Method | Description |
|--------|-------------|
| `start()` | Start background thread |
| `stop()` | Stop background thread |
| `check()` | Scan all certs, renew due ones |

## Storage

### Database: `ssl_certificates.db`

Extended pipe-delimited format:

```
id|domain_id|domain|provider|certificate_path|key_path|chain_path|issued_at|expires_at|renew_after|status|auto_renew|enabled|last_error
```

### Disk storage: `/srv/containercp/ssl/<domain>/`

```
/srv/containercp/ssl/
└── <domain>/
    ├── fullchain.pem    — full certificate chain
    ├── privkey.pem      — private key
    ├── chain.pem        — CA chain only
    └── metadata.json    — machine-readable metadata
```

### metadata.json format

```json
{
    "domain": "example.com",
    "provider": "letsencrypt",
    "issued_at": "2025-07-08T12:00:00Z",
    "expires_at": "2025-10-06T12:00:00Z",
    "renew_after": "2025-09-06T12:00:00Z",
    "status": "active",
    "last_error": "",
    "auto_renew": true
}
```

## Providers

### CertificateProvider (extended interface)

```cpp
class CertificateProvider {
    virtual ~CertificateProvider() = default;

    virtual OperationResult request(const std::string& domain) = 0;
    virtual OperationResult renew(const std::string& domain) = 0;
    virtual OperationResult revoke(const std::string& domain) = 0;
    virtual OperationResult status(const std::string& domain) = 0;

    // New for future providers
    virtual bool supports_dns_challenge() const { return false; }
    virtual OperationResult request_dns(const std::string& domain) { return {false, "not supported"}; }
};
```

### LetsEncryptProvider (real ACME HTTP-01 implementation)

- ACME directory URL: `https://acme-v02.api.letsencrypt.org/directory`
- Staging directory: `https://acme-staging-v02.api.letsencrypt.org/directory`
  (configurable via `LETSENCRYPT_STAGING=1` env var)
- Account key stored at `/srv/containercp/ssl/account.pem`
- HTTP-01 challenge response served via a temporary well-known directory
  mapped into the central nginx proxy
- Certificate files written to `/srv/containercp/ssl/<domain>/`
- No external dependencies — ACME protocol implemented using
  libcurl (or built-in HTTP client if curl is not available)

Implementation approach:
- Use libcurl for ACME HTTP calls (JSON over HTTPS)
- JWT signed with ES256 (ECDSA P-256) for ACME authentication
- Parse JSON responses using a minimal JSON parser (no external dep)
- Retry logic: 3 attempts with exponential backoff on ACME errors

### ProxyProvider integration

The `ProxyProvider` interface gets two new methods:

```cpp
class ProxyProvider {
    virtual OperationResult attach_certificate(const std::string& domain,
                                                const std::string& cert_path,
                                                const std::string& key_path) = 0;
    virtual OperationResult detach_certificate(const std::string& domain) = 0;
};
```

- `attach_certificate()` generates HTTPS server block in nginx config
  with `ssl_certificate` and `ssl_certificate_key` directives,
  adds HTTP → HTTPS redirect
- `detach_certificate()` removes HTTPS block, reverts to HTTP-only
- `reload()` is called after each operation

### Future provider compatibility

The `CertificateProvider` interface is designed for:

- **DNS-01 challenge**: Add `request_dns()` method. Provider creates
  DNS TXT record, ACME validates. No HTTP path needed.
- **Cloudflare**: Implement `CertificateProvider` using Cloudflare API
  for DNS-01 automation
- **Route53**: Implement `CertificateProvider` using AWS Route53 API
- **Custom certificates**: A `CustomCertificateProvider` reads PEM files
  from a user-specified path and manages them as `SslCertificate` resources
  without ACME interaction
- **Import PEM**: A one-time import operation reads existing fullchain.pem
  + privkey.pem and creates an `SslCertificate` resource with
  `provider == "custom"`

## REST API

### Design principles

- RESTful resource paths under `/ssl/`
- Consistent JSON envelope `{"success": true, "data": ...}`
- All endpoints return meaningful error messages

### Endpoints

```
GET    /ssl
       → list all certificates
       → {"success":true, "data": [{"domain": "...", "status": "...", ...}]}

GET    /ssl/<domain>
       → certificate details
       → {"success":true, "data": {"domain": "...", "expires_at": "...", "days_remaining": 90, ...}}

POST   /ssl/<domain>/issue
       → issue new certificate via ACME HTTP-01
       → {"success":true, "data": {"domain": "...", "status": "active", ...}}
       → on failure: {"success":false, "error": "ACME challenge failed: ..."}

POST   /ssl/<domain>/renew
       → force renew certificate
       → same response as issue

POST   /ssl/<domain>/enable
       → enable HTTPS (attach cert to proxy config)
       → {"success":true, "data": {"domain": "...", "https": true}}

POST   /ssl/<domain>/disable
       → disable HTTPS (detach cert from proxy config)
       → {"success":true, "data": {"domain": "...", "https": false}}

GET    /ssl/<domain>/status
       → current certificate status
       → {"success":true, "data": {"status": "active", "days_remaining": 90, "last_error": ""}}
```

### Error responses

All endpoints return standard error envelope:

```json
{
    "success": false,
    "error": "Domain not found"
}
```

HTTP status codes:
- 200 — success
- 400 — validation error (invalid domain, already issued)
- 404 — domain not found
- 409 — conflict (certificate already active)
- 500 — ACME server error or internal error

## Web UI

### SSL page redesign

The existing SSL table page is redesigned with:

| Column | Content |
|--------|---------|
| Domain | Clickable link to detail |
| Status | Badge (active=green, expiring=yellow, expired=red, disabled=gray, failed=red) |
| Issuer | "Let's Encrypt" or "Custom" |
| Expiration | Date + days remaining counter |
| HTTPS | Enabled/Disabled toggle |
| Auto Renew | On/Off badge |

### Per-certificate detail view

A detail view shows:

- Domain name
- Certificate status with color badge
- Issuer
- Issued date
- Expiration date
- Days remaining (with color: green >30, yellow 7-30, red <7)
- Last error message (if failed)
- Auto Renew toggle

### Action buttons

| Button | Condition | Action |
|--------|-----------|--------|
| Issue Certificate | Status is empty/pending | Calls POST /ssl/<domain>/issue |
| Renew | Status is active/expiring | Calls POST /ssl/<domain>/renew |
| Enable HTTPS | HTTPS is disabled | Calls POST /ssl/<domain>/enable |
| Disable HTTPS | HTTPS is enabled | Calls POST /ssl/<domain>/disable |
| View Details | Always | Shows detail panel/modal |

### Integration with Site detail page

The Site detail page SSL tab shows the certificate card with status,
expiration, and quick actions (Issue/Renew/Enable/Disable).

### Dashboard card

A new dashboard card "SSL Certificates" shows:
- Total certificates
- Active count
- Expiring within 30 days (with warning color)
- Expired count (with alert color)

## CLI

### New and modified commands

```
ssl list                    → list all certificates (color-coded status)
ssl show <domain>           → show certificate details + expiration
ssl issue <domain>          → issue new certificate
ssl renew <domain>          → force renew
ssl enable <domain>         → enable HTTPS
ssl disable <domain>        → disable HTTPS
ssl status <domain>         → quick status check
```

### Output format

```
$ containercp ssl show example.com
Domain:        example.com
Status:        active
Issuer:        Let's Encrypt
Issued:        2025-07-08
Expires:       2025-10-06 (90 days)
Auto Renew:    yes
HTTPS:         enabled
```

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `LETSENCRYPT_STAGING` | (unset) | When set, use ACME staging directory |
| `CONTAINERCP_SSL_DIR` | `/srv/containercp/ssl` | Certificate storage root |
| `CONTAINERCP_ACME_EMAIL` | (unset) | Email for Let's Encrypt account (optional) |

## Migration Strategy

Existing `SslCertificate` resources with `provider == "placeholder"`
and `status == "placeholder"` are treated as "not issued". The Web UI
shows "Issue Certificate" button for these. No migration script needed
— old records remain valid, and users issue real certificates when ready.

## Backward Compatibility

- `SslCertificateManager` existing methods remain unchanged
- Old `ssl_certificates.db` format is read on startup; new fields
  default to empty strings if not present
- CLI commands `ssl request` / `ssl renew` / `ssl revoke` keep the
  same names but now call real ACME logic instead of placeholder
- REST API endpoints remain; new endpoints are additive
- `LetsEncryptProvider` class name stays the same; method signatures
  unchanged

## Rejected Alternatives

1. **Use certbot as a subprocess** — rejected because it adds an
   external dependency, ties us to certbot's behavior, and complicates
   error handling. Built-in ACME gives full control.

2. **Use a C++ ACME library** — rejected because there is no mature,
   maintained C++ ACME library. Implementing the protocol directly
   is ~500 lines and gives full control.

3. **Store certificates inside Docker volumes** — rejected because
   the Product Vision requires configuration editable without
   recompilation, and certs must be accessible to the proxy provider
   directly. Disk storage at `/srv/containercp/ssl/` is the simplest
   shared location.

4. **Single certificate for all domains (SAN)** — rejected because
   each site should own its certificate for isolation, independent
   renewal, and future wildcard support per domain.

5. **Implement DNS-01 first** — rejected because HTTP-01 is simpler
   and works immediately. DNS-01 is added as a future provider.

## Risks

| Risk | Mitigation |
|------|------------|
| ACME rate limits (50 certs/week) | Use staging for development; production respects limits |
| Port 80 must be reachable | Central proxy already binds port 80; HTTP-01 challenge served through it |
| AC ME protocol complexity | Only HTTP-01 in initial implementation; simplest ACME flow |
| libcurl dependency | Check availability at build time; fall back to built-in HTTP client if absent |
| Certificate file permissions | Private keys must be readable by proxy but not world-readable |
| Renewal failure | Keep old cert until expiry; log error; retry next day |
| Background scheduler thread safety | Use mutex around certificate state; scheduler runs on timer, not continuous |

## Validation Plan

1. Build with zero compiler warnings
2. All existing tests pass
3. Run on real Debian 13 with a real domain pointing to the server
4. POST /ssl/<domain>/issue → certificate issued and stored on disk
5. Verify fullchain.pem, privkey.pem, chain.pem exist at
   `/srv/containercp/ssl/<domain>/`
6. Verify nginx config includes ssl_certificate directives
7. `curl https://<domain>/` returns valid TLS + HTTP 200
8. `curl http://<domain>/` redirects to HTTPS
9. POST /ssl/<domain>/disable → HTTP stops redirecting
10. POST /ssl/<domain>/enable → HTTPS restored
11. POST /ssl/<domain>/renew → new certificate issued
12. POST /ssl/<domain>/status → returns correct days_remaining
13. CLI `ssl list` shows certificate
14. CLI `ssl show <domain>` shows all metadata
15. Web UI SSL page shows certificate with correct status and actions
16. Auto-renewal: set renew_after to past → scheduler triggers renewal
17. Multiple sites each get their own certificate
18. Daemon restart preserves all certificate state

## Required files

### New files
- `libs/ssl/AcmeClient.h/.cpp` — ACME protocol implementation (HTTP-01)
- `libs/ssl/RenewalScheduler.h/.cpp` — background renewal scheduler
- `libs/ssl/CertificateStore.h/.cpp` — disk I/O for cert files + metadata

### Modified files
- `libs/ssl/CertificateProvider.h` — extended interface (supports_dns_challenge)
- `libs/ssl/LetsEncryptProvider.h/.cpp` — real ACME implementation
- `libs/ssl/SslCertificate.h/.cpp` — new fields (issued_at, renew_after, last_error)
- `libs/ssl/SslCertificateManager.h/.cpp` — new methods (update_status, find_due_for_renewal, etc.)
- `libs/proxy/ProxyProvider.h` — attach_certificate / detach_certificate
- `libs/proxy/NginxProxyProvider.h/.cpp` — HTTPS config generation
- `libs/api/ApiServer.cpp` — new SSL endpoints
- `libs/daemon/DaemonApp.cpp` — new CLI handlers
- `app/containercpd/main.cpp` — start RenewalScheduler
- `web/app.js` — SSL page, detail view, action buttons
- `web/index.html` — SSL page HTML
- `libs/core/ServiceRegistry.cpp` — wire up providers
- `docs/ADR/ADR-003-LetsEncrypt.md` — update with implementation details
- `CHANGELOG.md` — entry for this Epic
