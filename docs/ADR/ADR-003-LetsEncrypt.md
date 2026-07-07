# ADR-003: Automatic HTTPS (Let's Encrypt)

## Status

Accepted

## Context

ContainerCP must automatically provision HTTPS certificates for hosted
sites. The system should request certificates from Let's Encrypt when
a site is created and renew them automatically before expiry.

## Decision

### Architecture

A `CertificateProvider` abstraction is introduced, similar to
`HostingProvider` and `AccessProvider`. The interface supports:

- `request(domain)` — request a new certificate
- `renew(domain)` — renew an existing certificate
- `revoke(domain)` — revoke a certificate
- `status(domain)` — check certificate status

### Implementation

`LetsEncryptProvider` implements `CertificateProvider` using the ACME
protocol. In the MVP, this provider is a placeholder that logs actions
without calling real ACME servers.

The existing `SslCertificate` resource is extended with:

- `domain_id` (foreign key to Domain)
- `provider` (string: "letsencrypt", "manual", "placeholder")
- `certificate_path` (path to fullchain.pem)
- `key_path` (path to privkey.pem)
- `expires_at` (ISO-8601 date string, "unknown" if not set)
- `status` (string: "requested", "active", "expired", "failed", "placeholder")
- `auto_renew` (bool, default true)
- `enabled` (bool)

### Lifecycle

When a site is created:

1. Site record + Domain + Database are created
2. ReverseProxy resource and config are created
3. SslCertificate resource is created with status "requested"
4. CertificateProvider::request() is called
5. If successful, status becomes "active"

When a site is removed:

1. CertificateProvider::revoke() is called
2. SslCertificate resource is removed

### Renewal strategy

A `RenewalScheduler` abstraction handles periodic renewal checks.
In the MVP, the scheduler is a no-op placeholder. The CLI provides
`ssl renew <domain>` for manual renewal.

### Storage

Certificates are stored in `ssl_certificates.db` with format:

```
id|domain_id|domain|provider|certificate_path|key_path|expires_at|status|auto_renew|enabled
```

### CLI

```
ssl list                — list all certificates
ssl show <domain>       — show certificate details
ssl request <domain>    — request new certificate
ssl renew <domain>      — renew certificate
ssl revoke <domain>     — revoke certificate
ssl enable <domain>     — enable certificate
ssl disable <domain>    — disable certificate
```

### Integration

- `SiteCreateOperation` creates an SslCertificate resource and calls
  `CertificateProvider::request()`.
- `SiteRemoveOperation` removes the SslCertificate resource and calls
  `CertificateProvider::revoke()`.
- `NginxProxyProvider` generates SSL-enabled proxy configs when a
  certificate exists and is enabled.

### Future

- ACME HTTP-01 challenge requires port 80 reachable
- ACME DNS-01 challenge supports wildcard certificates
- Auto-renewal via systemd timer or embedded scheduler
- Certificate expiry monitoring and alerting

## Consequences

- SSL is fully automated for new sites
- The provider abstraction allows future ACME client changes
- Placeholder provider allows development without real certificates
- Existing `ssl enable/disable` commands remain backward compatible
