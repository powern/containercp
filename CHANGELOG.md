# Changelog

All notable changes to ContainerCP are documented here.

Format: date | commit | summary

---

## 2025-07-09 | Phase 1–5: Runtime management

- Runtime subsystem with `RuntimeActionExecutor`, `ServiceRole`, `CommandExecutor`
- Site Details page redesigned as Website Management Center with Runtime card
- Runtime card shows Frontend/PHP/Database/Redis status + restart actions
- Runtime architecture refactoring: `ServiceRole` abstraction, `ContainerStatus` moved to executor
- Phase 3 fix: restart-all semantics, restart-db/redis actions added
- Phase 5 cleanup: moved container status inspection into `RuntimeActionExecutor`
- Phase 4: Sites UI restart actions (⚡ dropdown), Phase 5: moved to Site Details

See `docs/changelog/runtime-phases.md` for detailed entries with commit hashes,
file changes, validation results, and known risks.

---

## 2025-07-08 | SSL & HTTPS subsystem

- ACME HTTP-01 challenge via Web UI (staging + production)
- Bootstrap simplified (removed SSL step)
- Admin Panel HTTPS on port 443
- Let's Encrypt integration with auto-renewal
- `CertificateStore`, `RenewalScheduler`, `PemCertificateProvider`
- HTTPS status display in Sites Runtime card (consumes `CertificateStore`)

See `docs/changelog/ssl-subsystem.md` for detailed per-commit entries.

---

## 2025-07-08 | RC2 — Stability & Production Foundation

- Daemon architecture, REST API hardening
- Deployment scripts, update mechanism
- Port management refactoring
- Bug fixes for login, site removal, and rollback

See `docs/changelog/rc2-stability.md` for detailed entries.

---

## 2025-07-08 and earlier | Earlier development

- Multi-site Docker networking (ARCH-004)
- Web UI v0.5, PHP hosting, profiles
- CLI tooling, template engine
- Sprint reviews and infrastructure setup

See `docs/changelog/early-development.md` for detailed entries.

---

### Risks (current)

- Existing sites with host-port allocation retain old compose template
- Fresh site creation uses new template (always overwritten on disk)
- Deprecated PortManager not yet removed — cleanup planned
- `RuntimeActionExecutor` requires Docker Compose v2+
- Async jobs cancelled/marked failed if daemon shuts down during execution

---

## 2026-07-11 | Mail module hardening

- Network isolation: LMTP port 24 removed from host, ports bound to 127.0.0.1
- LMTP via Docker DNS (`containercp-mail-dovecot:24`) instead of `127.0.0.1:24`
- Router consolidation: 6 prefix handlers → 2 dispatchers, no 404 fallthrough
- Transactional `apply_config()`: generate → `postfix check` → reload → rollback
- Self-signed TLS cert auto-generated on fresh install (`ensure_certificate()`)
- Certificate status reported in health endpoint (valid/self-signed/expired/missing)
- Alias self-loop detection, `postmap -q` validation before apply
- Process-level health checks: `postfix status`, `doveadm who`, `redis-cli ping`
- Health status model: ok / degraded / error
- E2E test script: `scripts/test-mail-routing.sh`
- All aliases now written to Postfix `virtual_alias_maps` (was `(void)aliases;`)
- Port publishing fixed: Postfix 25/465/587, Dovecot 143/993 exposed on host

---

## 2026-07-11 | SMTP + DNS + Smarthost

- SMTP server fixes: Postfix master starts reliably (base image → debian:bookworm,
  stale socket cleanup in entrypoint, virtual_mailboxes mount, empty map files,
   Rspamd milter temporarily disabled, later re-enabled for DKIM signing)
- Postfix config: compatibility_level, mynetworks, smtpd_relay_restrictions,
  smtp_host_lookup, maillog_file (direct file logging, no syslog dependency)
- Docker DNS fix: resolv.conf with Google DNS, chroot jail copy, `dns: 8.8.8.8`
- Smarthost API: `GET /api/mail/smarthost`, `POST /api/mail/smarthost`
  ```json
  {"enabled":true,"host":"smtp.gmail.com","port":587,
   "username":"user@gmail.com","password":"app-password"}
  ```
- DKIM DNS record format (add TXT to your DNS provider):
  ```
  Type:  TXT
  Name:  dkim._domainkey.<your-domain>
  Value: v=DKIM1; k=rsa; p=<your-public-key>
  ```
- Direct MX delivery verified: admin@maillab.softi.co → powern76@gmail.com
  (SPF: PASS, DMARC: PASS, TLS: AES_256_GCM_SHA384)

---

## 2026-07-12 | SnappyMail webmail integration

- New container: `containercp-mail-snappymail` (alpine + nginx + php84)
- Image: `ghcr.io/containercp/mail-snappymail:latest`
- Accessible at `https://<server-hostname>/webmail/` via nginx proxy
- Connects to Dovecot IMAP and Postfix SMTP (STARTTLS auth)
- New `webmail_upstream` param in `ProxyConfigBuilder::Params`
- Web UI sidebar: Webmail link added between Mail and Proxy
- Health check: snappymail container status reported in /api/health

---

## 2026-07-12 | Rspamd DKIM signing (replaced OpenDKIM)

- OpenDKIM milter was not signing — replaced with Rspamd milter proxy
- `POST /api/mail/domains/<id>/dkim/generate` generates 2048-bit RSA key
- DKIM DNS record stored in `MailDomain::dkim_public_key_dns`, returned via API
- Rspamd `dkim_signing` module signs outbound mail via milter on port 11332
- Postfix milter: `smtpd_milters = inet:containercp-mail-rspamd:11332`
- Config: `worker-proxy.inc`, `worker-normal.inc`, `dkim_signing.conf`, `logging.inc`
- Key permissions: 644 (Rspamd runs as `_rspamd` user in container)
- Bug fixes: `use_esld=false` (eSLD mismatch broke domain key lookup),
  `worker-normal.inc` not generated, `worker-proxy.inc` not mounted in compose
- Docker images: `ghcr.io/containercp/mail-rspamd:latest` (debian:trixie + rspamd)
