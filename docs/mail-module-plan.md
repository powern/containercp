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

### Component choice rationale

| Component | Role | Why |
|-----------|------|-----|
| **Postfix** | SMTP server (MTA) | Mature, secure, extensible, industry standard |
| **Dovecot** | IMAP/POP3 server | Mature, secure, well-integrated with Postfix, supports virtual users |
| **Rspamd** | Spam/virus filtering, DKIM signing | Modern, fast, Lua-configurable, replaces Amavis+SpamAssassin |
| **Redis** | Queue cache, rate limiting, Rspamd backend | Lightweight, fast, already used in ContainerCP ecosystem |
| **OpenDKIM** | DKIM signing (standalone) | Standard DKIM implementation, or Rspamd handles it |

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

## 2. Project structure

```
libs/mail/
├── MailDomainManager.h/.cpp     — mail domain CRUD
├── MailboxManager.h/.cpp        — mailbox CRUD (via Dovecot API)
├── AliasManager.h/.cpp          — alias management
├── ForwarderManager.h/.cpp      — forwarder management
├── MailConfigProvider.h/.cpp    — config generation for Postfix/Dovecot
├── MailViewService.h/.cpp       — enriched API responses
├── MailContainerManager.h/.cpp  — Docker Compose lifecycle
└── QuotaManager.h/.cpp          — quota enforcement

libs/mail/providers/
├── MailProvider.h               — interface for mail operations
├── DockerMailProvider.h/.cpp    — Docker-based mail stack
├── ExternalMailProvider.h/.cpp  — external relay configuration
└── M365Provider.h/.cpp          — Microsoft 365 integration (future)
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

| Mode | Description | MX handling |
|------|-------------|-------------|
| `disabled` | Mail not handled by ContainerCP | No change |
| `local` | ContainerCP is the primary mail server | MX → ContainerCP |
| `external` | External relay (e.g. Google Workspace) | MX → external |
| `m365` | Microsoft 365 hybrid/split | MX → M365, some local mailboxes |

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

When a domain is in `m365` mode:

1. MX records are checked: if they point to `mail.protection.outlook.com`
   or `.mail.microsoftonline.com`, the domain is recognized as M365.
2. ContainerCP does NOT manage MX records for this domain.
3. Only explicitly created local mailboxes are delivered locally.
4. All other addresses are relayed to M365.

### Split delivery

Postfix handles split delivery via `transport_maps`:

| Recipient | Route |
|-----------|-------|
| `local-user@domain` | Dovecot LMTP (local) |
| `*@domain` | M365 via smarthost |

### Autodiscover

A future stage could provide an Autodiscover endpoint for Outlook
clients, enabling automatic account configuration for both local
and M365 mailboxes.

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

## 7. Custom configuration preservation

```
/etc/containercp/mail/
├── generated/
│   ├── postfix-main.cf
│   ├── postfix-master.cf
│   ├── dovecot.conf
│   └── rspamd.conf
├── custom/
│   ├── postfix-main.cf.d/        (included by generated config)
│   ├── dovecot.conf.d/           (included by generated config)
│   └── rspamd.conf.d/            (included by generated config)
└── state/
    ├── dkim-keys/
    └── ssl/
```

The `custom/` directory is never touched by ContainerCP code.
Administrators add override snippets here.  Generated configs include
these via `!include` directives.

---

## 8. Implementation stages

### Stage 1 — Foundation (estimated: 1-2 weeks)

- MailDomain resource (CRUD + domain modes)
- Mailbox resource (CRUD + Dovecot password hashing)
- Basic Postfix + Dovecot Docker Compose generation
- `MailConfigProvider` generates Postfix/Dovecot configs
- `MailContainerManager` handles Docker lifecycle
- API: `/api/mail/domains`, `/api/mail/domains/<id>/mailboxes`
- Health: basic service checks (containers running)
- Custom config preservation structure

### Stage 2 — Delivery (estimated: 1-2 weeks)

- Alias management
- Forwarder management
- Quota tracking
- SMTP authentication (Dovecot SASL)
- TLS/SSL for Postfix and Dovecot
- DKIM signing (OpenDKIM or Rspamd millt)
- API: aliases, forwarders, quota
- `MailViewService` for enriched responses

### Stage 3 — M365 and external modes (estimated: 1 week)

- Microsoft 365 mode (split delivery via `transport_maps`)
- External relay mode
- MX record validation
- Autodiscover endpoint (basic)
- Integration with Domain module (DKIM DNS records)

### Stage 4 — Health and recovery (estimated: 1 week)

- `GET /api/mail/health` with full diagnostics
- Integration with `RecoveryManager`
- Mail queue monitoring
- Certificate expiry checking
- DNS validation

### Stage 5 — Webmail and future (estimated: 2+ weeks)

- Webmail container (RainLoop / SnappyMail)
- Spam filtering configuration UI
- Antivirus integration
- Backup / restore for mail data
- Migration tools (imapsync)
- Admin UI pages

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
