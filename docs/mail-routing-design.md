# Mail Routing Design — External Relay & Split M365

## Overview

This document describes the expected mail flow for the three active
domain modes (LocalPrimary, ExternalRelay, SplitM365).  Disabled
domains are not handled by ContainerCP.

The server runs a single Postfix instance that handles all domains.
Routing decisions are made per-recipient based on transport maps and
domain mode.

---

## 1. LocalPrimary — ContainerCP is the primary mail server

```
Incoming SMTP (port 25)
    │
    ▼
Postfix (containercp-mail-postfix)
    │
    ├── Recipient is in virtual_mailbox_maps → local mailbox
    │       ▼
    │   Dovecot LMTP (port 24) → Maildir delivery
    │
    └── Recipient NOT in virtual_mailbox_maps → unknown user
            ▼
        Postfix rejects (550 User unknown)
```

**Outbound:**  Postfix delivers directly to destination MX.

### Transport maps

```
domain                      lmtp:127.0.0.1:24     (all recipients local)
```

LocalPrimary has no transport map entry — local delivery uses
`virtual_transport = lmtp:127.0.0.1:24` (set in main.cf).
Unknown recipients are rejected (domain not in `relay_domans`).

---

## 2. ExternalRelay — External provider handles all mail

```
Incoming SMTP (port 25)
    │
    ▼
Postfix (containercp-mail-postfix)
    │
    └── All recipients → relay to external smarthost
            ▼
        smtp:[relay_host]:587  (STARTTLS, may use auth)
```

**Mailboxes:**  Zero local mailboxes for this domain.  All recipients
are relayed to the configured `relay_host`.

**Outbound:**  Postfix relays through the configured smarthost.
No direct delivery.

**Transport maps:**  `domain → smtp:[relay_host]`

**Postfix main.cf:**
```
relayhost = [relay_host]
```

---

## 3. SplitM365 — Microsoft 365 split delivery

### Inbound flow

```
Incoming SMTP (port 25)
    │
    ▼
Postfix (containercp-mail-postfix)
    │
    ├── Recipient in virtual_mailbox_maps → local mailbox
    │       ▼
    │   Dovecot LMTP (port 24) → Maildir delivery
    │
    └── Recipient NOT in virtual_mailbox_maps → relay to M365
            │
            ▼
        smtp:[relay_host]:25  (unauthenticated relay)
            │
            ▼
        M365 inbound connector → M365 mailbox
```

**MX records:**  Remain unchanged — still point to
`<domain>.mail.protection.outlook.com`.

**Key principle:**  Postfix only delivers explicit local mailboxes.
Everything else is relayed to the M365 MX target.  No recipient is
rejected — unknown recipients are forwarded to M365, which either
delivers or rejects.

### Outbound flow (from local mailboxes)

```
Local mailbox sends mail via SMTP submission (port 587)
    │
    ▼
Postfix (authenticated SASL)
    │
    └── Authenticated submission → relay to M365 smarthost
            ▼
        smtp:[relay_host]:587  (STARTTLS + auth)
            │
            ▼
        M365 → delivery to final destination
```

Local mailboxes do NOT deliver directly.  All outbound mail goes
through the M365 smarthost to ensure SPF/DKIM alignment.

### Transport maps

Only one transport entry per split-m365 domain — the SMTP relay for
non-local recipients.  Local delivery uses `virtual_transport =
lmtp:127.0.0.1:24` (global in main.cf):

```
domain                      smtp:[relay_host]:25  (non-local catch-all)
```

Postfix resolves each recipient:
1. Recipient in `virtual_mailbox_maps` → deliver via `virtual_transport`
   (LMTP to Dovecot).
2. Recipient NOT in `virtual_mailbox_maps` → `transport_maps` domain
   match → relay to M365 MX.

### Recipient resolution table

| Recipient type | Example | Action |
|----------------|---------|--------|
| Local mailbox | `alice@domain` (in virtual_mailbox_maps) | `virtual_transport` → LMTP → Dovecot |
| Non-local (M365) | `bob@domain` (not in virtual_mailbox_maps) | Transport map catch-all → SMTP → M365 MX |
| Unknown | `nonexistent@domain` | Transport map catch-all → SMTP → M365 MX (M365 decides) |

---

## 4. Disabled — Mail not handled

```
Incoming SMTP (port 25)
    │
    ▼
Postfix
    │
    └── Domain not in virtual_mailbox_domains
            ▼
        Postfix rejects (550 Mailbox not found)
```

No transport map entry is generated.  Postfix does not accept mail
for this domain.

---

## 5. relay_host validation

| Mode | relay_host required | Notes |
|------|---------------------|-------|
| Disabled | No | Ignored |
| LocalPrimary | No | Ignored |
| ExternalRelay | Yes | Error if empty |
| SplitM365 | Yes | Error if empty |

If `relay_host` is changed to empty while mode requires it, the
PATCH endpoint returns a validation error.  The mode must be changed
to one that does not require `relay_host` before clearing it.

---

## 6. Postfix configuration per mode

### ExternalRelay

```ini
relayhost = [relay_host]
# Optional: smtp_sasl_auth_enable for authenticated relay
```

The domain is NOT added to `virtual_mailbox_domains`.  No local
mailboxes are expected.

### SplitM365

```ini
relay_domains = domain1, domain2
# Postfix must be told these domains are OK to relay for
# (non-local recipients of a split-m365 domain are relayed)
```

The domain IS added to `virtual_mailbox_domains` so local mailbox
lookups work.

Transport map has both the specific local entries and a wildcard
catch-all for each domain.

---

## 7. Sequence diagram (SplitM365 inbound)

```
Sender                Postfix              Dovecot              M365
  │                     │                    │                    │
  │── SMTP ────────────→│                    │                    │
  │                     │                    │                    │
  │                     ├── Is recipient in virtual_mailboxes?    │
  │                     │                    │                    │
  │                     ├── Yes: LMTP ──────→│                    │
  │                     │                    │── Maildir ────→ disk
  │                     │                    │                    │
  │                     ├── No: transport map wildcard matches   │
  │                     │── SMTP ───────────────────────────────→│
  │                     │                    │                    │
  │                     │                    │              M365 delivers
```

## Verified Postfix assumptions

### transport_maps lookup

Postfix transport(5) specifies the lookup fallback chain:
1. `user@domain` — full address match
2. `user@.domain` — with partial domain lead-in
3. `.domain` — organizational domain
4. `domain` — bare domain

Domain-only entries serve as catch-all for all non-local recipients
at that domain.  Specific `user@domain` entries take priority.

### relay_domains

Postfix will reject mail for domains it does not serve.  Both
ExternalRelay and SplitM365 domains must appear in `relay_domains`
so Postfix accepts mail for them.  SplitM365 domains additionally
appear in `virtual_mailbox_domains` (for local mailbox lookups).

### No global relayhost

ContainerCP never sets Postfix's global `relayhost`.  All per-domain
relay routing is handled by transport maps.  This allows multiple
ExternalRelay domains with different relay hosts to coexist.

### Map type

All maps use `texthash` (simple hash).  No regex or PCRE maps.
The domain-only fallback is native Postfix behavior, not a map
feature.

---

*Document accompanies Mail Module Stage 3 implementation.*
*See `docs/ADR/ADR-007-m365-split-delivery.md` for architecture decisions.*
