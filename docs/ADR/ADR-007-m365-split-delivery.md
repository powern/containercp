# ADR-007: M365 Split Delivery Routing

## Status

Accepted

## Context

ContainerCP supports `split-m365` mode where a domain's mail is
handled by both Microsoft 365 and a local Postfix/Dovecot stack.

Local mailboxes (e.g. `noreply@company.com`) are delivered to Dovecot.
All other recipients at the same domain must be relayed to M365.

This requires Postfix to distinguish three recipient categories for
the same domain:
1. Known local mailboxes → deliver to Dovecot
2. Known M365 mailboxes → relay to M365
3. Unknown recipients → relay to M365 (M365 decides)

Postfix must also be configured to relay mail for a domain it does
not authoritatively serve (split-m365 domain's MX still points to
M365).

## Decision

### Transport maps with wildcard catch-all

Each `split-m365` domain gets two transport map entries:

```
local-user@domain       lmtp:127.0.0.1:24     (explicit local mailbox)
*@domain                smtp:[relay_host]:25  (wildcard catch-all)
```

Postfix evaluates transport maps top-down.  The explicit (local)
match wins for local mailboxes.  The wildcard catches everything
else and relays it to the M365 MX target.

### relay_host must be configured explicitly

The M365 MX target (`relay_host`) must be provided by the
administrator when setting mode to `split-m365`.  This is typically
the domain's M365 MX endpoint (e.g. `company-com.mail.protection.outlook.com`).

Automatic MX discovery from DNS is deferred to the Health stage
(Stage 4) and is an optional convenience, not a requirement.

### relay_domains in Postfix main.cf

Split-m365 domains are added to Postfix's `relay_domains` parameter.
Without this, Postfix would reject non-local recipients for a domain
it claims via `virtual_mailbox_domains`.

### LocalPrimary relies on virtual_mailbox_domains only

For LocalPrimary mode, transport maps point to LMTP and Postfix
rejects unknown recipients.  No `relay_domains` needed because no
relay occurs.

### ExternalRelay uses relayhost only

For ExternalRelay mode, the domain is not in `virtual_mailbox_domains`
and Postfix simply relays all mail through the configured `relayhost`.

## Alternatives considered

### Option B: LDAP or SQL lookup for recipient verification

Postfix could query Dovecot's auth database to determine whether a
recipient is local before routing.  This would avoid the wildcard
catch-all and allow Postfix to reject unknown recipients locally.

**Not chosen** because:
- Adds a live database dependency to the SMTP path
- More complex to configure and debug
- The wildcard catch-all is simpler and M365 rejects unknowns anyway

### Option C: Virtual alias maps for each M365 mailbox

Each M365 mailbox could be explicitly listed as a virtual alias
pointing to the M365 relay target.  Unknown recipients would be
rejected by Postfix.

**Not chosen** because:
- Requires duplicating M365's mailbox list in ContainerCP
- Would drift from M365's actual mailbox list
- Administrators would need to maintain two directories

### Option D: Two Postfix instances

One Postfix serves local-primary domains, another handles split
delivery.  This isolates routing logic but doubles complexity.

**Not chosen** because:
- Single Postfix with transport maps is simpler
- No operational benefit from two instances
- Port allocation for two SMTP servers is unnecessary complexity

## Consequences

- Split-m365 routing requires only transport maps + relay_domains
- No mailbox list duplication — unknown recipients go to M365
- Administrators must configure relay_host explicitly
- Future DNS-based MX auto-discovery can be added without changing
  the transport map logic
- Postfix log analysis may show relayed recipients for unknowns —
  this is correct behavior (M365 decides)

## Verified Postfix behavior

### transport_maps lookup order

Postfix transport(5) specifies the following lookup fallback:

1. `user@domain` — full recipient address
2. `user@.domain` — full address with partial domain lead-in
3. `.domain` — partial domain (organizational domain)
4. `domain` — domain only

When a recipient address does not match an exact `user@domain` entry,
Postfix drops the local part and retries with just the domain.

This makes the bare-domain catch-all (`domain smtp:[relay]`) correct:
local recipients match the explicit `user@domain` entry; all others
fall through to the domain-only entry.

### relay_domains interaction

Postfix rejects mail for domains it does not serve.  `relay_domains`
tells Postfix: "accept mail for these domains even if they are not
local, and relay them via transport maps or relayhost."

Without `relay_domains`, Postfix would reject non-local recipients for
both ExternalRelay and SplitM365 domains.

SplitM365 domains appear in both `virtual_mailbox_domains` (enabling
local mailbox lookup) and `relay_domains` (enabling relay of non-local
recipients).  Postfix handles both lists independently — they are not
mutually exclusive.

### ExternalRelay does not use relayhost

Postfix's `relayhost` parameter specifies a global default relay
destination.  It is NOT set by ContainerCP.  Instead, each
ExternalRelay domain uses a per-domain transport map entry:

```
domain.com smtp:[relay_one.com]
other.com  smtp:[relay_two.com]
```

This allows multiple ExternalRelay domains with different relay hosts
to coexist on the same Postfix instance.  The global `relayhost` is
not needed when transport maps provide per-domain routing.

### Map type: texthash only

All Postfix maps (transport_maps, virtual_mailbox_maps) use
`texthash`, a simple hash lookup.  No regex, PCRE, or LDAP maps are
used.  This means:

- All keys must match exactly — no pattern-based lookup
- The domain-only fallback is Postfix's native behavior, not a
  texthash feature
- texthash is the simplest and fastest Postfix map type
