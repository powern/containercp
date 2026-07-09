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


---
Back to [ARCH-005 index](ARCH-005-SSL-Management.md)
