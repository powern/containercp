# Mail Module — Architecture & Roadmap

## Status: Draft (Stage 0 — Design)

---

## 1. Architecture overview

The Mail subsystem uses a **single global mail stack** per ContainerCP
server.  Multiple domains are managed within one stack, not one server
per domain.

### Docker topology

```
┌─────────────────────────────────────────────────────────┐
│                    containercp-mail                      │
│                    (Docker Compose project)              │
│                                                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐ │
│  │  Postfix  │  │  Dovecot │  │  Rspamd  │  │  Redis │ │
│  │  (SMTP)   │  │ (IMAP/   │  │ (spam/   │  │ (queue │ │
│  │  25/465/  │  │  POP3)   │  │  virus)  │  │  cache)│ │
│  │  587/993  │  │  143/993 │  │          │  │        │ │
│  └──────────┘  └──────────┘  └──────────┘  └────────┘ │
│                                                         │
│  ┌──────────┐  ┌──────────────────────────────────────┐ │
│  │  DKIM    │  │  Adminer / RainLoop / SnappyMail     │ │
│  │  signing │  │  (webmail — future stage)            │ │
│  └──────────┘  └──────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

### Docker stack comparison

Three approaches were evaluated:

#### Option A: Custom Postfix/Dovecot stack (recommended)

| Component | Role |
|-----------|------|
| Postfix | SMTP server (MTA) |
| Dovecot | IMAP/POP3 server with SASL auth |
| Rspamd | Spam filtering, DKIM signing, rate limiting |
| Redis | Rspamd backend, queue cache |

**Advantages:**
- Full control over every configuration detail
- Each component is a mature, well-documented industry standard
- Easy to debug — standard tools, standard log formats
- No intermediate abstraction layer hiding configuration
- Upgrade each component independently
- Compatible with any management tool (Postfixadmin, Dovecot REST API)
- Rspamd replaces Amavis + SpamAssassin + OpenDKIM in one daemon

**Disadvantages:**
- More components to configure initially
- No single "mail stack" Docker image — manage multiple containers

#### Option B: docker-mailserver (single-image stack)

A single Docker image containing Postfix, Dovecot, Rspamd, and webmail.

**Advantages:**
- Single container, simpler deployment
- Active community, frequent updates
- Pre-integrated configuration

**Disadvantages:**
- Less control over individual component versions
- Configuration is abstracted through environment variables — harder to debug
- Upstream decisions may not match ContainerCP requirements
- Replacing one component (e.g. Rspamd → custom) is difficult
- Harder to integrate with ContainerCP's existing service architecture

#### Option C: Mailcow (multi-container stack)

A complete mail suite with its own management UI and API.

**Advantages:**
- Feature-rich out of the box
- Includes webmail, spam, antivirus

**Disadvantages:**
- Duplicates ContainerCP's management layer
- Conflicting UIs and API conventions
- Heavy dependency footprint
- Difficult to align with ContainerCP's domain model

#### Decision

**Option A (custom Postfix/Dovecot stack)** is recommended because:

1. Each component is an industry standard with decades of operational knowledge.
2. Configuration is transparent — no abstraction layer between ContainerCP
   and the mail daemon.
3. Components can be upgraded independently.
4. Debugging uses standard tools (`postfix log`, `doveadm`, `rspamc`).
5. The stack aligns with the existing Docker-based architecture already used
   for site containers and the reverse proxy.

### Network layout

```
  Internet
    │
    ├── SMTP (25)  ──→  Postfix (container)
    ├── SMTPS (465) ──→ Postfix (container)
    ├── SMTP (587)  ──→ Postfix (container)
    │
    ├── IMAPS (993) ──→ Dovecot (container)
    ├── POP3S (995) ──→ Dovecot (container)
    │
    └── HTTP (webmail) ──→ nginx → webmail container (future)

  Internal:
    Postfix ←→ Dovecot (LMTP)
    Postfix ←→ Rspamd (milter)
    Dovecot ←→ Rspamd (antivirus)
    Rspamd  ←→ Redis
```

All services run inside Docker on a dedicated `containercp-mail` Docker
network.  Ports are published on the host only when the mail module is
enabled.

---

## 2. Provider abstraction

The Mail module interacts with the underlying mail implementation through
a stable interface (`MailProvider`).  The API and UI depend on this
interface, not on the specific implementation.

```
┌──────────────────────────────────────────────────────────┐
│                    API / UI                                │
│  (depends on abstraction, not implementation)              │
└────────────────────┬─────────────────────────────────────┘
                     │
┌────────────────────▼─────────────────────────────────────┐
│                  MailProvider (interface)                  │
│  apply_config()   create_mailbox()   delete_mailbox()     │
│  get_quota()      health_check()     reload()             │
└────────────────────┬─────────────────────────────────────┘
                     │
        ┌────────────┼────────────┐
        ▼            ▼            ▼
┌──────────────┐ ┌──────────┐ ┌──────────┐
│  DockerMail  │ │ External │ │  M365    │
│  Provider    │ │  Relay   │ │ (future) │
│              │ │ Provider │ │          │
│  Postfix +   │ │          │ │          │
│  Dovecot     │ │          │ │          │
└──────────────┘ └──────────┘ └──────────┘
```

This design allows the mail backend to be replaced without API or UI
changes.  The initial implementation uses `DockerMailProvider`
(Postfix + Dovecot).  Future providers can be added for different
backends.

### Project structure

```
libs/mail/
├── MailProvider.h                — interface for mail operations
├── MailDomainManager.h/.cpp      — mail domain CRUD
├── MailboxManager.h/.cpp         — mailbox CRUD
├── AliasManager.h/.cpp           — alias management
├── ForwarderManager.h/.cpp       — forwarder management
├── QuotaManager.h/.cpp           — quota tracking
├── MailViewService.h/.cpp        — enriched API responses
├── providers/
│   └── DockerMailProvider.h/.cpp — Postfix + Dovecot implementation
├── config/
│   ├── MailConfigProvider.h/.cpp — config generation
│   └── MailContainerManager.h/.cpp — Docker lifecycle
└── health/
    └── MailHealthMonitor.h/.cpp  — periodic health checks
```

---

## 3. Data model

### MailDomain

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint64 | Primary key |
| `domain` | string | Domain name (e.g. `example.com`) |
| `mode` | enum | `disabled`, `local`, `external`, `m365` |
| `mx_mode` | enum | `self`, `external`, `m365`, `unknown` |
| `relay_host` | string | External SMTP relay (for `external` mode) |
| `dkim_selector` | string | DKIM DNS selector |
| `dkim_private_key_path` | string | Path to DKIM private key |
| `dkim_public_key_dns` | string | Generated DKIM DNS record text |
| `max_mailboxes` | uint64 | Per-domain mailbox limit |
| `max_aliases` | uint64 | Per-domain alias limit |
| `enabled` | bool | Master switch |
| `catch_all` | string | Catch-all address or empty |
| `created_at` | string | ISO 8601 |
| `updated_at` | string | ISO 8601 |

### Mailbox

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint64 | Primary key |
| `domain_id` | uint64 | FK to `MailDomain` |
| `local_part` | string | Local part (e.g. `user` for `user@domain`) |
| `password_hash` | string | Dovecot-compatible password hash |
| `quota_bytes` | uint64 | Mailbox quota in bytes |
| `quota_messages` | uint64 | Max message count |
| `enabled` | bool | Enable/disable without deleting |
| `forward_to` | string | Forwarding address (empty = deliver locally) |
| `spam_enabled` | bool | Per-mailbox spam filtering |
| `last_login` | string | Last IMAP/POP3 login |
| `created_at` | string | ISO 8601 |

### Alias

| Field | Type | Description |
|-------|------|-------------|
| `id` | uint64 | Primary key |
| `domain_id` | uint64 | FK to `MailDomain` |
| `source` | string | e.g. `info@example.com` |
| `destination` | string | e.g. `user@example.com` or external |
| `enabled` | bool | |

### Domain modes

| Mode | Description | MX handling | Use case |
|------|-------------|-------------|----------|
| `disabled` | Mail not handled by ContainerCP. | No change | Domain exists but mail is managed elsewhere or not used. |
| `local-primary` | ContainerCP is the primary mail server for this domain. All mailboxes are local. | MX → ContainerCP | Company fully hosted on ContainerCP. |
| `external-relay` | An external provider (Google Workspace, Zoho, etc.) is primary. ContainerCP may relay outbound. | MX → external provider | Company uses Google Workspace; ContainerCP only provides local services. |
| `split-m365` | Microsoft 365 is primary, but selected mailboxes exist locally (hybrid split delivery). | MX → M365 (unchanged) | Company uses M365; only technical mailboxes (noreply, monitoring) are local. |

The mode names are chosen to be self-documenting:
- `local-primary` — unambiguous: ContainerCP is the primary
- `external-relay` — describes the relationship: external provider, ContainerCP may relay
- `split-m365` — explicitly names M365 and the split delivery model

### Configuration distinction

ContainerCP clearly separates:

- **Managed config** — files under `/etc/containercp/mail/generated/`
  (Postfix `main.cf`, Dovecot `dovecot.conf`, etc.).  These are
  regenerated by `MailConfigProvider` and must not be edited manually.
- **Custom config** — files under `/etc/containercp/mail/custom/`.
  Administrators can add configuration snippets here.  The generated
  config includes these via `!include` directives.  ContainerCP never
  overwrites files in the `custom/` directory.

---

## 4. API design

### Mail Domains

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/mail/domains` | List mail domains |
| POST | `/api/mail/domains` | Create mail domain |
| DELETE | `/api/mail/domains/<id>` | Remove mail domain |
| PATCH | `/api/mail/domains/<id>` | Update domain settings |

### Mailboxes

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/mail/domains/<id>/mailboxes` | List mailboxes |
| POST | `/api/mail/domains/<id>/mailboxes` | Create mailbox |
| DELETE | `/api/mail/mailboxes/<id>` | Remove mailbox |
| PATCH | `/api/mail/mailboxes/<id>` | Update mailbox settings |
| POST | `/api/mail/mailboxes/<id>/password` | Change password |
| GET | `/api/mail/mailboxes/<id>/quota` | Get quota usage |

### Aliases

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/mail/domains/<id>/aliases` | List aliases |
| POST | `/api/mail/domains/<id>/aliases` | Create alias |
| DELETE | `/api/mail/aliases/<id>` | Remove alias |

### Configuration

| Method | Path | Purpose |
|--------|------|---------|
| POST | `/api/mail/regenerate` | Regenerate all mail configs |
| GET | `/api/mail/config/status` | Show config status (generated vs custom) |

### Health

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/mail/health` | Mail stack health (services, queue, certs, DNS) |

### Recovery

| Method | Path | Purpose |
|--------|------|---------|
| POST | `/api/mail/recover` | Restart/recreate mail containers |
| POST | `/api/mail/reload` | Reload Postfix/Dovecot configs |

---

## 5. Microsoft 365 compatibility

### Detection

When a domain is in `split-m365` mode:

1. MX records are checked: if they point to `mail.protection.outlook.com`
   or `.mail.microsoftonline.com`, the domain is recognized as M365.
2. ContainerCP does NOT change MX records for this domain.
3. Only explicitly created local mailboxes are delivered locally.
4. All other addresses are relayed to M365 via smarthost.

### Split delivery

Postfix handles split delivery via `transport_maps`:

| Recipient | Route |
|-----------|-------|
| `local-user@domain` | Dovecot LMTP (local delivery) |
| `*@domain` | M365 via smarthost (`smtp.outlook.com`) |

### Real-world deployment examples

#### Example A: Company fully hosted on ContainerCP

```
Mode:             local-primary
Mailboxes:        50 (all local)
MX records:      mx.containercp.example
Aliases:         info@ → sales@, support@
DKIM:            managed by ContainerCP
Outbound:        direct from Postfix
```

Architecture matches: ContainerCP is the primary mail server.
All mail is local.  Standard MX, DKIM, SPF setup.

#### Example B: Company using Microsoft 365, a few local technical mailboxes

```
Mode:             split-m365
Mailboxes:        noreply@company.com (local)
                  monitoring@company.com (local)
M365 mailboxes:   ceo@, hr@, sales@, support@, everyone else
MX records:      company-com.mail.protection.outlook.com
                 (unchanged — still points to M365)
Outbound:        local mailboxes → M365 smarthost
                 M365 handles outbound delivery for its own mailboxes
```

Architecture: ContainerCP only hosts technical mailboxes.
Postfix delivers `noreply@` locally, relays `*@company.com` to M365.
Users connect Outlook to M365 as normal.  No MX change needed.

#### Example C: Company using Google Workspace

```
Mode:             external-relay
Mailboxes:        0 local (all in Google Workspace)
Relay host:       smtp.gmail.com:587
MX records:      ASPMX.L.GOOGLE.COM (unchanged)
```

Architecture: ContainerCP does not host any mailboxes for this domain.
If outbound relay is needed, Postfix forwards through Google's SMTP.
No local delivery.  MX stays with Google.

#### Example D: Hybrid migration in progress

```
Mode:             split-m365 → local-primary (transition)
Phase 1:          Create mailboxes on ContainerCP, migrate data
Phase 2:          Change DNS, switch mode to local-primary
Phase 3:          Decommission M365
```

Architecture supports migration without service interruption:
mailboxes can be created locally while M365 is still primary.
When migration is complete, DNS and mode switch atomically.

### Autodiscover

A future stage could provide an Autodiscover endpoint for Outlook
clients, enabling automatic account configuration for both local
and M365 mailboxes.  The architecture must not prevent this.

---

## 6. Health monitoring

### Built from the start

| Check | Method | Frequency |
|-------|--------|-----------|
| Postfix running | `docker exec postfix postfix status` | Every 60s |
| Dovecot running | `docker exec dovecot dovecot status` | Every 60s |
| Redis running | `docker exec redis ping` | Every 60s |
| SMTP port open | `curl telnet://localhost:25` | Every 60s |
| IMAP port open | `curl telnet://localhost:143` | Every 60s |
| Mail queue depth | `docker exec postfix mailq` | Every 300s |
| Cert expiry | Check Postfix/Dovecot cert paths | Every 3600s |
| DNS/MX match | Compare configured vs actual MX | Every 3600s |

Results are exposed via `GET /api/mail/health` and integrated into
`RecoveryManager` (future).

---

## 7. Configuration ownership

### Ownership model

| Directory | Owner | Contents | Regenerated? |
|-----------|-------|----------|-------------|
| `generated/` | ContainerCP | Full config files for Postfix, Dovecot, Rspamd | Yes — always overwritten on regeneration |
| `custom/` | Administrator | Override snippets included by generated configs | Never touched by ContainerCP |
| `state/` | ContainerCP | DKIM keys, SSL certs, runtime state | Managed by specific operations |

### ContainerCP owns (generated/)

Files under `generated/` are the canonical managed configuration.
ContainerCP writes them on:

- Initial module setup
- Domain/mailbox/alias changes
- Explicit `POST /api/mail/regenerate`
- Daemon startup (if enabled)
- Configuration version changes

Administrators must not edit files in `generated/` — changes will be
overwritten.

### Administrators own (custom/)

Files under `custom/` are never read, written, or enumerated by
ContainerCP code.  They exist only for administrator overrides.

The generated config includes custom snippets via `!include`:

```
# In generated/postfix-main.cf:
!include /etc/containercp/mail/custom/postfix-main.cf.d/*.cf
```

This guarantees that administrator modifications survive any
regeneration or upgrade.

### Upgrade guarantees

| Event | generated/ | custom/ | state/ |
|-------|------------|---------|--------|
| Configuration regeneration | Overwritten | Preserved | Preserved |
| Mail module version upgrade | Overwritten (if format changes) | Preserved | Preserved |
| ContainerCP version upgrade | Overwritten (if format changes) | Preserved | Preserved |
| Manual edit by admin | Lost on next regeneration | **Safe — never touched** | N/A |

### Regeneration behavior

When `POST /api/mail/regenerate` is called:

1. Read current domain/mailbox/alias state from ContainerCP managers.
2. Render config templates with current state.
3. Write files to `generated/` (temporary files, then atomic rename).
4. Run `docker exec postfix postfix check` to validate Postfix config.
5. Run `docker exec dovecot doveconf` to validate Dovecot config.
6. If validation fails: roll back generated files, report error.
7. If validation passes: signal Postfix and Dovecot to reload.
8. Administrator custom files in `custom/` are never read or modified.

### Config validation before apply

Before any config is written to the live containers:

1. Render config to temporary files.
2. Copy into the container to a staging location.
3. Run daemon-specific validation command.
4. Only on success: move to live location and trigger reload.

This prevents a bad config from breaking the mail stack.

---

## 8. Implementation stages

Each stage is independently reviewable and leaves the project in a
working state.  Stages build on previous ones but do not require
them to be fully production-ready — each stage is functional on its
own.

### Stage 1a — MailDomain resource (estimated: 2-3 days)

- `MailDomainManager` — CRUD for mail domain records
- Data model with domain modes (`disabled`, `local-primary`,
  `external-relay`, `split-m365`)
- Persistence via `Storage` (pipe-delimited or JSON)
- API: `GET/POST/DELETE /api/mail/domains`
- No Docker containers yet — domain records only
- Tests: CRUD operations, mode validation

*Leaves project in working state:* domains are stored and queryable.
No mail functionality yet, but the resource layer is solid.

### Stage 1b — Mailbox resource (estimated: 3-4 days)

- `MailboxManager` — CRUD for mailbox records
- Password hashing (SHA-512-CRYPT / Dovecot ARGON2)
- Quota tracking (`QuotaManager`)
- API: `GET/POST/DELETE/PATCH /api/mail/domains/<id>/mailboxes`
- API: `POST /api/mail/mailboxes/<id>/password`
- Tests: password hashing, CRUD, quota validation

*Leaves project in working state:* mailboxes exist in ContainerCP's
database.  No mail delivery yet, but all data is prepared.

### Stage 1c — Docker mail stack foundation (estimated: 4-5 days)

- `DockerMailProvider` — implement `MailProvider` interface
- `MailContainerManager` — Docker Compose lifecycle for Postfix + Dovecot
- `MailConfigProvider` — generate Postfix `main.cf` + Dovecot `dovecot.conf`
- Docker Compose generation: Postfix, Dovecot, Redis containers
- Custom config directory structure (`generated/`, `custom/`)
- Basic SMTP authentication (Dovecot SASL with passwd-file)
- API: `POST /api/mail/regenerate`
- `MailViewService` — enriched API responses

*Leaves project in working state:* mail containers run.  Domains and
mailboxes from Stages 1a/1b are used to configure Postfix and Dovecot.
Local delivery works for `local-primary` mode.

### Stage 2a — Aliases and forwarders (estimated: 2-3 days)

- `AliasManager` — alias CRUD
- `ForwarderManager` — forwarder CRUD
- Postfix `virtual_alias_maps` generation
- API: `GET/POST/DELETE /api/mail/domains/<id>/aliases`
- Tests: alias resolution, forwarder delivery

### Stage 2b — TLS, DKIM, security (estimated: 3-4 days)

- TLS/SSL certificates for Postfix and Dovecot
- DKIM key generation and signing (via Rspamd or OpenDKIM)
- DKIM DNS record display in API
- Postfix `transport_maps` for split delivery
- API: certificate management endpoints
- Tests: DKIM signing, TLS configuration

### Stage 3 — External modes and M365 (estimated: 4-5 days)

- `external-relay` mode — Postfix relay host configuration
- `split-m365` mode — M365 split delivery via transport_maps
- MX record validation (DNS lookup, compare with mode)
- Integration with Domain module (DKIM DNS records)
- Autodiscover endpoint (basic, IIS-compatible)
- Tests: transport_maps generation, MX validation

### Stage 4 — Health and recovery (estimated: 3-4 days)

- `MailHealthMonitor` — periodic health checks
- `GET /api/mail/health` — service status, queue, certs, DNS
- Integration with `RecoveryManager` (reload/recreate mail stack)
- Mail queue monitoring (Postfix `mailq` parsing)
- Certificate expiry warnings
- API: `POST /api/mail/recover`, `POST /api/mail/reload`
- Tests: health check failures, recovery integration

### Stage 5 — Webmail and future (estimated: separate epic)

- Webmail container (RainLoop / SnappyMail)
- Spam filtering configuration UI
- Antivirus (ClamAV) integration via Rspamd
- Backup / restore for mail data (Maildir dump + SQL)
- Migration tools (`imapsync` integration)
- Admin UI pages for all mail functions

---

## 9. Future extensibility

| Feature | How the architecture supports it |
|---------|----------------------------------|
| Webmail | Separate container on the mail network; nginx proxy routes `/mail/` |
| Spam filtering | Rspamd included from Stage 1; UI config added later |
| Antivirus | Rspamd supports ClamAV integration — add container, configure |
| DKIM management | `MailConfigProvider` generates DNS records; Domain module displays them |
| DNS automation | Existing `DomainManager` can publish DKIM/SPF/DMARC records |
| Backups | `MailboxManager` exposes mailbox list; backup module can dump maildirs |
| Clustering | Dovecot supports director + shared storage; design uses standard Dovecot config |
| Migration tools | `imapsync` can be invoked via API for per-mailbox migration |

---

## 10. Related documents

- `docs/reverse-proxy-architecture.md` — Reverse Proxy (similar architecture pattern)
- `docs/development/single-source-of-truth.md` — SSOT rules
- `docs/development/api-rules.md` — API design rules
- `docs/runtime-architecture.md` — Runtime subsystem (reused for container management)
- `docs/startup-architecture-review.md` — Startup architecture
