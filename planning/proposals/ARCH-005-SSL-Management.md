# ARCH-005: SSL/HTTPS Management

Status: Draft

## Problem

ContainerCP has no real SSL implementation. The existing
`LetsEncryptProvider` is a placeholder that logs actions and returns
success. Sites are served over HTTP only. There is no automatic
certificate issuance, renewal, or HTTPS enforcement.

A hosting control panel without HTTPS is not production-ready. However,
SSL must remain optional — sites must work HTTP-only by default, and
SSL errors must never break a running site.

## Motivation

SSL/HTTPS is a core requirement for any hosting platform. The Product
Vision requires automatic SSL provisioning (see ADR-003).

Critical product requirement: SSL is an **optional capability** attached
to an existing site. Many sites are created before DNS is pointed
correctly or before Let's Encrypt validation can succeed. The platform
must not require SSL for site existence.

This Epic replaces the placeholder `LetsEncryptProvider` with a real
ACME HTTP-01 implementation, while keeping SSL strictly optional per
site.

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
- Site creation does not create any SSL resource

## Certificate States

Every site has exactly one SSL state. States are independent of site
existence — a site always works over HTTP regardless of SSL state.

```
                        ┌──────────────┐
          ┌────────────▶│  HTTP_ONLY   │◀────────────────────┐
          │             │ (default)    │                      │
          │             └──────┬───────┘                      │
          │                    │                              │
          │           POST /ssl/<d>/issue                     │
          │                    │                              │
          │                    ▼                              │
          │             ┌──────────────┐                      │
          │             │   ISSUING    │                      │
          │             │  (ACME req)  │                      │
          │             └──────┬───────┘                      │
          │                    │                              │
          │           ┌───────┴────────┐                     │
          │           ▼                ▼                      │
          │    ┌──────────┐    ┌───────────┐                  │
          │    │  ACTIVE  │    │   ERROR   │                  │
          │    │ (HTTPS)  │    │ (HTTP OK) │                  │
          │    └────┬─────┘    └─────┬─────┘                  │
          │         │               │                        │
          │  POST disable     POST issue (retry)              │
          │         │               │                        │
          │         └───────┬───────┘                        │
          │                 │                                │
          │                 ▼                                │
          │          ┌──────────────┐                        │
          └──────────│  DISABLED   │─────────────────────────┘
                     │ (HTTP only) │
                     └──────────────┘
```

### State definitions

| State | Description | HTTPS available? | HTTP works? |
|-------|-------------|-----------------|-------------|
| `HTTP_ONLY` | Default. No certificate requested. | No | Yes |
| `ISSUING` | ACME challenge in progress. | No (HTTP only until complete) | Yes |
| `ACTIVE` | Certificate valid, HTTPS enabled (optional redirect). | Yes | Yes (redirect optional) |
| `ERROR` | Issuance or renewal failed. | No (falls back to HTTP) | Yes |
| `DISABLED` | HTTPS disabled by user. Cert may exist on disk. | No | Yes |

### State transition rules

1. **HTTP_ONLY → ISSUING**: User clicks "Issue Certificate". `POST /ssl/<d>/issue`.
2. **ISSUING → ACTIVE**: ACME succeeds. Certificate written to disk. Proxy config updated.
3. **ISSUING → ERROR**: ACME fails. `last_error` set. Site stays HTTP-only. User can retry.
4. **ACTIVE → DISABLED**: User clicks "Disable HTTPS". `POST /ssl/<d>/disable`.
   Certificate kept on disk. Proxy config reverts to HTTP-only.
5. **DISABLED → HTTP_ONLY**: Certificate removed from disk (optional cleanup).
   No proxy changes needed (already HTTP-only).
6. **ERROR → ISSUING**: User retries after fixing the issue (e.g., DNS propagated).
7. **ACTIVE → ERROR**: Renewal failed. Old certificate kept until expiry.
   HTTPS continues working until expiry, then reverts to HTTP-only.
8. **ACTIVE → ACTIVE**: Renewal succeeds. Certificate files replaced. Proxy reloaded.

### Critical guarantees

- **No state transition ever breaks HTTP access.**
- **ACTIVE state never transitions directly to HTTP_ONLY** — only to DISABLED or ERROR.
- **ERROR state still serves HTTP** — the site is never inaccessible.
- **ISSUING state still serves HTTP** — ACME challenge does not block traffic.

## Proposed Architecture

### Overview

SSL lifecycle lives entirely in `LetsEncryptProvider`. The proxy
provider only attaches existing certificates to nginx config and
reloads.

```
┌─────────────────────────────────────────────────────┐
│                 LetsEncryptProvider                  │
│  ┌────────────┐  ┌──────────────┐  ┌─────────────┐ │
│  │ AcmeClient │  │ CertStore    │  │Renewal      │ │
│  │ (HTTP-01)  │  │ (disk I/O)   │  │Scheduler    │ │
│  └────────────┘  └──────────────┘  └─────────────┘ │
│         │               │               │           │
│         ▼               ▼               ▼           │
│  ACME API          /srv/containercp/   timer       │
│  (libcurl)         ssl/<domain>/       check       │
└─────────────────────────────────────────────────────┘
         │
         │ attach_certificate / detach_certificate
         ▼
┌─────────────────────────────────────────────────────┐
│              NginxProxyProvider                      │
│  - generate HTTPS server block                       │
│  - add ssl_certificate / ssl_certificate_key         │
│  - add HTTP→HTTPS redirect (optional)                │
│  - reload nginx                                      │
└─────────────────────────────────────────────────────┘
```

### Site creation behavior

When a site is created:
1. Site is created as HTTP-only (no SSL resource created).
2. No ACME call, no certificate, no HTTPS config.
3. SSL resource is created lazily only when user requests certificate issuance.

This means:
- `SiteCreateOperation` does NOT touch SSL at all.
- The SSL page in Web UI shows HTTP_ONLY for every site that has no certificate.
- No delayed creation, no background issuance.

### ACME HTTP-01 flow (Issue)

```
1. POST /ssl/<domain>/issue
2. SslCertificate resource created (status: ISSUING)
3. LetsEncryptProvider creates account key (if first run, stored at
   /srv/containercp/ssl/account.pem)
4. Provider requests new order + authorization from ACME server
5. ACME returns HTTP-01 challenge token + value
6. Provider writes challenge file to
   /srv/containercp/ssl/<domain>/.well-known/acme-challenge/<token>
7. Central nginx serves /.well-known/acme-challenge/ from
   /srv/containercp/ssl/<domain>/.well-known/acme-challenge/
8. ACME server validates → certificate issued
9. Provider downloads fullchain.pem, privkey.pem, chain.pem
10. Provider writes metadata.json with status: active
11. Provider calls ProxyProvider::attach_certificate(domain, cert_path, key_path)
12. ProxyProvider generates HTTPS server block, reloads nginx
13. SslCertificate status updated to ACTIVE
```

On failure at any step:
- SslCertificate status set to ERROR
- `last_error` populated with English error message
- Site continues serving HTTP normally
- Challenge files cleaned up

### Auto-renewal

- A `RenewalScheduler` runs as a background thread inside the daemon.
- Checks all ACTIVE certificates daily (configurable interval).
- Renews any certificate where current date >= `renew_after`.
- Renewal follows the same HTTP-01 flow as initial issue.
- On renewal success: certificate files replaced, proxy reloaded.
- On renewal failure:
  - Old certificate kept on disk (still valid).
  - `last_error` set in metadata.
  - Status remains ACTIVE but `renewal_failed` flag set.
  - Retried next day.
  - If certificate expires before renewal succeeds, status → ERROR,
    HTTPS disabled, site reverts to HTTP-only.

### HTTP → HTTPS redirect

- Controlled by a per-certificate `redirect_enabled` flag.
- When enabled: nginx config includes `return 301 https://$host$request_uri;`.
- When disabled: both HTTP and HTTPS serve content.
- Default: redirect disabled after certificate issuance.
- User can toggle via `POST /ssl/<domain>/redirect/enable` and
  `/ssl/<domain>/redirect/disable`.

## New Resources

No new resource types. The existing `SslCertificate` resource is
extended:

### Extended fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `issued_at` | string (ISO-8601) | `""` | Date of certificate issuance |
| `expires_at` | string (ISO-8601) | `""` | Certificate expiration date |
| `renew_after` | string (ISO-8601) | `""` | Date when renewal should trigger |
| `last_error` | string | `""` | Last ACME error message |
| `https_enabled` | bool | `false` | Whether HTTPS is currently active |
| `redirect_enabled` | bool | `false` | Whether HTTP→HTTPS redirect is active |

### Status values

| Status | Meaning |
|--------|---------|
| `http_only` | No certificate, no SSL resource exists. Implicit state. |
| `issuing` | ACME challenge in progress |
| `active` | Certificate issued, HTTPS enabled |
| `error` | Last ACME operation failed |
| `disabled` | HTTPS disabled by user, certificate may or may not exist |

Note: `HTTP_ONLY` is an **implicit state** — no SslCertificate record
exists for the site. The REST API and GUI derive this state by absence.

## Managers

### SslCertificateManager (extended)

Existing methods remain unchanged. New methods:

| Method | Description |
|--------|-------------|
| `find_by_domain(domain)` | Find cert by domain (already exists) |
| `update_status(id, status)` | Update certificate status |
| `update_https(id, enabled, redirect)` | Update HTTPS + redirect flags |
| `set_metadata(id, issued_at, expires_at, renew_after)` | Set dates after issue/renew |
| `set_error(id, error_msg)` | Set last error and set status to ERROR |
| `find_due_for_renewal()` | Find ACTIVE certs where renew_after <= now |
| `find_expiring_before(date)` | Find ACTIVE certs expiring before date |
| `find_all_with_status()` | Return all certs grouped by status |
| `get_or_create(domain_id, domain)` | Return existing cert or create HTTP_ONLY placeholder |

### RenewalScheduler (new)

| Method | Description |
|--------|-------------|
| `start()` | Start background thread |
| `stop()` | Stop background thread |
| `check()` | Scan all certs, renew due ones |

The scheduler runs inside the daemon process. It uses a simple
timer (e.g., check every 6 hours). No external cron dependency.

## Storage

### Database: `ssl_certificates.db`

Extended pipe-delimited format:

```
id|domain_id|domain|provider|certificate_path|key_path|chain_path|issued_at|expires_at|renew_after|status|auto_renew|https_enabled|redirect_enabled|last_error
```

### Disk storage: `/srv/containercp/ssl/<domain>/`

```
/srv/containercp/ssl/
├── account.pem                        — ACME account private key
└── <domain>/
    ├── fullchain.pem                  — full certificate chain
    ├── privkey.pem                    — private key (mode 600)
    ├── chain.pem                      — CA chain only
    └── metadata.json                  — machine-readable metadata
```

### metadata.json format

```json
{
    "domain": "example.com",
    "provider": "letsencrypt",
    "status": "active",
    "issued_at": "2025-07-08T12:00:00Z",
    "expires_at": "2025-10-06T12:00:00Z",
    "renew_after": "2025-09-06T12:00:00Z",
    "auto_renew": true,
    "https_enabled": true,
    "redirect_enabled": false,
    "last_error": ""
}
```

## Providers

### CertificateProvider (extended interface)

```cpp
class CertificateProvider {
public:
    virtual ~CertificateProvider() = default;

    // Core ACME operations
    virtual OperationResult request(const std::string& domain) = 0;
    virtual OperationResult renew(const std::string& domain) = 0;
    virtual OperationResult revoke(const std::string& domain) = 0;
    virtual OperationResult status(const std::string& domain) = 0;

    // Future provider capabilities
    virtual std::string provider_name() const = 0;
    virtual bool supports_dns_challenge() const { return false; }
    virtual OperationResult request_dns(const std::string& domain) {
        return {false, "DNS challenge not supported by this provider"};
    }

    // Certificate file paths (populated after successful request/renew)
    virtual std::string certificate_path(const std::string& domain) const = 0;
    virtual std::string key_path(const std::string& domain) const = 0;
    virtual std::string chain_path(const std::string& domain) const = 0;
};
```

### LetsEncryptProvider (real ACME HTTP-01)

- ACME directory URL: `https://acme-v02.api.letsencrypt.org/directory`
- Staging: `https://acme-staging-v02.api.letsencrypt.org/directory`
  (enable via `LETSENCRYPT_STAGING=1` env var for testing)
- Account key stored at `/srv/containercp/ssl/account.pem`
- EC private key generated on first use (P-256 for ACME JWT)
- HTTP-01 challenge:
  - Token + key authorization written to `/.well-known/acme-challenge/` directory
  - Central nginx proxy serves this path from disk
  - Challenge directory cleaned up after validation (success or failure)
- Certificate files written to `/srv/containercp/ssl/<domain>/`
- Uses libcurl for ACME HTTPS calls
- JWT signed with ES256 (ECDSA P-256) using OpenSSL
- Minimal JSON parser (built-in, no external dependency)
- Retry: 3 attempts with exponential backoff (1s, 3s, 9s)

### ProxyProvider interface (extended)

Two new methods, and only these two. No certificate lifecycle logic:

```cpp
class ProxyProvider {
    virtual OperationResult attach_certificate(const std::string& domain,
                                                const std::string& cert_path,
                                                const std::string& key_path) = 0;
    virtual OperationResult detach_certificate(const std::string& domain) = 0;
};
```

**`attach_certificate()`** behavior:
- Reads existing nginx config for the domain
- Adds `listen 443 ssl;` server block
- Adds `ssl_certificate` and `ssl_certificate_key` directives
- If `redirect_enabled` flag is set, adds `return 301 https://$host$request_uri;`
  to the HTTP server block
- Reloads nginx

**`detach_certificate()`** behavior:
- Removes SSL directives from nginx config
- Removes `listen 443 ssl;` block
- Removes HTTP→HTTPS redirect if present
- Reloads nginx
- Does NOT delete certificate files from disk

### Future provider compatibility

The `CertificateProvider` interface supports:

- **DNS-01 challenge**: Implement `request_dns()`. Provider creates DNS
  TXT record via API, ACME validates. No port 80 needed.
- **Cloudflare**: `CertificateProvider` subclass using Cloudflare API for
  DNS-01. Provider name: `cloudflare`.
- **Route53**: `CertificateProvider` subclass using AWS Route53 API for
  DNS-01. Provider name: `route53`.
- **Custom certificates**: `CustomCertificateProvider` manages existing
  PEM files. Provider name: `custom`. User provides paths to
  fullchain.pem and privkey.pem. No ACME interaction.
- **Import PEM**: One-time operation. Reads existing fullchain.pem +
  privkey.pem, copies to `/srv/containercp/ssl/<domain>/`, creates
  SslCertificate resource with `provider == "custom"`.

Each provider implements `supports_dns_challenge()` to advertise its
capabilities. The REST API returns this info so the GUI can show/hide
the "Use DNS challenge" option per provider.

## REST API

### Design principles

- All endpoints under `/ssl/`.
- Consistent JSON envelope `{"success": true, "data": ...}`.
- Sites with no SSL resource return `HTTP_ONLY` state.
- SSL never blocks site operations.

### Endpoints

```
GET    /ssl
       → list SSL state for ALL sites (including HTTP_ONLY)
       → {"success":true, "data": [
           {"domain": "site1.local", "status": "active", "expires_at": "...", ...},
           {"domain": "site2.local", "status": "http_only", ...},
           {"domain": "site3.local", "status": "error", "last_error": "...", ...}
         ]}

GET    /ssl/<domain>
       → SSL details for one domain
       → {"success":true, "data": {
           "domain": "example.com",
           "provider": "letsencrypt",
           "status": "active",
           "issued_at": "2025-07-08T12:00:00Z",
           "expires_at": "2025-10-06T12:00:00Z",
           "days_remaining": 90,
           "auto_renew": true,
           "https_enabled": true,
           "redirect_enabled": false,
           "last_error": ""
         }}

POST   /ssl/<domain>/issue
       → issue new certificate via ACME HTTP-01
       → site stays HTTP during issuance
       → {"success":true, "data": {"domain": "...", "status": "issuing"}}
       → on failure: {"success":false, "error": "ACME challenge failed: ...",
          "data": {"domain": "...", "status": "error", "last_error": "..."}}

POST   /ssl/<domain>/renew
       → force renew existing certificate
       → old cert kept until new one is issued
       → same response format as /issue

POST   /ssl/<domain>/enable
       → enable HTTPS for a domain that has a valid certificate
       → attaches cert to proxy, reloads nginx
       → {"success":true, "data": {"domain": "...", "https_enabled": true}}
       → on failure: {"success":false, "error": "No valid certificate for domain"}

POST   /ssl/<domain>/disable
       → disable HTTPS, revert to HTTP-only
       → certificate files kept on disk
       → {"success":true, "data": {"domain": "...", "https_enabled": false}}

POST   /ssl/<domain>/redirect/enable
       → enable HTTP → HTTPS redirect
       → only valid when HTTPS is enabled
       → {"success":true, "data": {"redirect_enabled": true}}

POST   /ssl/<domain>/redirect/disable
       → disable HTTP → HTTPS redirect
       → both HTTP and HTTPS serve content
       → {"success":true, "data": {"redirect_enabled": false}}

GET    /ssl/<domain>/status
       → quick status check
       → {"success":true, "data": {
           "status": "active",
           "https_enabled": true,
           "days_remaining": 90,
           "last_error": ""
         }}

GET    /ssl/providers
       → list available certificate providers
       → {"success":true, "data": [
           {"name": "letsencrypt", "supports_dns": false, "description": "Let's Encrypt ACME HTTP-01"},
           {"name": "custom", "supports_dns": false, "description": "Custom PEM certificates"}
         ]}
```

### HTTP status codes

| Code | Meaning |
|------|---------|
| 200 | Success |
| 400 | Validation error (invalid domain, bad state transition) |
| 404 | Domain not found |
| 409 | Conflict (already issuing, wrong state) |
| 500 | ACME server error or internal error |

### Error responses (consistent)

```json
{
    "success": false,
    "error": "Human-readable error message",
    "data": {
        "domain": "example.com",
        "status": "error",
        "last_error": "ACME challenge failed: could not connect to ..."
    }
}
```

## Web UI

### Design principle

**Every site is visible on the SSL page, regardless of SSL state.**
HTTP-only sites are shown with clear state indication.

### SSL page (table view)

The SSL page lists every site on the system, not just sites with
certificates.

| Column | Content |
|--------|---------|
| Domain | Clickable link to detail |
| State | Badge with color: HTTP_ONLY (gray), ISSUING (blue), ACTIVE (green), ERROR (red), DISABLED (gray) |
| HTTPS | On/Off badge or "N/A" for HTTP_ONLY |
| Expiration | Date + days remaining, or "—" for HTTP_ONLY/ERROR/DISABLED |
| Actions | Context-dependent buttons |

### State-dependent actions

| State | Available actions |
|-------|------------------|
| `HTTP_ONLY` | [Issue Certificate] |
| `ISSUING` | (spinner, no actions) |
| `ACTIVE` | [Disable HTTPS] [Renew] [Toggle Redirect] [View Details] |
| `ERROR` | [Retry Issue] [View Error] |
| `DISABLED` | [Enable HTTPS] [Issue New Certificate] |

### Per-domain detail view

Opens when clicking a domain name. Shows:

- Domain name
- State badge (color-coded)
- Provider name
- Issued date (or "—")
- Expiration date (or "—")
- Days remaining with color coding:
  - Green: >30 days
  - Yellow: 7–30 days
  - Red: <7 days or expired
- HTTPS toggle (Enable/Disable)
- HTTP→HTTPS redirect toggle (only when HTTPS is enabled)
- Auto Renew toggle
- Last error message (if any), with red background
- Certificate file paths (for debugging)

### Site detail page integration

The Site detail page already has an SSL tab. It shows the same
state-dependent card as the SSL detail view, with the same actions.

### Dashboard card

New dashboard card "SSL":

| Metric | Color |
|--------|-------|
| Total sites | default |
| HTTPS active | green |
| Expiring within 30 days | yellow |
| Errors | red |

### Failure display

SSL errors are displayed prominently:
- Red badge in the SSL table row
- Red error box in the detail view with the exact error message
- Dashboard shows error count
- Errors never block site operations — the site continues serving HTTP

## CLI

### Commands

```
ssl list
    → list SSL state for ALL sites
    → color-coded status

ssl show <domain>
    → show SSL details for one domain
    → includes all metadata, days remaining, error messages

ssl issue <domain>
    → issue certificate for domain
    → async: returns immediately, shows "ISSUING" state
    → use `ssl show` to check result

ssl renew <domain>
    → force renew certificate
    → keeps old cert until new one is issued

ssl enable <domain>
    → enable HTTPS

ssl disable <domain>
    → disable HTTPS, keep certificate files

ssl redirect enable <domain>
    → enable HTTP→HTTPS redirect

ssl redirect disable <domain>
    → disable HTTP→HTTPS redirect

ssl status <domain>
    → quick status check
```

### Output format

```
$ containercp ssl show example.com
Domain:        example.com
State:         ACTIVE
Provider:      Let's Encrypt
Issued:        2025-07-08
Expires:       2025-10-06 (90 days)
Auto Renew:    yes
HTTPS:         enabled
Redirect:      disabled
Last Error:    (none)
```

```
$ containercp ssl list
Domain                    State       HTTPS   Expires
────────────────────────────────────────────────────────
example.com               ACTIVE      on      2025-10-06 (90d)
test.local                HTTP_ONLY   —       —
broken.local              ERROR       off     ACME challenge failed: ...
```

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

## Configuration

| Variable | Default | Description |
|----------|---------|-------------|
| `CONTAINERCP_SSL_DIR` | `/srv/containercp/ssl` | Certificate storage root |
| `CONTAINERCP_ACME_EMAIL` | (unset) | Email for Let's Encrypt account registration |
| `LETSENCRYPT_STAGING` | (unset) | When set, use ACME staging directory |
| `CONTAINERCP_RENEWAL_INTERVAL` | `6` | Hours between renewal checks |

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

### New files
- `libs/ssl/AcmeClient.h/.cpp` — ACME protocol HTTP-01 implementation
- `libs/ssl/RenewalScheduler.h/.cpp` — background renewal scheduler
- `libs/ssl/CertificateStore.h/.cpp` — disk I/O for cert files + metadata.json
- `libs/ssl/SslCertificateView.h/.cpp` — view model combining site + ssl state

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
