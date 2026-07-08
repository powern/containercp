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

SSL is a capability of the **Site** model, not the Proxy. Every site
has an SSL state (HTTP_ONLY by default). The system is built around a
generic `CertificateProvider` interface. REST API, GUI, and daemon
depend only on `CertificateProvider`, never on a concrete
implementation. Future providers are pluggable without changing
business logic.

```
Site
 ├── Backend (Apache/Nginx)
 ├── Database (MariaDB)
 ├── Proxy (nginx config + cert attachment)
 └── SSL ← CertificateProvider (abstract)
               ↑
        ┌──────┴──────┐
        │             │
  LetsEncrypt    CustomCertificate
  Provider       Provider
     │
  ┌──┴──┐
  │     │
HTTP01  DNS01
ChallengeProvider (future)
```

```
┌─────────────────────────────────────────────────────┐
│              CertificateProvider (abstract)          │
│  request() / renew() / revoke() / status()          │
│  provider_name() / certificate_path()               │
└─────────────────────────────────────────────────────┘
         ↑                          ↑
┌────────┴──────────┐    ┌─────────┴──────────┐
│ LetsEncryptProvider│    │CustomCertificate   │
│  ┌──────────────┐  │    │Provider            │
│  │ Challenge    │  │    │ (import PEM,       │
│  │ Provider     │  │    │  manage files)     │
│  │ (abstract)   │  │    └────────────────────┘
│  └──────┬───────┘  │
│         │          │
│  ┌──────┴──────┐   │
│  │HTTP01       │   │
│  │Challenge    │   │
│  │Provider     │   │
│  └─────────────┘   │
│                    │
│  Future: DNS01     │
│  ChallengeProvider │
└────────────────────┘
         │
         │ attach_certificate / detach_certificate
         ▼
┌─────────────────────────────────────────────────────┐
│              ProxyProvider (abstract)                │
│  - generate server config (nginx, future: Caddy,    │
│    Traefik, HAProxy)                                 │
│  - attach certificates to HTTPS listener            │
│  - reload proxy                                     │
└─────────────────────────────────────────────────────┘
```

### CertificateProvider abstraction (sole dependency)

The entire system is designed around `CertificateProvider`. No code
outside the provider implementations references `LetsEncryptProvider`,
`CloudflareProvider`, or any concrete class by name. Providers are
registered in `ServiceRegistry` by string key and selected per-site.

```
ServiceRegistry::certificate_providers()
    → map<string, CertificateProvider*>
    → {"letsencrypt": ..., "custom": ...}
```

REST API handlers, CLI handlers, and RenewalScheduler all call
`CertificateProvider` methods. Adding a new provider means:
1. Implement the interface
2. Register it in ServiceRegistry
3. No business logic changes

### SSL belongs to Site model

SSL state is part of the Site aggregate:

```
Site {
    string domain;
    BackendType backend;
    SslState ssl_state;  // HTTP_ONLY | ISSUING | ACTIVE | ERROR | DISABLED
    optional<SslCertificate> certificate;
}
```

This ownership means:
- SSL tab in site detail shows the certificate
- Site removal cleans up SSL files
- Domain changes on a site trigger certificate invalidation
- Site listing includes SSL state

### Site creation behavior

When a site is created:
1. Site is created as HTTP-only (no SSL resource created).
2. No ACME call, no certificate, no HTTPS config.
3. SSL resource is created lazily only when user requests certificate issuance.

This means:
- `SiteCreateOperation` does NOT touch SSL at all.
- The SSL page in Web UI shows HTTP_ONLY for every site that has no certificate.
- No delayed creation, no background issuance.

### SAN / multi-domain preparation

Today every site has one domain. Future versions will support multiple
domains per site (e.g., `example.com`, `www.example.com`,
`api.example.com`). The SSL architecture is designed for this from the
start:

- `SslCertificate.domains[]` stores all domains covered by the
  certificate (the primary domain + SAN entries).
- `metadata.json.domains[]` persists the domain list.
- The ACME order requests a SAN certificate for all listed domains.
- The nginx `server_name` directive lists all domains.

When a site's domain list changes:
1. The current certificate is detected as stale (domains differ).
2. The status transitions to `error` with `last_error =
   "Certificate does not cover current domains. Reissue required."`.
3. HTTPS continues working until the old cert expires, but the GUI
   prompts the user to reissue.
4. The user clicks "Reissue" and a new SAN certificate is obtained.

This design means no breaking change when multi-domain per site is
implemented — the data model already supports it.

### ACME HTTP-01 flow (Issue)

```
1. POST /ssl/<domain>/issue
2. SslCertificate resource created (status: ISSUING)
3. LetsEncryptProvider creates account key (if first run, stored at
   /srv/containercp/ssl/account.pem)
4. Provider runs preflight validation (DNS, port 80, .local check)
5. Provider requests new order + authorization from ACME server
6. ACME returns HTTP-01 challenge token + value
7. Provider delegates to ChallengeProvider::prepare() which writes
   challenge file to site directory's .well-known/acme-challenge/<token>
8. Central nginx serves /.well-known/acme-challenge/ from disk
9. ACME server validates → certificate issued
10. Provider downloads fullchain.pem, privkey.pem, chain.pem
11. Provider calls CertificateStore::save_all(site_id, meta, cert, key, chain)
12. Provider writes metadata.json via CertificateStore (atomic write)
13. Provider calls ProxyProvider::attach_certificate(domain, cert_path, key_path)
14. ProxyProvider generates HTTPS server block, reloads nginx
15. SslCertificate status updated to ACTIVE
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
| `domains` | string (comma-separated) | `""` | All domains covered by certificate (SAN) |
| `challenge_type` | string | `""` | Challenge used: "http-01" or "dns-01" |
| `last_validation` | string (ISO-8601) | `""` | Last successful preflight validation |
| `renew_attempts` | int | `0` | Consecutive failed renewal attempts |
| `version` | int | `1` | Metadata schema version for forward compat |

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

The scheduler runs inside the daemon process. It checks every 24 hours.
No external cron dependency. The daemon is already long-running, so the
scheduler is a simple `std::thread` with a `sleep_until` loop.

Scheduler logic:
1. Enumerate all certificates with status `active` and `auto_renew == true`.
2. For each, compare current date with `renew_after`.
3. If due, call `CertificateProvider::renew(domain)`.
4. On success: update metadata, reset `renew_attempts` to 0.
5. On failure: increment `renew_attempts`, set `last_error`, log warning.
   Old certificate is kept and continues working.
6. If `renew_attempts >= 7` (one week of daily failures), transition
   status to `error` and disable HTTPS. Site reverts to HTTP-only.
7. Log summary: "Renewal check complete: N active, M renewed, F failed".

## Storage

### Database: `ssl_certificates.db`

Extended pipe-delimited format:

```
id|domain_id|domain|provider|certificate_path|key_path|chain_path|issued_at|expires_at|renew_after|status|auto_renew|https_enabled|redirect_enabled|domains|challenge_type|last_error|last_validation|renew_attempts|version
```

### Disk storage: `/srv/containercp/ssl/<site-id>/`

Storage is keyed by Site ID, not domain name. This allows future
domain renames without moving certificate storage. The current domain
is stored in metadata only.

```
/srv/containercp/ssl/
├── account.pem                        — ACME account private key
└── <site-id>/
    ├── metadata.json                  — single source of truth
    ├── fullchain.pem                  — full certificate chain (0644)
    ├── privkey.pem                    — private key (0600)
    └── chain.pem                      — CA chain only (0644)
```

### StorageManager: `CertificateStore`

A dedicated `CertificateStore` class handles all disk I/O. Providers
never manipulate files directly. Only `CertificateStore` performs:

- `save_metadata(site_id, metadata)` — atomic write with fsync + rename
- `load_metadata(site_id)` — parse JSON, return Metadata struct
- `save_fullchain / save_privkey / save_chain` — atomic PEM writes
- `save_all(site_id, ...)` — batch write all files
- `load_fullchain / load_privkey / load_chain` — read PEM files
- `remove_all(site_id)` — delete all files and directory
- `enumerate()` — scan ssl_root for numeric directories
- `validate(site_id)` — check files, permissions, metadata integrity

### metadata.json format

```json
{
    "version": 1,
    "site_id": 1,
    "provider_id": "letsencrypt",
    "certificate_type": "pem",
    "status": "active",
    "domains": ["example.com", "www.example.com"],
    "issued_at": "2025-07-08T12:00:00Z",
    "expires_at": "2025-10-06T12:00:00Z",
    "renew_after": "2025-09-06T12:00:00Z",
    "https_enabled": true,
    "redirect_enabled": false,
    "auto_renew": true,
    "challenge_type": "http-01",
    "last_validation": "2025-07-08T11:55:00Z",
    "last_error": "",
    "renew_attempts": 0,
    "fingerprint_sha256": "abc123...",
    "serial_number": "04:AB:CD...",
    "issuer": "CN=R3, O=Let's Encrypt, C=US",
    "subject": "CN=example.com",
    "created_at": "2025-07-08T12:00:00Z",
    "updated_at": "2025-07-08T12:00:00Z"
}
```

The `version` field enables forward-compatible schema migrations.
Future readers must tolerate unknown fields. Writers always include
all known fields. Timestamps use ISO-8601 UTC.

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

### ChallengeProvider (abstract)

ACME challenge logic is extracted into its own interface so the
challenge method (HTTP-01, DNS-01) is decoupled from certificate
lifecycle orchestration.

```cpp
class ChallengeProvider {
public:
    virtual ~ChallengeProvider() = default;

    virtual std::string type() const = 0;  // "http-01" | "dns-01"

    // Prepare the challenge (write token file, create DNS record)
    virtual OperationResult prepare(const std::string& domain,
                                     const std::string& token,
                                     const std::string& key_authorization) = 0;

    // Clean up after challenge
    virtual OperationResult cleanup(const std::string& domain,
                                     const std::string& token) = 0;

    // Verify the challenge is in place and reachable
    virtual OperationResult verify(const std::string& domain) = 0;
};
```

- `HTTP01ChallengeProvider` writes tokens to
  `/srv/containercp/ssl/<domain>/.well-known/acme-challenge/` and
  verifies they are served through the central proxy on port 80.
- `DNS01ChallengeProvider` (future) creates DNS TXT records via API
  and verifies propagation.
- `LetsEncryptProvider` holds a `ChallengeProvider` reference and
  delegates challenge steps. The orchestration logic is identical
  regardless of challenge type.

### Preflight validation

Before contacting the ACME server, `LetsEncryptProvider` runs
preflight checks to fail fast with meaningful errors:

| Check | Failure message |
|-------|----------------|
| Domain resolves to a public IP | `Domain does not resolve to a public IP address` |
| Domain is not localhost | `Cannot issue certificate for localhost` |
| Domain does not end with .local | `Cannot issue certificate for .local domains` |
| Domain does not end with .test | `Cannot issue certificate for .test domains` |
| Port 80 is reachable on public IP | `Port 80 is not reachable from the internet` |
| ACME challenge path is accessible | `ACME challenge directory is not accessible` |
| Certificate does not already exist and is active | `Valid certificate already exists, use /renew` |
| Domains have not changed since last issue | `Domains changed since last issue, must reissue` |

If any check fails, the provider returns `OperationResult{false,
"message"}` immediately. The ACME server is never contacted. This
saves rate limits and gives the user actionable feedback.

### LetsEncryptProvider (real ACME HTTP-01)

- ACME directory URL: `https://acme-v02.api.letsencrypt.org/directory`
- Staging: `https://acme-staging-v02.api.letsencrypt.org/directory`
  (enable via `LETSENCRYPT_STAGING=1` env var for testing)
- Account key stored at `/srv/containercp/ssl/account.pem`
- EC private key generated on first use (P-256 for ACME JWT)
- Delegates challenge to `ChallengeProvider` (currently HTTP01ChallengeProvider)
- Certificate files written to `/srv/containercp/ssl/<domain>/`
- Uses libcurl for ACME HTTPS calls
- JWT signed with ES256 (ECDSA P-256) using OpenSSL
- Minimal JSON parser (built-in, no external dependency)
- ACME retry: 3 attempts with exponential backoff (1s, 3s, 9s)
- Preflight validation runs before every ACME request

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

### Step 4 — LetsEncryptProvider (ACME HTTP-01)
- Implement `HTTP01ChallengeProvider` with real token write/serve
- Implement `LetsEncryptProvider` with ACME protocol
- Implement preflight validation (DNS, port 80, .local/.test checks)
- ACME account key generation (P-256)
- libcurl integration for ACME HTTPS calls
- JWT signing with ES256
- Minimal JSON parser for ACME responses
- Certificate download to `/srv/containercp/ssl/<domain>/`
- `metadata.json` write after successful issue
- Retry with exponential backoff
- Lock file for concurrent issue prevention
- Staging mode via `LETSENCRYPT_STAGING=1`
- Commit and push

### Step 5 — Proxy integration
- Implement `ProxyProvider::attach_certificate()` in
  `NginxProxyProvider` — generate HTTPS nginx config with
  `ssl_certificate` + `ssl_certificate_key` + optional redirect
- Implement `ProxyProvider::detach_certificate()` — remove SSL config,
  revert to HTTP
- Add `/.well-known/acme-challenge/` location to central proxy config
- Call `attach_certificate` after successful issue/renew
- Call `detach_certificate` on disable
- No nginx-specific SSL logic outside `NginxProxyProvider`
- Commit and push

### Step 6 — RenewalScheduler
- Implement `RenewalScheduler` with 24-hour interval
- Enumerate active certificates due for renewal
- Call `CertificateProvider::renew()` for each due cert
- Track `renew_attempts`, disable after 7 consecutive failures
- Log summary after each check cycle
- Start scheduler in daemon `main.cpp`
- Commit and push

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
