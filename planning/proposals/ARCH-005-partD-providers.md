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


---
Back to [ARCH-005 index](ARCH-005-SSL-Management.md)
