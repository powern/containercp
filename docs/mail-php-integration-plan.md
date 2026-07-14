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

## 4. Site Mail Lifecycle Architecture

### 4.1 Design Principles

The mail lifecycle for a ContainerCP site follows these principles:

| # | Principle | Rationale |
|---|-----------|-----------|
| 1 | **Mail is opt-in, not automatic** | A site can exist purely as a web application. Mail is an add-on capability that the user explicitly enables. |
| 2 | **Runtime-first, not migration-first** | Mail configuration (msmtprc, SASL credentials, network) must be set up by the same code path for both new sites and migrated sites. The migration tool reuses runtime methods, not the other way around. |
| 3 | **Site mail = MailDomain + credentials + network** | Three things must exist for PHP `mail()` to work: (a) a MailDomain record for the site's domain, (b) SASL credentials for msmtp, (c) network connectivity from the PHP container to the mail stack. |
| 4 | **Layered tiers of mail service** | A site can opt into different mail tiers: (T1) outbound-only PHP transactional mail, (T2) full mail hosting with IMAP/webmail, (T3) external relay via smarthost. Each tier adds components but shares the same foundation. |
| 5 | **Separation of concerns** | MailDomainManager owns the data model (CRUD). A new orchestrator owns the lifecycle (credential generation, msmtprc, network). DockerMailProvider owns the global mail stack config. |
| 6 | **Idempotent enable/disable** | Enabling mail for a site that already has mail configured must be safe. Disabling must clean up without breaking other sites. |

### 4.2 Mail States for a Site

```
                    ┌──────────────┐
                    │  No Mail     │  (site created, mail never enabled)
                    └──────┬───────┘
                           │ enable mail
                           ▼
                    ┌──────────────┐
                    │  Enabling    │  (transitional — creating MailDomain,
                    └──────┬───────┘   credentials, network)
                           │
                           ▼
              ┌──────────────────────────┐
              │  Mail Active             │  (MailDomain exists, mode ≠ disabled)
              │  ┌────────────────────┐  │
              │  │  T1: Outbound only │  │  msmtp + SASL + network, no mailboxes
              │  ├────────────────────┤  │
              │  │  T2: Full hosting  │  │  T1 + mailboxes + IMAP + webmail
              │  ├────────────────────┤  │
              │  │  T3: External relay│  │  T1 + smarthost, MX is external
              │  └────────────────────┘  │
              └──────┬───────────────────┘
                     │ disable mail
                     ▼
              ┌──────────────┐
              │  Disabling   │  (transitional — removing credentials,
              └──────┬───────┘   msmtprc, network; MailDomain stays
                     │           with mode=disabled or is removed)
                     ▼
              ┌──────────────┐
              │  Mail Off    │  (MailDomain exists with mode=disabled,
              └──────────────┘   or no MailDomain at all)
```

Key decision: **MailDomain is NOT removed on disable** — it is kept with `mode=disabled`. This preserves the configuration so the user can re-enable without re-entering data. MailDomain is only removed during site removal (`SiteRemoveOperation` already handles this at line 66).

### 4.3 New Site Creation Flow

Current site creation (`SiteCreateOperation::execute()`) follows this order:

```
 1. Validate owner & domain
 2. Create Site record
 3. Create Domain record (linked to site)
 4. Create Database record
 5. DockerComposeProvider::create_site():
    a. Create site directories
    b. Generate .env with DB credentials
    c. Generate docker-compose.yml
    d. Generate web server config
    e. Create public/index.php
    f. docker compose up -d
 6. Create proxy config + reload
 7. Create proxy record
```

**Proposed change: Add mail preparation (T1 foundation) during step 5:**

```
 5. DockerComposeProvider::create_site():
    a. Create site directories
    b. Generate .env with DB credentials
    c. Generate docker-compose.yml (ALWAYS include containercp-mail network
       if the mail module is active — see 4.3.1)
    d. Generate web server config
    e. Create public/index.php
    f. docker compose up -d
    g. [NEW] If mail module is active:
       - Create /srv/containercp/sites/{domain}/config/php/ directory
       - Generate a placeholder msmtprc (SASL credentials unknown yet,
         so mail cannot actually be sent)
       - Add sendmail_path to site's php.ini override (see 4.3.2)
    h. [NEW] If mail module is inactive: no mail preparation
```

#### 4.3.1 Network: Conditional `containercp-mail` in Compose

The `containercp-mail` network is created by `DockerMailProvider::prepare_environment()` during mail module activation. It does NOT exist when the mail module is inactive.

The compose generator (`ComposeGenerator.cpp`) already handles service networks. Two approaches:

**Option A (recommended — future-proof): ALWAYS include `containercp-mail` in the compose template, but use `external: true` and `required: false`.** Docker Compose v3.8+ ignores missing external networks. This way every site's compose file is consistent from day one.

```yaml
networks:
  containercp-mail:
    external: true
    required: false   # Ignored if network doesn't exist yet
```

When the mail module is later activated, all existing compose files already reference the network — just `docker compose up -d` on each site to reconnect.

**Option B (current-reality): Add the network only when the mail module is active.** Simpler to implement but requires a batch upgrade path when the mail module is activated.

#### 4.3.2 PHP `sendmail_path`: Global vs Per-Site

The PHP `sendmail_path` directive can be set at two levels:

- **Global** (in `docker/php/Dockerfile` + `php.ini`): `sendmail_path = /usr/bin/msmtp -t`
- **Per-site override** (in site's `config/php/php.ini`): can set a custom `sendmail_path` if needed

**Recommendation:** Set `sendmail_path = /usr/bin/msmtp -t` globally in the PHP Docker image. The msmtp configuration (`/etc/msmtprc`) is what actually controls per-site behavior. This means every PHP container has msmtp installed and PHP is ready to use it — but without a valid `msmtprc`, mail fails safely.

#### 4.3.3 What Happens at Site Creation in Each Mail State

| Mail Module State | MailDomain exists? | Network connected? | msmtprc exists? | PHP mail() works? |
|---|---|---|---|---|
| Inactive | No (mail not yet relevant) | No (network doesn't exist) | No | No (msmtp not in image yet — Stage 1) |
| Active, site has no MailDomain | No | Yes (network always added) | Placeholder only | No (no valid credentials) |
| Active, site has MailDomain (mode=local-primary) | Yes | Yes | Yes (with real credentials) | Yes |
| Active, site has MailDomain (mode=disabled) | Yes | Yes | Placeholder only | No |

### 4.4 Enabling Mail for an Existing Site

When a user enables mail for a site (via API, CLI, or Web UI), the system must:

```
POST /api/sites/{id}/enable-mail (or extended POST /api/mail/domains)
```

**Step-by-step flow (idempotent, rollback on failure):**

| Step | Action | Component | Rollback |
|------|--------|-----------|----------|
| 1 | Find the site's primary Domain record | `DomainManager` | — |
| 2 | Create MailDomain with specified mode (or validate existing) | `MailDomainManager` | Remove MailDomain |
| 3 | Generate SASL credentials (username + password) | New `SiteMailCredentials` component | Remove credential from Postfix |
| 4 | Write `/etc/msmtprc` to site's config/php/ directory | `DockerComposeProvider` | Delete msmtprc |
| 5 | Connect PHP container to `containercp-mail` network | `Runtime::connect_mail_network()` | `docker network disconnect` |
| 6 | Sync mail config → Postfix reload (add to sender_login) | `RuntimeSync::sync("mail")` | Previous config state |
| 7 | Save state + update UI | `StateManager` | — |

**After enable:**
- The site's PHP container can send mail via msmtp → Postfix submission (port 587, STARTTLS)
- If mode is `local-primary`: Postfix delivers incoming mail to local mailboxes (if any exist)
- If mode is `external-relay`: Postfix relays outgoing mail to the external smarthost
- If mode is `outbound-only` (T1): Postfix accepts outgoing mail but rejects incoming (550)

**SASL credential generation:**

```
Username: site-{SITE_ID}@php.containercp.internal
Password: <auto-generated, 32-char alphanumeric>
Stored in: Dovecot passdb (for auth) + msmtprc (for client)
Mapped in Postfix: site-{SITE_ID}@php.containercp.internal → @{site_domain}
```

The `@php.containercp.internal` domain is a virtual domain that exists ONLY in Postfix/Dovecot for PHP mail authentication. It has no DNS, no MX records, and no mailboxes. It is purely an auth realm.

**Postfix sender_login map entry:**
```
site-42@php.containercp.internal  @example.com
```

This ensures authenticated PHP site 42 can ONLY send as `*@example.com`.

#### 4.4.1 API Surface

| Endpoint | Method | Purpose | Status |
|----------|--------|---------|--------|
| `/api/sites/{id}/enable-mail` | POST | Enable mail for a site | New (proposed) |
| `/api/sites/{id}/disable-mail` | POST | Disable mail for a site | New (proposed) |
| `/api/sites/{id}/mail-status` | GET | Get mail status for a site | New (proposed) |
| `/api/mail/domains` | POST | Create MailDomain (existing) | Extended to trigger msmtp setup |
| `/api/mail/credentials` | POST | Generate/rotate SASL credentials | New (proposed) |

The existing `POST /api/mail/domains` should be EXTENDED to automatically set up msmtp/SASL/network when the MailDomain is linked to a site (i.e., `domain_id` and `site_id` are non-zero). This keeps the API surface small.

### 4.5 Disabling Mail for a Site

When a user disables mail for a site:

| Step | Action | Component |
|------|--------|-----------|
| 1 | Remove /etc/msmtprc from site config | `DockerComposeProvider` |
| 2 | Remove SASL credentials from Postfix/Dovecot | `SiteMailCredentials` |
| 3 | Disconnect PHP container from `containercp-mail` network | `Runtime` |
| 4 | Set MailDomain mode to `disabled` (preserve config) | `MailDomainManager` |
| 5 | Sync mail config → Postfix reload | `RuntimeSync` |

**Important:** The MailDomain record is NOT deleted — only set to `disabled`. The user can re-enable later without re-entering configuration. Full deletion requires explicit `DELETE /api/mail/domains/{id}`.

**Rollback:** Each step is reversible with a rollback plan.

### 4.6 Migration Flow (VestaCP → ContainerCP)

The `VestaSiteImporter` has four stages:

```
Stage 0: inspect() — analyze source site
Stage 1: import_files() — copy files, create site
Stage 2: import_sql() — import database, update wp-config
Stage 3: upgrade_site() — ContainerCP-specific upgrades
```

**Proposed: Mail enablement happens during Stage 3 (upgrade_site), NOT during site creation.**

The flow:

1. **During `inspect()`**: Detect if the source site had mail configured (check for VestaCP mail domain, mailboxes, forwarders). Report findings to the user.

2. **During `import_files()`**: `SiteCreateOperation` creates the site (sections 4.3). At this point, the PHP container has no mail config — but the `containercp-mail` network is already connected (if mail module is active).

3. **During `upgrade_site()`**: If the user opted to migrate mail (and source had mail):
   - Same flow as "Enable Mail for Existing Site" (section 4.4)
   - Create MailDomain with `mode=local-primary`
   - Generate SASL credentials + msmtprc
   - Import VestaCP mailboxes via postfix/dovecot commands
   - Copy maildir data from backup

4. **Fresh migration (no source mail)**: `upgrade_site()` does NOT enable mail. The user must explicitly enable it later via API/UI/CLI.

**Why not during site creation?** Site creation is for the web application. Mail is an orthogonal feature that should be explicitly opted into, even during migration.

### 4.7 Mail Module Lifecycle Integration

The global mail module activation/deactivation affects all sites:

#### Mail Module Activated (`POST /api/mail/activate`)

```
DockerMailProvider::activate():
  1. prepare_environment() → create containercp-mail network, TLS certs, directories
  2. write_configs() → Postfix, Dovecot, Rspamd config for ALL domains
  3. start() → docker compose up -d (Postfix, Dovecot, Rspamd, Redis, SnappyMail)
  4. module_state = Active

After activation:
  Runtime::upgrade_mail():
    1. For every site that has a MailDomain with mode ≠ disabled:
       a. Generate SASL credentials (if missing)
       b. Generate msmtprc (if missing)
       c. Connect PHP container to containercp-mail network (via docker network connect)
    2. For every site without MailDomain: connect PHP to network anyway
       (harmless — no credentials = no mail)
```

#### Mail Module Deactivated (`POST /api/mail/deactivate`)

```
DockerMailProvider::deactivate():
  1. stop() → docker compose down (stops all mail containers)
  2. module_state = Inactive

After deactivation:
  - containercp-mail network is removed by Docker Compose
  - All PHP containers lose connectivity to mail stack
  - msmtprc files remain on disk but cannot connect
  - MailDomain records remain untouched
```

#### Runtime Recovery / Daemon Restart

On daemon restart (`ServiceRegistry::start()`):

```
For each site:
  If mail module is Active AND site has a MailDomain with mode ≠ disabled:
    1. Check if PHP container is connected to containercp-mail network
    2. If not connected: docker network connect
    3. Check if msmtprc exists in site config
    4. If not: regenerate msmtprc
  Else:
    Skip (no mail expected for this site)
```

This is part of the existing `Runtime::upgrade()` mechanism (to be extended).

### 4.8 Mandatory vs. Optional Components

| Component | Mandatory for PHP mail()? | Mandatory for Full Hosting? | Notes |
|-----------|--------------------------|----------------------------|-------|
| `msmtp` + `msmtp-mta` in PHP image | **Yes** | **Yes** | One-time image build |
| `sendmail_path = /usr/bin/msmtp -t` | **Yes** | **Yes** | Global in php.ini |
| PHP container connected to `containercp-mail` network | **Yes** | **Yes** | Set at site creation or upgrade |
| `/etc/msmtprc` per site | **Yes** | **Yes** | Generated on mail enable |
| SASL credentials (username + password) | **Yes** | **Yes** | Generated on mail enable |
| Postfix sender restrictions | **Yes** | **Yes** | Prevents spoofing |
| Postfix rate limiting | Recommended | Recommended | Abuse prevention |
| TLS for msmtp → Postfix | Recommended | **Yes** | Encryption in transit |
| MailDomain record | **Yes** | **Yes** | Defines mail handling policy |
| Mailbox (IMAP) | No | **Yes** | Required for incoming mail delivery |
| DKIM signing | Recommended | Recommended | Email deliverability |
| Webmail (SnappyMail) | No | **Yes** | User-facing mail access |
| Aliases (forwarders) | No | Optional | Mail routing |
| Smarthost (external relay) | Optional | Optional | If MX is external |

### 4.9 Existing Components to Reuse

| Component | Existing Capability | Reuse for Mail Lifecycle |
|-----------|-------------------|--------------------------|
| `MailDomainManager` | CRUD for MailDomain records | Create/update MailDomain during enable; read during sync |
| `MailboxManager` | CRUD for mailboxes | Create mailboxes for T2 hosting |
| `DockerMailProvider` | Postfix/Dovecot/Rspamd config generation | Add `smtpd_sender_login_maps`, rate limits; add `php.containercp.internal` domain to Postfix |
| `RuntimeSync::sync("mail")` | Calls `apply_config()` when Active | Trigger after enabling/disabling site mail |
| `DockerComposeProvider::create_site()` | Site directory layout, compose generation | Add `config/php/` directory, placeholder msmtprc |
| `ComposeGenerator` | Docker Compose YAML generation | Add `containercp-mail` network to PHP service |
| `Runtime::create_site_stack()` | `docker compose up -d` | Extend to connect `containercp-mail` after stack start |
| `SiteCreateOperation` | Site creation orchestrator | Accept optional mail enable flag |
| `SiteRemoveOperation` | Site removal cleanup | Already removes MailDomain records (line 66) |
| `VestaSiteImporter::upgrade_site()` | ContainerCP-specific upgrades | Add mail enable step |
| `PasswordGenerator` | Random password generation | Generate SASL passwords |
| `CommandExecutor` | Shell command execution | Postfix/`docker network connect` commands |
| `ServiceRegistry` | Startup recovery, health checks | Add mail connectivity health check per site |

### 4.10 Components to Extend

| Component | Changes Needed | Priority |
|-----------|---------------|----------|
| **NEW: `SiteMailOrchestrator`** | Coordinates enable/disable: MailDomain creation → credential generation → msmtprc → network → sync → rollback | High |
| **NEW: `SiteMailCredentials`** | Generates, stores, rotates per-site SASL credentials. Manages Postfix sender_login map entries. | High |
| `DockerMailProvider` | Add sender restrictions, rate limits, `php.containercp.internal` domain config, SASL passdb for PHP credentials | High |
| `ComposeGenerator` | Add `containercp-mail` network to PHP service (conditional or external:true) | High |
| `DockerComposeProvider::create_site()` | Create `config/php/` directory, write placeholder msmtprc, connect `containercp-mail` | High |
| `Runtime::upgrade()` | Iterate all sites → ensure mail network + msmtprc for enabled sites | Medium |
| `ApiServer` | Add `POST /api/sites/{id}/enable-mail`, `POST /api/sites/{id}/disable-mail`, extend `POST /api/mail/domains` to trigger lifecycle | Medium |
| `VestaSiteImporter::upgrade_site()` | Invoke `SiteMailOrchestrator` when source had mail | Medium |
| `web/app.js` | Mail status per site, enable/disable toggle | Medium |
| `docker/php/Dockerfile` | Add `msmtp`, `msmtp-mta`, `ca-certificates` packages | Stage 1 |
| `docker/php/php.ini` | Add `sendmail_path = /usr/bin/msmtp -t` | Stage 1 |

#### 4.10.1 SiteMailOrchestrator Interface (Proposed)

```cpp
class SiteMailOrchestrator {
public:
    // Enable mail for a site. Idempotent.
    virtual core::OperationResult enable_mail(
        uint64_t site_id,
        const std::string& domain,
        MailDomainMode mode = MailDomainMode::LocalPrimary,
        jobs::JobManager* jobs = nullptr,
        uint64_t job_id = 0) = 0;

    // Disable mail for a site. Idempotent. Preserves MailDomain with mode=disabled.
    virtual core::OperationResult disable_mail(
        uint64_t site_id,
        jobs::JobManager* jobs = nullptr,
        uint64_t job_id = 0) = 0;

    // Get mail status for a site
    virtual SiteMailStatus get_status(uint64_t site_id) = 0;

    // Rotate SASL credentials for a site
    virtual core::OperationResult rotate_credentials(
        uint64_t site_id) = 0;
};
```

#### 4.10.2 SiteMailCredentials Interface (Proposed)

```cpp
class SiteMailCredentials {
public:
    struct Credential {
        std::string username;   // site-{ID}@php.containercp.internal
        std::string password;   // auto-generated
        std::string domain;     // the site's domain (for sender_login map)
    };

    // Generate new credentials for a site
    virtual Credential generate(uint64_t site_id, const std::string& domain) = 0;

    // Remove credentials for a site
    virtual bool remove(uint64_t site_id) = 0;

    // Find existing credentials
    virtual std::optional<Credential> find(uint64_t site_id) = 0;

    // Apply credentials to Postfix (add to sender_login + SASL passdb)
    virtual core::OperationResult apply(const Credential& cred) = 0;

    // Remove credentials from Postfix
    virtual core::OperationResult revoke(const Credential& cred) = 0;
};
```

### 4.11 Architectural Decisions

This section records key architectural decisions made during the design of the site mail lifecycle.

#### ADR-2026-001: MailDomain Auto-Creation at Site Creation

**Decision:** Do NOT auto-create MailDomain at site creation.

**Context:** Every site has exactly one primary Domain record created during `SiteCreateOperation`. The question is whether a corresponding MailDomain should be created with `mode=disabled`.

**Considered options:**
- **Auto-create with mode=disabled:** Every site gets a MailDomain placeholder. Web UI can show "Enable mail" for any site. Simpler migration (MailDomain already exists).
- **Do NOT auto-create:** Mail is explicitly opt-in. No wasted records. Site creation stays clean.

**Rationale:**
- Mail is an add-on capability, not a core site feature (unlike Domain or Database which are created automatically).
- `SiteRemoveOperation` already cleans up MailDomains during site removal — so removing a site that never had mail would still check for MailDomains unnecessarily.
- Keep site creation fast and focused on web application concerns.
- The user explicitly enabling mail is a better UX than discovering a "disabled" MailDomain.

**Consequence:** MailDomain must be created when mail is enabled (section 4.4). Migration code must also create it.

#### ADR-2026-002: Per-Site SASL Credentials

**Decision:** Each site gets unique SASL credentials for msmtp.

**Context:** PHP containers need to authenticate to Postfix to send mail. The credentials can be shared across all sites or unique per site.

**Considered options:**
- **Shared credential:** Single username/password for all PHP sites. Simpler, but no per-site rate limiting, no audit trail.
- **Per-site credential:** Each site has unique credentials. Enables per-site rate limiting, sender restriction, audit.

**Rationale:**
- Per-site credentials enable `smtpd_sender_login_maps` to bind each site to its domain.
- If a site is compromised, only its credentials need to be rotated.
- Per-site rate limiting prevents one compromised site from affecting others.
- Matches ContainerCP's security philosophy.

**Consequence:** Need `SiteMailCredentials` component to manage per-site credentials. Integration with Dovecot SASL passdb required.

#### ADR-2026-003: msmtp as Runtime Concern

**Decision:** msmtp configuration is set up by `DockerComposeProvider::create_site()` or `SiteMailOrchestrator::enable_mail()`, NOT by the migration tool directly.

**Context:** Both new sites and migrated sites need msmtp configuration. The question is which code path owns this logic.

**Considered options:**
- **Migration-owning:** `VestaSiteImporter` generates msmtprc directly during migration.
- **Runtime-owning:** `DockerComposeProvider` and `SiteMailOrchestrator` own the logic. Migration calls these components.

**Rationale:**
- Single code path for both new and migrated sites.
- If the msmtp implementation changes, only runtime code needs updating.
- `VestaSiteImporter::upgrade_site()` already follows this pattern — it calls runtime upgrades.

**Consequence:** The migration `upgrade_site` method calls `SiteMailOrchestrator::enable_mail()`.

#### ADR-2026-004: `containercp-mail` Network in Compose Template

**Decision:** Include `containercp-mail` in every site's docker-compose.yml as an optional external network.

**Context:** The PHP container must be on the `containercp-mail` network to reach Postfix. The network may or may not exist at site creation time.

**Considered options:**
- **Optional external:** `required: false` — compose ignores missing network. All compose files are consistent.
- **Conditional generation:** Only add network when mail module is active. Requires batch upgrade on mail activation.

**Rationale:**
- Consistency: every site's compose file has the same structure.
- Future-proof: when mail module is activated, all sites are immediately ready (just `docker compose up -d`).
- Docker Compose v3.8+ supports `required: false` for external networks.
- Zero risk: if the network doesn't exist, it's simply ignored.

**Consequence:** ComposeGenerator must support optional external networks. This is a minor change to the YAML template.

#### ADR-2026-005: Mail Enable as Separate API, Not Implicit

**Decision:** Mail enable requires an explicit API call. Creating a MailDomain alone does NOT automatically enable msmtp.

**Context:** A user might create a MailDomain for configuration purposes (e.g., to set up external relay) without wanting PHP mail immediately.

**Considered options:**
- **Implicit:** Creating a MailDomain with mode != disabled automatically sets up msmtp.
- **Explicit:** Separate `enable-mail` call that creates MailDomain + sets up msmtp.

**Rationale:**
- Clarity: the user explicitly requests mail functionality.
- Safety: no automatic network connections or credential generation without user intent.
- Flexibility: user can configure MailDomain first, then enable PHP mail later.

**Consequence:** The existing `POST /api/mail/domains` endpoint should NOT trigger msmtp setup. A new `POST /api/sites/{id}/enable-mail` endpoint (or a flag on the MailDomain creation) should be introduced.

#### ADR-2026-006: Single `Runtime::upgrade()` Sufficient for Mail

**Decision:** No separate `mail-upgrade` command. Mail upgrade is handled by extending `Runtime::upgrade()`.

**Context:** When the daemon starts or after mail module activation, existing sites need their mail configurations checked.

**Considered options:**
- **Unified upgrade:** `Runtime::upgrade()` checks mail connectivity for all sites.
- **Separate mail-upgrade:** `Runtime::upgrade_mail()` or CLI `containercp upgrade-mail`.
- **Event-driven:** React to mail module activation event.

**Rationale:**
- Simplicity: one entry point for all upgrade logic.
- `Runtime::upgrade()` already iterates all sites for various upgrades.
- No need for an additional CLI command or event system.

**Consequence:** `Runtime::upgrade()` gets a new `upgrade_mail_for_site()` helper.

#### ADR-2026-007: Outbound-Only Mode (T1)

**Decision:** Add `outbound-only` as a new MailDomainMode value.

**Context:** Many web applications only need to send transactional emails (password resets, notifications) and do not need incoming mail hosting.

**Considered options:**
- **Reuse `local-primary` without mailboxes:** Postfix would accept incoming mail but have no mailbox → bounce. Works but generates backscatter.
- **Reuse `external-relay` with smarthost:** Requires configuring a smarthost even when direct delivery is fine.
- **New `outbound-only` mode:** Postfix rejects incoming mail for this domain (550), only allows authenticated outbound.

**Rationale:**
- No backscatter: reject incoming with 550 instead of accepting and bouncing.
- No unnecessary smarthost configuration.
- Clean semantics: the mode name communicates the intent.
- Minimal Postfix config change: add domain to `smtpd_relay_domains` but NOT to `mydestination` or `virtual_mailbox_domains`.

**Consequence:** `MailDomainMode` enum gains `OutboundOnly`. Postfix config generation adds a new domain class. Validation passes without relay_host.

### 4.12 Open Lifecycle Questions

These questions remain open and should be resolved before implementation:

1. **Self-service credential rotation** — Should the Web UI allow users to rotate PHP mail credentials? Or should this be admin-only via daemon API?

2. **msmtp log visibility** — Should msmtp logs be written to the site's `logs/` directory for troubleshooting? This requires a bind mount or log configuration.

3. **Mailbox auto-creation** — When enabling full hosting (T2), should a `postmaster@` mailbox be auto-created? What about `admin@` or `webmaster@`?

4. **Default MailDomain mode for enable** — When a user clicks "Enable mail" in the Web UI, what should the default mode be? `local-primary` if mail module is not smarthost-configured, `outbound-only` as a safe default?

5. **Site-level `mail_enabled` flag** — Should `site::Site` gain a `bool mail_enabled` field for fast lookup, or should it always be derived from MailDomain existence?

6. **Enforcement of unique PHP SASL credentials** — Should each site get one credential set, or could a site have multiple (one per alias domain)?

7. **Migration of VestaCP mailboxes** — During migration, should existing VestaCP mailboxes be imported automatically, or offered as an optional step?

## 5. Implementation Plan

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
New credential map format: `site-{SITE_ID}@php.containercp.internal` mapped to `@site-domain.com` domain prefix.

**2c. Add rate limiting:**
```postfix
smtpd_client_connection_rate_limit = 30
smtpd_client_message_rate_limit = 100
smtpd_client_recipient_rate_limit = 50
```

### Stage 3: Network Connectivity

**Files to modify:**
- `libs/docker/ComposeGenerator.cpp` — add `containercp-mail` network to PHP service (as optional external)
- `libs/provider/DockerComposeProvider.cpp` — ensure network reference exists

**Change in compose template:**
```yaml
php:
  networks:
    - containercp-site-{{SITE_ID}}
    - containercp-mail    # NEW — optional external network

networks:
  containercp-mail:
    external: true
    required: false
```

**For existing sites:** upgrade path via `Runtime::upgrade()`:
- `docker network connect containercp-mail site-{N}-php`
- Regenerate compose file to include network reference

### Stage 4: SMTP Credentials + TLS

**TLS fix:** The existing Postfix self-signed cert has `CN=mail.local` (not `containercp-mail-postfix`). Two options:

**Option A (simple):** msmtp connects with `tls_certcheck off` — skips hostname verification but still encrypts traffic.
```msmtprc
tls_certcheck off
```

**Option B (proper):** Generate cert with `CN=containercp-mail-postfix` or use Docker DNS alt name. More complex but properly verifiable.

**Recommended:** Start with Option A for simplicity, generate proper cert as a follow-up.

**New component:** `SiteMailCredentials`

For each site on mail enable, generate credentials via `SiteMailOrchestrator`:

```msmtprc
defaults
auth           on
tls            on
tls_certcheck  off
host           containercp-mail-postfix
port           587
user           site-{SITE_ID}@php.containercp.internal
password       <auto-generated>
from           {domain}@{domain}
```

**Credential storage:**
- Postfix `smtpd_sender_login_maps` (texthash): `site-{SITE_ID}@php.containercp.internal  @{domain}`
- Dovecot passdb or separate SASL database: stores the password for authentication

**New API endpoints:**
```
POST /api/sites/{id}/enable-mail
POST /api/sites/{id}/disable-mail
GET  /api/sites/{id}/mail-status
```

### Stage 5: SiteMailOrchestrator + SiteMailCredentials Implementation

**New files:**
- `libs/mail/SiteMailOrchestrator.h/.cpp`
- `libs/mail/SiteMailCredentials.h/.cpp`

**Responsibilities:**
- `SiteMailOrchestrator`: coordinates enable/disable/status of mail for a site
- `SiteMailCredentials`: manages per-site SASL credentials (generate, remove, find, apply, revoke)

### Stage 6: Runtime Upgrade — Mail Network Recovery

**Files to modify:**
- `libs/runtime/Runtime.cpp` — add `upgrade_mail_for_site()` method

**Logic:**
```cpp
void Runtime::upgrade_mail_for_site(uint64_t site_id, const std::string& domain) {
    // Check if PHP container is connected to containercp-mail
    // If not: docker network connect
    // Check if msmtprc exists
    // If not: regenerate via SiteMailOrchestrator
}
```

### Stage 7: Migration — Enable Mail for Migrated Sites

**Files to modify:**
- `libs/migration/VestaSiteImporter.cpp`

**Logic in `upgrade_site()`:**
```cpp
if (source_had_mail && user_opted_migrate_mail) {
    site_mail_orchestrator.enable_mail(site_id, domain, MailDomainMode::LocalPrimary);
    // Optionally import mailboxes from VestaCP dump
}
```

### Stage 8: WordPress wp_mail() + Web UI

- Test that `wp_mail()` reaches Postfix and is delivered
- Verify SPF/DKIM alignment for PHP-submitted mail
- Test smarthost relay through PHP → Postfix → external relay
- Outbound mail status page per site
- Enable/disable mail toggle
- Test send button
- msmtp health check (monitoring)

## 6. Implementation Ordering

The stages should be implemented in this order due to dependencies:

```
Stage 1 (PHP image) ───→ Stage 2 (Postfix security) ───→ Stage 3 (Network)
                                                              │
                                                              ▼
                                                     Stage 4 (Credentials)
                                                              │
                                                              ▼
                                                     Stage 5 (Orchestrator)
                                                              │
                                              ┌───────────────┼───────────────┐
                                              ▼               ▼               ▼
                                       Stage 6 (Runtime)  Stage 7 (Migrate)  Stage 8 (UI)
```

## 7. Files to Modify

| File | Change | Stage |
|------|--------|-------|
| `docker/php/Dockerfile` | Add msmtp msmtp-mta ca-certificates | 1 |
| `docker/php/php.ini` (new) | `sendmail_path = /usr/bin/msmtp -t` | 1 |
| `libs/mail/providers/DockerMailProvider.cpp` | Add `smtpd_sender_login_maps`, `reject_sender_login_mismatch`, rate limits, `php.containercp.internal` domain | 2 |
| `docker/mail/docker-entrypoint.sh` | Add sender restriction directives to submission | 2 |
| `libs/docker/ComposeGenerator.cpp` | Add `containercp-mail` network (optional external) to PHP service | 3 |
| `libs/provider/DockerComposeProvider.cpp` | Create `config/php/` dir, placeholder msmtprc | 3, 5 |
| `libs/mail/SiteMailOrchestrator.h/.cpp` | **New**: enable/disable/status orchestration | 5 |
| `libs/mail/SiteMailCredentials.h/.cpp` | **New**: per-site SASL credential management | 5 |
| `libs/runtime/Runtime.cpp` | Add `upgrade_mail_for_site()` for startup recovery | 6 |
| `libs/runtime/DockerRuntime.cpp` | Handle `docker network connect/disconnect` for mail | 6 |
| `libs/migration/VestaSiteImporter.cpp` | Invoke orchestrator during `upgrade_site()` | 7 |
| `libs/api/ApiServer.cpp` | Add enable/disable/mail-status endpoints | 5, 8 |
| `web/app.js` | Mail status per site, enable/disable toggle, test send | 8 |

## 8. Open Questions

### Verified (code confirmed):
1. **Per-site SASL credentials** — needed because no `reject_sender_login_mismatch` exists
2. **TLS cert CN mismatch** — must use `tls_certcheck off` or generate proper SAN cert
3. **msmtp = runtime concern** — belongs in `DockerComposeProvider::create_site()` or `SiteMailOrchestrator`, not migration
4. **No local queue** — Postfix downtime = immediate mail failure from PHP
5. **No rate limiting** — must be added to Postfix config during implementation
6. **No sender restrictions** — must add `smtpd_sender_login_maps` + `reject_sender_login_mismatch`
7. **No MailDomain auto-creation at site creation** — ADR-2026-001
8. **`containercp-mail` network as optional external** — ADR-2026-004

### Open for discussion:
1. Shared credential for all PHP sites vs per-site SASL user? → **ADR-2026-002: per-site**
2. Is a Web UI for outbound mail configuration required?
3. Should `/etc/msmtprc` be generated per-site (via DockerComposeProvider) or globally? → **per-site, per ADR-2026-002**
4. Should we offer a "test email" button in the Web UI?
5. Should msmtp logs be collected centrally?
6. How to handle PHP `mail()` return path / envelope sender?
7. TLS: `tls_certcheck off` or generate proper certificate? → **start with off, proper cert as follow-up**
8. Should site::Site gain a `mail_enabled` boolean field?
9. Mailbox auto-creation on mail enable (postmaster@)?
10. Default MailDomain mode for "Enable mail" action in UI?
11. Self-service credential rotation from Web UI?

## 9. Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| msmtp misconfiguration → silent mail drop | Medium | Health check: periodic test send |
| Postfix SASL password rotation breaks PHP mail | Low | Use stable credentials with Dovecot |
| Network reconnection on site recreate | Low | Startup recovery via `Runtime::upgrade_mail_for_site()` |
| WordPress plugins bypass `mail()` | Low | Document SMTP plugin configuration |
| **Postfix downtime → mail loss from ALL PHP sites** | **Medium** | msmtp has NO local queue. Add Postfix health check to site monitoring, document risk |
| **Compromised PHP site sends spam** | **Low** | Rate limiting + sender_login_maps restrict abuse |
| **TLS cert CN mismatch** | **Medium** | `tls_certcheck off` or generate proper SAN cert for containercp-mail-postfix |
| **No per-site rate limiting** | **Low** | Add per-site credentials with individual rate limits |
| **Mail module activation leaves existing sites without mail connectivity** | **Medium** | `Runtime::upgrade()` iterates all sites and connects PHP containers to `containercp-mail` network |
| **Site removal with active mail leaves Postfix with stale credentials** | **Low** | `SiteRemoveOperation` already removes MailDomain. Extend to also remove SASL credentials and revoke Postfix maps |
| **Migration enables mail for sites that don't need it** | **Medium** | Mail enable is optional during migration — user must explicitly choose to migrate mail |
