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


---
Back to [ARCH-005 index](ARCH-005-SSL-Management.md)
