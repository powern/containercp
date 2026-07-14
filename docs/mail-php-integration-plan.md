# Mail + PHP Integration Plan

## Status: Draft

## 1. Current State

### Existing: Global Mail Stack
ContainerCP has a fully functional global mail stack as a Docker Compose project:

| Container | Image | Role |
|-----------|-------|------|
| containercp-mail-postfix | ghcr.io/containercp/mail-postfix | SMTP MTA (ports 25, 465, 587) |
| containercp-mail-dovecot | ghcr.io/containercp/mail-dovecot | IMAP/POP3/LMTP (ports 143, 993) |
| containercp-mail-rspamd | ghcr.io/containercp/mail-rspamd | Spam filtering, DKIM signing (milter 11332) |
| containercp-mail-redis | redis:7-alpine | Cache |
| containercp-mail-snappymail | ghcr.io/containercp/mail-snappymail | Webmail (port 80, via /webmail/) |

### Missing: PHP → SMTP Connectivity
PHP containers CANNOT send email:

- PHP Dockerfile (`docker/php/Dockerfile`) installs only `mysqli` and `pdo_mysql`
- **No** `msmtp`, `sendmail`, `ssmtp`, or any MTA in PHP image
- **No** `sendmail_path` configured in PHP
- **No** network connection between site PHP containers and `containercp-mail` network

### Network Architecture Gap
```
containercp-public          containercp-site-N        containercp-mail
     │                            │                       │
  site-web ────────────────────── │                       │
  site-php ──── ❌ NOT connected   │   ❌ NOT connected    │
  site-db ─────────────────────── │                       │
  site-redis ──────────────────── │                       │
     │                            │                       │
  containercp-proxy ──────────────────────────────────────┘ (for SnappyMail)
```

## 2. How PHP `mail()` Currently Fails

```
PHP mail()
  → sendmail_path not configured    ❌ STOPS HERE
  → msmtp not installed             ❌
  → /usr/sbin/sendmail missing      ❌
  → no network to containercp-mail  ❌
  → WordPress wp_mail() silent fail ❌
```

## 3. Recommended Architecture

### Variant A (preferred): msmtp in PHP → central Postfix

```
PHP mail()
  → sendmail_path = /usr/bin/msmtp
  → msmtp (in PHP container, via /etc/msmtprc)
  → containercp-mail-postfix:587 (STARTTLS + SASL auth)
  → recipient
```

**Why this variant:**
- msmtp is ~100KB, minimal overhead
- Uses existing Postfix submission (port 587 already configured with Dovecot SASL)
- No additional containers needed
- Compatible with PHP `mail()`, `wp_mail()`, Laravel `mail()`
- Centralized mail queue management in Postfix

**Trade-offs vs alternatives:**
- vs **local Postfix per site**: less isolation, less resource usage ✅
- vs **direct SMTP from PHP**: works with `mail()` without code changes ✅
- vs **host-level relay**: fully containerized, no host dependency ✅
- vs **external SMTP only**: works even without external relay ✅

### 3.1 Verified Architectural Findings (code analysis of main branch)

The following findings were verified against the actual codebase and must be addressed before or during implementation:

| # | Finding | Code Evidence | Impact |
|---|---------|---------------|--------|
| 1 | **No sender restrictions** — any authenticated user can send as any `From:` address | `smtpd_sender_login_maps`, `smtpd_sender_restrictions`, `reject_sender_login_mismatch` — all absent from entire codebase | Security: `php-site-11` could spoof admin@any-domain.com |
| 2 | **No SASL-only users** — every Dovecot user gets a maildir; no outbound-only credential mode exists | `MailboxManager::create()` always creates full mailbox with home directory in Dovecot passwd | Need separate credential store for PHP msmtp (not mailbox) |
| 3 | **TLS certificate CN mismatch** — self-signed cert has `CN=mail.local`, not `containercp-mail-postfix` | `DockerMailProvider::ensure_certificate()` line 92: `-subj "/CN=mail.local"` | msmtp TLS verification will fail unless `tls_certcheck off` |
| 4 | **msmtp is runtime-first, not migration** — `DockerComposeProvider::create_site()` has zero mail logic | `DockerComposeProvider.cpp` lines 19–170: no mail network, no msmtp config | msmtp config must be set up DURING site creation, not only during migration |
| 5 | **No rate limiting anywhere** — Postfix has no connection/message rate limits configured | `smtpd_client_connection_rate_limit`, `anvil`, `postscreen` — all absent | Risk of abuse from compromised PHP sites |
| 6 | **msmtp has NO local queue** — if Postfix is unreachable, PHP `mail()` returns false immediately | `docker/php/Dockerfile` installs no MTA; msmtp has no spool | Transient Postfix downtime causes mail loss from all PHP sites |

## 4. Implementation Plan

### Stage 1: PHP Docker Image — Add msmtp

**Files to modify:**
- `docker/php/Dockerfile`

**Changes:**
```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
    msmtp msmtp-mta ca-certificates \
    && rm -rf /var/lib/apt/lists/*
```

**Also add PHP config:**
```ini
sendmail_path = /usr/bin/msmtp -t
```

### Stage 2: Postfix Security Hardening (prerequisite)

**Files to modify:**
- `libs/mail/providers/DockerMailProvider.cpp`
- `docker/mail/docker-entrypoint.sh`

**2a. Add sender restrictions:**
```postfix
# In write_postfix_config():
smtpd_sender_login_maps = texthash:/etc/postfix/sender_login
smtpd_sender_restrictions = reject_sender_login_mismatch, permit_sasl_authenticated, permit_mynetworks
```

This binds authenticated SMTP users to specific sender domains. Without this, any PHP site could send as any `From:` address.

**2b. Create SASL-only users (not mailboxes):**
New credential map format: `php-site-11@mail.local` mapped to `@unity.softico.ua` domain prefix.

**2c. Add rate limiting:**
```postfix
smtpd_client_connection_rate_limit = 30
smtpd_client_message_rate_limit = 100
smtpd_client_recipient_rate_limit = 50
```

### Stage 3: Network Connectivity

**Files to modify:**
- `libs/docker/ComposeGenerator.cpp` — add `containercp-mail` network to PHP service
- `libs/provider/DockerComposeProvider.cpp` — ensure network exists

**Change in compose template:**
```yaml
php:
  networks:
    - containercp-site-{{SITE_ID}}
    - containercp-mail    # NEW
```

**For existing sites:** upgrade path:
- `docker network connect containercp-mail site-{N}-php`
- Regenerate compose file

### Stage 4: SMTP Credentials + TLS

**TLS fix:** The existing Postfix self-signed cert has `CN=mail.local` (not `containercp-mail-postfix`). Two options:

**Option A (simple):** msmtp connects with `tls_certcheck off` — skips hostname verification but still encrypts traffic.
```msmtprc
tls_certcheck off
```

**Option B (proper):** Generate cert with `CN=containercp-mail-postfix` or use Docker DNS alt name. More complex but properly verifiable.

**New component:** `PhpMailCredentials` generator

For each site, generate `/etc/msmtprc` via bind mount or runtime config:
```msmtprc
defaults
auth           on
tls            on
tls_certcheck  off
host           containercp-mail-postfix
port           587
user           site-{SITE_ID}@php.mail.local
password       <auto-generated>
from           {domain}@{domain}
```

**Credential storage:** Use a separate `texthash` map in Postfix (`/etc/postfix/sender_login`) that maps `site-{SITE_ID}@php.mail.local` → `@{domain}` for sender restriction. Password stored in Dovecot SASL passdb or a separate `smtpd_sasl_password_maps`.

**Credential storage options:**
A. Per-site SASL user created in Dovecot via API
B. Shared credential for all PHP sites (simpler)
C. Docker secrets mounted into PHP container

**Runtime-first approach:** msmtp configuration is a **runtime concern**, not a migration concern. The `DockerComposeProvider::create_site()` method (not the migrator) should:
1. Add `containercp-mail` network to PHP container
2. Generate `/etc/msmtprc` from site config
3. Create SASL credentials for the site

The migrator (`VestaSiteImporter`) should simply reuse the same runtime mechanisms. This ensures new sites and migrated sites follow identical code paths.

### Stage 5: WordPress wp_mail() Validation

- Test that `wp_mail()` reaches Postfix and is delivered
- Verify SPF/DKIM alignment for PHP-submitted mail
- Test smarthost relay through PHP → Postfix → external relay

### Stage 5: Web UI + API

- Outbound mail status page per site
- Test send button
- msmtp health check (monitoring)

## 5. Files to Modify

| File | Change |
|------|--------|
| `docker/php/Dockerfile` | Add msmtp msmtp-mta ca-certificates |
| `docker/php/php.ini` (new) | `sendmail_path = /usr/bin/msmtp -t` |
| `libs/mail/providers/DockerMailProvider.cpp` | Add `smtpd_sender_login_maps`, `reject_sender_login_mismatch`, rate limits |
| `docker/mail/docker-entrypoint.sh` | Add sender restriction directives to submission |
| `libs/docker/ComposeGenerator.cpp` | Add `containercp-mail` network to php service |
| `libs/provider/DockerComposeProvider.cpp` | Generate `/etc/msmtprc`, create SASL credentials, connect network |
| `libs/runtime/DockerRuntime.cpp` | Handle mail network attachment during startup |
| `libs/migration/VestaSiteImporter.cpp` | Reuse runtime msmtp setup during migration |
| `libs/api/ApiServer.cpp` | Optional: endpoint for sending test email |
| `web/app.js` | Outbound mail status UI |

## 6. Open Questions

### Verified (code confirmed):
1. **Per-site SASL credentials** — needed because no `reject_sender_login_mismatch` exists
2. **TLS cert CN mismatch** — must use `tls_certcheck off` or generate proper SAN cert
3. **msmtp = runtime concern** — belongs in `DockerComposeProvider::create_site()`, not migration
4. **No local queue** — Postfix downtime = immediate mail failure from PHP
5. **No rate limiting** — must be added to Postfix config during implementation
6. **No sender restrictions** — must add `smtpd_sender_login_maps` + `reject_sender_login_mismatch`

### Open for discussion:
1. Shared credential for all PHP sites vs per-site SASL user?
2. Is a Web UI for outbound mail configuration required?
3. Should `/etc/msmtprc` be generated per-site (via DockerComposeProvider) or globally?
4. Should we offer a "test email" button in the Web UI?
5. Should msmtp logs be collected centrally?
6. How to handle PHP `mail()` return path / envelope sender?
7. TLS: `tls_certcheck off` or generate proper certificate? (trade-off: security vs complexity)

## 7. Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| msmtp misconfiguration → silent mail drop | Medium | Health check: periodic test send |
| Postfix SASL password rotation breaks PHP mail | Low | Use stable credentials with Dovecot |
| Network reconnection on site recreate | Low | Add to startup recovery |
| WordPress plugins bypass `mail()` | Low | Document SMTP plugin configuration |
| **Postfix downtime → mail loss from ALL PHP sites** | **Medium** | msmtp has NO local queue. Add Postfix health check to site monitoring, document risk |
| **Compromised PHP site sends spam** | **Low** | Rate limiting + sender_login_maps restrict abuse |
| **TLS cert CN mismatch** | **Medium** | `tls_certcheck off` or generate proper SAN cert for containercp-mail-postfix |
| **No per-site rate limiting** | **Low** | Add per-site credentials with individual rate limits
