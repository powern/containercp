# ContainerCP v0.6.0 Release Notes

**Release date:** 2026-07-16
**Version:** 0.6.0
**Previous release:** v0.5.0-rc2 (superseded)

---

## Overview

ContainerCP v0.6.0 is the **DNS and Mail** release. It completes the mail
subsystem (ARCH-006) and introduces a read-only DNS diagnostic center
(ARCH-007), along with production-grade SSL/HTTPS management (ARCH-005).

---

## New Features

### Mail Module (ARCH-006)

- **MailDomain resource** with 4 modes: Disabled, LocalPrimary, ExternalRelay,
  SplitM365. Domain-level mail configuration with relay host support.
- **Mailbox CRUD** with SHA-512-CRYPT password hashing. Per-mailbox enable/
  disable, quota, forwarding, and spam filtering.
- **Mail alias support** with domain-level virtual_alias_maps generation.
  Prevents self-loop and duplicate alias creation.
- **Docker mail stack** — Postfix, Dovecot, Redis, and Rspamd running in
  Docker containers with stock images (no custom builds).
- **DKIM key generation** via OpenSSL (2048-bit RSA), stored in MailDomain.
  Rspamd DKIM signing via milter proxy with `allow_username_mismatch=true`
  for PHP Mail compatibility.
- **TLS configuration** for both Postfix and Dovecot, using paths from
  CertificateStore.
- **Runtime synchronization** — 11 mail CRUD handlers trigger automatic
  config regeneration and service reload.
- **Health reporting** — Postfix/Dovecot/Redis status via HealthRegistry.
- **Module lifecycle** — activate/deactivate/status API endpoints.
- **Mail reload and recover** endpoints for safe configuration management.
- **Smarthost API** with TLS + SASL support for external relay.
- **PHP Mail integration** — per-site enable/disable with msmtp configuration.
  DKIM signing fix for username mismatch.

### DNS Diagnostic Center (ARCH-007)

- **DnsCheckService** — live DNS resolution using c-ares library.
  Supports A, AAAA, MX, TXT, CNAME, NS, SOA, CAA record types.
  60-second in-memory cache with `refresh=1` bypass.
- **REST API** — `GET /api/domains/<domain>/dns-check` with type filtering,
  error semantics, and structured responses.
- **Domain List** — progressive loading of DNS, Runtime, and Health columns.
  Real-time badge updates as data arrives.
- **Domain Detail** — 5-tab interface: Overview, DNS Records, Mail, Security,
  Health.
- **Configured vs Published comparison** — visual comparison for A, AAAA, MX,
  SPF, DKIM, DMARC, CAA, MTA-STS, TLS-RPT, Autodiscover records.
- **SPF Analyzer** — RFC 7208 semantic analysis supporting ip4, ip6, a, mx,
  include, redirect, all mechanisms. Uses c-ares for DNS resolution.
- **DMARC Wizard** — interactive policy selector (Monitor/Quarantine/Reject)
  with TXT record preview and comparison.
- **Evidence panels** — per-record why/how-to-fix panels showing expected
  vs published values, reasons, and remediation steps.
- **Context-aware Health Score** — 9 weighted check types with grade
  boundaries. Automatically excludes inapplicable checks (no mail, no runtime).
- **Admin-panel virtual system representation** — site_id=0 domains and sites
  synthesized at the view layer. Protected from deletion. Runtime N/A,
  SSL applicable.

### SSL/HTTPS Management (completed in ARCH-005)

- ACME HTTP-01 with Let's Encrypt (staging + production environments).
- CertificateStore with versioned metadata and atomic symlink rotation.
- Auto-renewal scheduler (24h interval).
- HTTP→HTTPS redirect support.
- SSL Web UI page with status overview.
- Admin-panel virtual Site SSL (site_id=0).

---

## Important Changes

### Breaking Changes
- **Version update**: 0.5.0-rc2 → 0.6.0. The v0.5.0 stable release was
  superseded by v0.6.0.
- **Site ID 0 semantics**: site_id=0 now represents the ContainerCP admin
  panel as a virtual system domain/site. Generic delete endpoints return 403
  for this record.

### Migration Notes
- Existing MailDomain records with domain_id=0 (external/unlinked) continue
  to work. The frontend no longer matches them against the admin-panel domain
  (id=0) — only exact FQDN matching is used.
- No database schema changes from v0.5.0-rc2.
- Existing sites, domains, databases, and backups are preserved.

---

## Known Limitations

### Mail
- **SnappyMail webmail**: Docker image `ghcr.io/containercp/mail-snappymail`
  is not available for all architectures. Webmail access requires the image
  to be published or built locally.
- **External relay modes**: ExternalRelay and SplitM365 modes require
  external MX/DNS configuration. Validated at the API level only.
- **24-hour stability test**: Not yet completed. Deferred to v0.6.1 or RC2.

### DNS
- **Read-only**: The DNS Diagnostic Center performs live lookups and
  comparisons but does NOT manage DNS zones. Authoritative DNS zone
  management is planned for a future release.
- **No HTTP check**: The Runtime column shows container status only.
  External HTTP reachability is not checked.

### General
- **Authentication**: All REST API endpoints use `AllowAll` auth middleware.
  Real authentication is not yet implemented.
- **Pagination**: Large datasets are not paginated.
- **PortManager**: The legacy PortManager is deprecated but not removed
  (ARCH-004 supersedes it).

---

## Release Candidate Validation Summary

All 94 validation items across 5 stages (A–E) have been executed and accepted:

| Stage | Scope | Items | Result |
|-------|-------|-------|--------|
| A | Build and startup | 1–9 | ✅ All PASS |
| B | Normal hosting regression | 10–20 | ✅ All PASS |
| C | Mail subsystem | 21–33 | ✅ All PASS/valid SKIP |
| D | DNS and admin panel | 34–80 | ✅ All PASS |
| E | Safety and stability | 79–94 | ✅ All PASS |

### Test Suite
- **Deterministic tests**: 242/242 passed
- **Integration tests** (live DNS): 15/15 passed
- **Full suite**: 257/257 passed
- **Build**: zero compiler warnings (Release)

---

## Files

| File | Purpose |
|------|---------|
| `build-release/containercpd` | Daemon binary |
| `build-release/containercp` | CLI thin client |
| `build-release/tests/containercp_tests` | Test executable |

### Key source directories

| Directory | Purpose |
|-----------|---------|
| `libs/dns/` | DNS check service, SPF analyzer, DNS check handler |
| `libs/network/` | Public IP detection |
| `libs/mail/` | MailDomain, Mailbox, MailAlias managers, DKIM, Docker mail provider |
| `libs/domain/` | Domain manager, domain view service (enriched JSON) |
| `libs/proxy/` | Reverse proxy manager, proxy view service, nginx provider |
| `libs/ssl/` | Certificate store, ACME client, renewal scheduler |
| `libs/api/` | REST API server, JSON formatter, sites view service |
| `web/` | Web UI (single-page application) |
| `planning/` | Implementation plans, validation checklists |
