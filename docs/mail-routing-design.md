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

**Transport maps:**  `domain → lmtp:127.0.0.1:24`

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

```
local-user@domain       lmtp:127.0.0.1:24     (local delivery)
*@domain                smtp:[relay_host]:25  (relay to M365 MX)
```

The wildcard entry catches every non-local recipient and forwards
them to the M365 MX endpoint.  Postfix transport maps are evaluated
in order: specific matches first, wildcard catches the rest.

### Recipient resolution table

| Recipient type | Example | Action |
|----------------|---------|--------|
| Local mailbox | `alice@domain` (in virtual_mailbox_maps) | Deliver to Dovecot LMTP |
| Non-local (M365) | `bob@domain` (not in virtual_mailbox_maps) | Relay to M365 MX |
| Unknown | `nonexistent@domain` | Relay to M365 MX (M365 decides) |

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

---

*Document accompanies Mail Module Stage 3 implementation.*
*See `docs/ADR/ADR-007-m365-split-delivery.md` for architecture decisions.*
