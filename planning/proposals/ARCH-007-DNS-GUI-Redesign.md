# DNS GUI Redesign — Informational Domain Dashboard

**Status:** Draft  
**Author:** ContainerCP Architecture  
**Version:** 5  
**Related Epic:** DNS GUI Redesign  

---

## Problem

The current Domains page (`/domains`) is a flat table that shows domain records with basic metadata (type, site, SSL status). The DNS column always shows "Unknown" — no actual DNS checks are performed. There is no domain detail page, no recommendations, no mail detection, and no health analysis.

Administrators currently must:
1. Open an external DNS tool to check what records exist
2. Cross-reference ContainerCP's mail, SSL, and site configuration manually
3. Consult external documentation for recommended DNS records (SPF, DKIM, DMARC, MTA-STS, etc.)

This creates friction and forces context switching.

---

## Motivation

This proposal aligns with the Product Vision goal of creating **"a modern, clean interface"** for system administrators. Instead of being yet another DNS editor, the Domains section becomes the **central information hub** — the first place an administrator looks to understand a domain's health.

Admin opens a domain → within roughly one minute understands:
- What is **configured correctly**
- What is **missing** (not yet generated or not yet published)
- What is **misconfigured** (published but wrong value)
- **Why** the system considers it an error or warning (with evidence)
- What the **correct DNS record** looks like (Host, Value, FQDN, Full Record)
- What action is needed to **fix** the issue
- Mail, SSL, and HTTP status at a glance

All without opening third-party instructions, external services, or separate documentation.

---

## Current Architecture

### Domain subsystem (`libs/domain/`)
- `Domain` resource: `id`, `fqdn`, `site_id`, `type`, `target`, `ssl_enabled`, `enabled`
- `DomainManager`: CRUD operations
- `DomainViewService`: builds enriched JSON with site name + SSL status
- API: `GET /api/domains` (enriched list), `POST /api/domains/remove`

### DNS-related code — what DOES exist
The codebase contains NO DNS zone editor, NO DNS provider interface, NO DNS record CRUD. All DNS-adjacent code is limited to:

| Location | What it does |
|----------|-------------|
| `libs/profile/ProfileType.h` | `enum ProfileType::DNS` — declared for future, no implementation |
| `libs/ssl/CertificateProvider.h` | `supports_dns_challenge()` — returns `false` (no DNS-01 ACME) |
| `libs/mail/DkimManager.h` | DKIM key generation → returns TXT value string (not published) |
| `libs/mail/MailDomain.h` | `dkim_public_key_dns` field — stores the DKIM TXT record value |
| `libs/storage/Storage.cpp:415,460` | Pipe-delimited persistence of `dkim_public_key_dns` |
| `libs/api/ApiServer.cpp:1687-1706` | `POST /api/mail/domains/<id>/dkim/generate` — returns `dns_record` string |
| `libs/runtime/RuntimeSynchronizer.h:14` | Comment mentioning future DNS sync handler |
| `libs/mail/providers/DockerMailProvider.cpp:308` | Postfix container DNS resolver (`nameserver 8.8.8.8`) — for SMTP, NOT for serving zones |
| `tests/test_runtime_sync.cpp:63-81` | Test stub for a `"dns"` sync handler |

### What does NOT exist
- **No `libs/dns/` directory** — `ls libs/` confirms zero DNS module
- **No DNS resolution code** — no `dig`, `drill`, `getaddrinfo`, or DoH client anywhere
- **No DNS provider interface** — nothing that reads or writes to an external DNS service
- **No API for DNS checking** — no `GET /api/domains/<domain>/dns-check`
- **No expected-vs-actual comparison** — system cannot tell if a generated DKIM record is actually published in DNS

### Current Web UI DNS column
```javascript
// web/app.js:872
{label:'DNS', html:() => '<span class="badge badge-info">Unknown</span>'}
```
Always shows "Unknown" — zero DNS logic exists in the frontend.

### Roadmap status
```
DNS-001: DNS resource and manager  ⬜ Planned (v0.6)
DNS-002: DNS provider interface     ⬜ Planned (v0.6)
DNS-003: DNS CLI and REST API       ⬜ Planned (v0.6)
DNS-004: DNS Web UI pages           ⬜ Planned (v0.6)
```
All tasks are still planned with the original scope of a full DNS zone editor — **which is no longer the goal**.

### Mail DKIM
- Fully implemented in `libs/mail/DkimManager`
- API: `POST /api/mail/domains/<id>/dkim/generate`
- Returns DNS TXT record value with copy buttons in UI (Host, Value, FQDN, Full Record)
- The record is stored locally but **never published** to DNS automatically

---

## Proposed Architecture

### Core principle

> **This is NOT a DNS Editor. This is the Domain Diagnostic Center.**
>
> ContainerCP does NOT:
> - Create, edit, or delete DNS zones
> - Publish DNS records to any provider (Cloudflare, Route53, BIND, PowerDNS, etc.)
> - Integrate with external DNS APIs
> - Act as a DNS server
>
> ContainerCP ONLY provides **diagnostic information**:
> - Generates the **correct DNS record content** (values, hostnames, policies)
> - Shows **Host, Value, FQDN, and Full Record** for one-click copy to the admin's DNS provider
> - Checks via **public DNS resolution** whether the record is actually published
> - Compares **Configured** (what ContainerCP expects) vs **Published** (what DNS returns)
> - **Explains every discrepancy** with a clear reason and raw data for advanced admins
> - Allows **re-checking** with one click

### Terminology

| Term | Meaning |
|------|---------|
| **Configured** | The value stored or generated inside ContainerCP (e.g., DKIM key, SPF template, SSL domain). This is the **expected** value. |
| **Published** | The value actually found in public DNS via live resolution. This is the **actual** value. |
| **Match** | Configured and Published values are identical. |
| **Mismatch** | Configured and Published values differ. |
| **Not Published** | Configured value exists but nothing found in DNS. |
| **Not Configured** | No expected value in ContainerCP (e.g., no MailDomain). |
| **Evidence** | The raw or parsed DNS response that led to the status conclusion. |

### Domain Diagnostic Center — ultimate goal

This module is not a DNS Editor. Its full name is:

> **Domain Diagnostic Center**

Admin opens a domain → within roughly **one minute** understands:

| Question | Answer provided by |
|----------|-------------------|
| Is this domain working? | Health Score + overview dashboard |
| What DNS records exist? | Tab 2: DNS Records — live DNS check |
| Are mail records correct? | Tab 3: Mail — Configured vs Published + evidence |
| Is SSL properly configured? | Tab 4: Security + SSL status |
| What is missing or wrong? | Status indicators: Match / Mismatch / Not Published |
| Why is it wrong? | Evidence panel: Expected, Actual, Reason, Raw DNS |
| How do I fix it? | One-click copy of the correct record + clear action text |
| Is it fixed now? | [Check Again] — re-runs the check |

All without external tools, documentation, or service switching.

### Page structure

#### 1. Domain List (`/domains`) — Enhanced table

The current flat table is enhanced with live-status columns:

| Column | Data source | Description |
|--------|-------------|-------------|
| Domain | `GET /api/domains` | Clickable → domain detail page |
| Type | `GET /api/domains` | Badge: primary/alias/redirect/wildcard |
| Site | `GET /api/domains` | Linked site name |
| DNS Health | `GET /api/domains/<id>/dns-check` | Badge: OK / Warning / Error / Unknown |
| Mail | `GET /api/mail/domains` + `GET /api/sites/<id>/mail-status` | Badge: Active / Not configured |
| SSL | `GET /api/domains` (already enriched) | Badge: Active/Disabled/Expired/etc |
| HTTP | `GET /api/runtime/<site_id>` | Badge: Online/Offline/Unknown |
| Health Score | Computed from all applicable components | Percentage (0–100) + letter grade |
| Actions | — | Open, Copy domain, View detail, Remove |

Each domain row shows a **Health Score** indicator — a small colored dot plus percentage. The score is **context-aware**: components that are not applicable to a domain are excluded from the calculation.

Health Score is computed **entirely on the frontend** from existing API responses. See the dedicated Health Score Model section for the full scoring algorithm.

The DNS Health column is fetched asynchronously per visible row and cached for 60 seconds. A small refresh icon appears when data is stale.

#### 2. Domain Detail (`/domains/<id>`) — NEW PAGE

This is the main informational dashboard. Layout uses the existing tab system (`style.css` already has `.tabs` and `.tab` classes).

**Header bar:**
```
← Domains  /  example.com        [Open in browser] [Copy domain] [Remove]
Health Score: 85%  (Good)
```

**Tabs:**

### Tab 1: Overview

Summary of everything at a glance. Each line shows the DNS publication status for expected records.

```
┌─────────────────────────────────────────────────────────────┐
│  DNS Records Check (from live DNS resolution)               │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ ✅ A       → 192.168.1.1          TTL: 3600            ││
│  │ ⚠️ AAAA   → Not configured        (recommended: IPv6)  ││
│  │ ✅ MX      → mail.example.com      Priority: 10         ││
│  │ ⚠️ SPF     → Not configured        (required for mail)  ││
│  │ ❌ DKIM    → Generated but NOT     [Copy to publish]    ││
│  │              published in DNS                           ││
│  │ ❌ DMARC   → Not configured                              ││
│  └─────────────────────────────────────────────────────────┘│
│                                              [Check Again]  │
│  Quick Actions: [Copy All Records] [DMARC Wizard]           │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────┬─────────────────────────────┐
│  Mail Status                │  SSL Status                  │
│  Mail Domain: example.com   │  Certificate: Active         │
│  Mode: Local Primary        │  Provider: Let's Encrypt     │
│  DKIM: ✅ Generated         │  HTTPS: Enabled              │
│  DMARC: ⚠️ Quarantine mode  │  Redirect: HTTP→HTTPS        │
│  Mailboxes: 3               │  Expires: Oct 6, 2026        │
└─────────────────────────────┴─────────────────────────────┘
```

### Tab 2: DNS Records

Shows DNS resolution results with the **Configured vs Published** distinction and **evidence** for every non-ok status.

Each row displays:
- **Type** — A, AAAA, MX, TXT, CNAME, NS
- **Name** — the DNS name (host)
- **Configured** — what ContainerCP expects (or "—" if no expectation)
- **Published** — what public DNS actually returns (or "Not found")
- **Status** — Match / Mismatch / Not Published / Not Configured
- **TTL** — from DNS response
- **Actions** — Copy buttons + Show Details

```
DNS Records for example.com

┌─────┬───────┬──────────────────┬──────────────────────────┬─────────┬──────┬──────────────────────┐
│ Sta │ Type  │ Name             │ Configured               │ Publish │ TTL  │ Actions              │
│ tus │       │                  │                          │ ed      │      │                      │
├─────┼───────┼──────────────────┼──────────────────────────┼─────────┼──────┼──────────────────────┤
│  ✅ │ A     │ @                │ (no config expected)     │ 192.168 │ 3600 │ [Copy Value]         │
│     │       │                  │                          │ .1.1    │      │                      │
│  ⚠️ │ AAAA  │ @                │ (no config expected)     │ Not     │ —    │ [Why?]               │
│     │       │ (info)           │                          │ found   │      │ IPv6 recommended     │
│  ✅ │ MX    │ @                │ (no config expected)     │ mail.ex │ 3600 │ [Copy Value]         │
│     │       │                  │                          │ ample.  │      │                      │
│     │       │                  │                          │ com (10)│      │                      │
│  ❌ │ TXT   │ dkim._domainkey  │ v=DKIM1; k=rsa; p=...   │ Not     │ —    │ [Copy H][Copy V]     │
│     │       │ .example.com     │                          │ found   │      │ [Copy F][Copy Full]  │
│     │       │                  │                          │         │      │ [Why?]               │
│  ⚠️ │ TXT   │ _dmarc           │ p=reject                 │ p=none  │ 3600 │ [Copy Record]        │
│     │       │ .example.com     │                          │         │      │ [Show Details]       │
└─────┴───────┴──────────────────┴──────────────────────────┴─────────┴──────┴──────────────────────┘
                                                                    [Check Again]
```

#### Configured vs Published logic

| Record type | Configured source | Published source |
|-------------|------------------|-----------------|
| A | No expected value in ContainerCP | DNS A lookup |
| AAAA | No expected value (informational) | DNS AAAA lookup |
| MX | No expected value in ContainerCP | DNS MX lookup |
| SPF | Template: `v=spf1 mx ~all` (generated when MailDomain exists) | DNS TXT lookup at `@` |
| DKIM | `MailDomain::dkim_public_key_dns` | DNS TXT lookup at `<selector>._domainkey.<domain>` |
| DMARC | Value from DMARC Wizard selection (or empty) | DNS TXT lookup at `_dmarc.<domain>` |
| MTA-STS | Template: `v=STSv1; id=...` | DNS TXT lookup at `_mta-sts.<domain>` |
| Autodiscover | Template: autodiscover URL | DNS SRV lookup |
| CAA | Template for Let's Encrypt | DNS CAA lookup |

#### Evidence / Explain Why

For every record where **Status ≠ Match**, a `[Why?]` or `[Show Details]` button is available. Clicking it opens an inline expandable section:

```
▼ Why is my DKIM record showing "Not Published"?

  Configured (ContainerCP)
    v=DKIM1; k=rsa; p=MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQC...

  Published (public DNS)
    (not found)

  Reason
    No TXT record exists at dkim._domainkey.example.com.
    ContainerCP generated the DKIM key pair, but the TXT record
    has not been added to your DNS provider.
    Copy the record above and add it to your DNS zone.

  DNS Response Details (c-ares)
    Query: dkim._domainkey.example.com TXT
    Status: NXDOMAIN (domain does not exist)
    No records found

  [Copy Record] [Dismiss]
```

Another example — DMARC Mismatch:

```
▼ Why is my DMARC record showing "Mismatch"?

  Recommended (from DMARC Wizard)
    v=DMARC1; p=reject; rua=mailto:dmarc@example.com

  Published (public DNS)
    v=DMARC1; p=none; rua=mailto:dmarc@example.com

  Reason
    The policy (p=) field differs.
    Recommended: p=reject (block failing emails)
    Published:   p=none (monitor only, no action)
    You selected "Reject" in the DMARC Wizard, but your DNS
    still has the old "Monitor" policy.

  DNS Response Details (c-ares)
    Query: _dmarc.example.com TXT
    Status: NOERROR
    Record: "v=DMARC1; p=none; rua=mailto:dmarc@example.com" (ttl=3600)

  [Copy Reject Record] [Dismiss]
```

Each evidence panel contains:
1. **Expected (Configured)** — the value ContainerCP generated or expects
2. **Actual (Published)** — the value found in DNS
3. **Reason** — human-readable explanation of why the status is not OK
4. **DNS Response Details** — structured c-ares output for experienced administrators
5. **Action button** — one-click copy of the correct record

#### Copy helpers

Each DNS record has context-appropriate copy buttons:

| Record | Buttons |
|--------|---------|
| A, AAAA, MX | `[Copy Value]` |
| SPF | `[Copy Record]` |
| DKIM | `[Copy Host]` `[Copy Value]` `[Copy FQDN]` `[Copy Full]` |
| DMARC | `[Copy Record]` `[Copy with RUA]` |
| MTA-STS | `[Copy TXT]` `[Copy Policy Template]` |

### Tab 3: Mail

Shows mail configuration status. Integrates with the existing Mail module.

**Layout depends on whether a MailDomain exists:**

**Scenario A — MailDomain exists:**
```
┌──────────────────────────────────────────────────────────────────────┐
│  Mail Domain: example.com                                            │
│  Mode: Local Primary                                                 │
│  Status: ✅ Active                                                    │
│                                                                      │
│  ┌─ Required Records ─────────────────────────────────────────────┐  │
│  │ ✅ MX     → mail.example.com (10)   Published: ✅ Match       │  │
│  │ ✅ SPF    → v=spf1 mx ~all         Published: ⚠️  Missing    │  │
│  │          [Copy SPF Record]         [Why?]                      │  │
│  │ ✅ DKIM   → Generated              Published: ❌  Not found   │  │
│  │          [Copy H][Copy V][Copy F][Copy Full]  [Why?]          │  │
│  │ ⚠️ DMARC  → p=reject               Published: ⚠️  Mismatch   │  │
│  │          [Copy Record]             [Show Details]              │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  ┌─ Recommended Records ─────────────────────────────────────────┐  │
│  │ ℹ️ Autodiscover → Not configured   [Copy Record]               │  │
│  │ ℹ️ MTA-STS     → Not configured   [Copy TXT] [Copy Policy]    │  │
│  │ ℹ️ TLS-RPT     → Not configured   [Copy Record]               │  │
│  │ ℹ️ CAA         → Not configured   [Copy Record]               │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                                                                      │
│  Mailboxes: 3       Aliases: 2                                       │
│  PHP Mail: ✅ Enabled for WordPress                                  │
└──────────────────────────────────────────────────────────────────────┘
```

Each record line shows:
1. **Configured** — the value ContainerCP generated or expects
2. **Published** — the value found in public DNS (with status: Match / Missing / Mismatch)
3. **Action** — copy buttons and `[Why?]` / `[Show Details]` for non-ok statuses

This gives the admin immediate visibility: "DKIM is generated (Configured) but I forgot to publish the TXT record (Published: Not found)."

**Scenario B — No MailDomain (clean state):**
```
┌────────────────────────────────────────────────────────────┐
│  Mail                                                        │
│  ℹ️  Mail service is not configured for this domain.         │
│  The following records are NOT shown as errors because       │
│  email is not in use: MX, SPF, DKIM, DMARC.                 │
│                                                              │
│  [Enable Mail for this Domain] → navigates to Mail section   │
└────────────────────────────────────────────────────────────┘
```

This is a **critical UX requirement**: when no MailDomain exists, MX/SPF/DKIM/DMARC are not shown as errors. Instead, a clean informational banner tells the admin that mail is not in use.

### Tab 4: Security

```
┌──────────────────────────────────────────────────────────────────┐
│  DMARC Policy                                                    │
│  Recommended: p=reject  |  Published: p=none  |  Status: ⚠️ Mismatch │
│                                                                  │
│  [Show Details] [DMARC Wizard] [Check Again]                      │
│                                                                  │
│  ▼ Why is my DMARC record showing "Mismatch"?                    │
│                                                                  │
│    Recommended (from DMARC Wizard)                              │
│      v=DMARC1; p=reject; rua=mailto:dmarc@example.com           │
│                                                                  │
│    Published (public DNS)                                        │
│      v=DMARC1; p=none; rua=mailto:dmarc@example.com             │
│                                                                  │
│    Reason                                                        │
│      The policy (p=) field differs.                              │
│      Recommended: p=reject (block failing emails)                │
│      Published:  p=none (monitor only, no action)                │
│      You selected "Reject" in the DMARC Wizard, but your DNS     │
│      still has the old "Monitor" policy. Update the TXT record   │
│      at _dmarc.example.com to activate the new policy.           │
│                                                                  │
│    Raw DNS Response (dig)                                        │
│      _dmarc.example.com. 3600 IN TXT "v=DMARC1; p=none; ..."    │
│                                                                  │
│    [Copy Reject Record]  [Dismiss]                               │
│                                                                  │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────┐   │
│  │    Monitor        │  │   Quarantine     │  │   Reject     │   │
│  │                   │  │   (Recommended)  │  │              │   │
│  │  p=none           │  │  p=quarantine    │  │  p=reject    │   │
│  │                   │  │                  │  │              │   │
│  │  No action taken  │  │  Tag as spam     │  │  Block       │   │
│  │  on failing msgs  │  │  in quarantine   │  │  delivery    │   │
│  └──────────────────┘  └──────────────────┘  └──────────────┘   │
│                                                                  │
│  ⚠️  Start with p=none to monitor, then escalate                 │
│  to quarantine after 1-2 weeks.                                  │
│                                                                  │
│  ───────────────────────────────────────────────────────────    │
│                                                                  │
│  MTA-STS (RFC 8461)                                              │
│  ℹ️  Ensures TLS is used for mail delivery.                      │
│  Requires: _mta-sts.example.com TXT + policy file                │
│  Published: Not found  [Why?]  [Copy TXT]  [Copy Policy]        │
│                                                                  │
│  CAA Record                                                      │
│  ℹ️  Certification Authority Authorization lets you               │
│  specify which CAs can issue certificates.                       │
│  Published: Not found  [Why?]  [Copy Record for LE]              │
│                                                                  │
│  TLS-RPT                                                         │
│  ℹ️  TLS-RPT sends delivery failure reports to your email.       │
│  Published: Not found  [Copy Record]                             │
└──────────────────────────────────────────────────────────────────┘
```

The **DMARC Wizard** is a three-card selector that generates the correct TXT record. It does NOT publish the record — it only shows the value for the admin to copy.

### Tab 5: Health

Shows the detailed Health Score breakdown. The score adapts to what is applicable for the domain.

```
┌──────────────────────────────────────────────────────────────────┐
│  Health Score: 92/100 (Excellent)            [Check Again]       │
│  9 applicable • 2 n/a (mail inactive) • 8 ok • 1 ⚠️ • 0 ❌      │
│                                                                  │
│  ┌─ Check ─────────┬───┬─────────┬───────────┬────────────────┐  │
│  │ Check            │ S │ Weight  │ Configured│ Published      │  │
│  ├──────────────────┼───┼─────────┼───────────┼────────────────┤  │
│  │ A record         │ ✅│ req     │ —         │ 192.168.1.1   │  │
│  │ AAAA (IPv6)      │ ⚠️│ info    │ —         │ Not found     │  │
│  │                  │   │ (no pen)│           │ [Why?]        │  │
│  │ MX               │ ✅│ req     │ —         │ mail.ex.com   │  │
│  │ SPF              │ ✅│ req     │ v=spf1..  │ v=spf1..      │  │
│  │ DKIM             │ ❌│ req     │ v=DKIM1.. │ Not found     │  │
│  │                  │   │         │           │ [Why?]        │  │
│  │ DMARC            │ ⚠️│ req     │ p=reject  │ p=none        │  │
│  │                  │   │         │           │ [Show Details]│  │
│  │ SSL Certificate  │ ✅│ req     │ example.. │ Active        │  │
│  │ HTTP Reachable   │ ✅│ req     │ example.. │ 200 OK        │  │
│  │ MTA-STS          │ ⚠️│ rec     │ v=STSv1.. │ Not found     │  │
│  ├──────────────────┼───┼─────────┼───────────┼────────────────┤  │
│  │ Total            │   │         │           │ 92/100        │  │
│  └──────────────────┴───┴─────────┴───────────┴────────────────┘  │
│                                                                  │
│  Last checked: 2 minutes ago                                     │
│                                                                  │
│  Future: propagation check, TTL analysis,                         │
│  certificate transparency, DNSSEC validation                      │
└──────────────────────────────────────────────────────────────────┘
```

Each check line shows:
- **Status** — ✅ ok, ⚠️ warning, ❌ error, ⬜ n/a (not applicable)
- **Weight** — `req` (required), `rec` (recommended), `info` (informational — no penalty)
- **Configured** — what ContainerCP expects (or `—` if no expectation)
- **Published** — what DNS returns (with `[Why?]` for non-ok statuses)
- **Score** — earned / max (visible in the Total row)

See the Health Score Model section for the complete algorithm.

**Legend:**
- 📌 **Required checks** — full weight, impact score (A, MX, SPF, DKIM, DMARC, SSL, HTTP)
- 💡 **Recommended checks** — small weight, minor or no penalty (MTA-STS, CAA, Autodiscover)
- ℹ️ **Informational checks** — no score impact, purely advisory (IPv6, TLS-RPT)
- ⬜ **Not applicable** — excluded from calculation entirely

### Evidence / Explain Why pattern

Every Warning or Error in the Domain Diagnostic Center must explain **why** the system reached that conclusion. The evidence panel is an inline expandable section containing:

1. **Configured** (Expected) — the value ContainerCP generated or expects
2. **Published** (Actual) — the value found in public DNS
3. **Reason** — a human-readable explanation of the discrepancy
4. **DNS Response Details** — structured c-ares output for experienced administrators
5. **Action button** — one-click copy of the correct record

**Interaction model:**
- Non-ok records show `[Why?]` (for simple cases) or `[Show Details]` (for complex cases with multiple fields)
- Clicking toggles an inline expandable `<div>` below the record row
- Only one evidence panel can be open at a time (accordion pattern)
- The panel auto-closes when `[Check Again]` is clicked (fresh results)

**Reason templates (frontend-generated):**

| Scenario | Reason template |
|----------|----------------|
| DKIM: Not Published | ContainerCP generated the DKIM key pair, but no TXT record exists at `<selector>._domainkey.<domain>`. Add the record to your DNS zone. |
| DKIM: Mismatch | The public key in DNS differs from the one ContainerCP generated. This may happen if the key was regenerated without updating DNS. |
| SPF: Missing | No SPF record found. Without SPF, spammers can forge emails from your domain. Add `v=spf1 mx ~all` to allow your mail servers only. |
| DMARC: Mismatch | The policy (p=) field differs. Recommended: `<value>`, Published: `<value>`. Update your DMARC TXT record at `_dmarc.<domain>`. |
| MX: Not found | No MX record found. Email delivery to this domain will fail. |
| SSL: Expiring | Certificate expires in `<N>` days. Renew via SSL section. |
| CAA: Missing | No CAA record found. Any CA can issue certificates for your domain. Add a CAA record to restrict to Let's Encrypt. |

The reason text is generated by the frontend based on the record type, configured value, published value, and applicable context.

### "Check Again" pattern

Every section that displays DNS check results has a `[Check Again]` button. Clicking it:
1. Sets the section to "Checking..." state (with a loading spinner)
2. Calls `GET /api/domains/<domain>/dns-check?types=...` (with relevant record types)
3. Replaces the stale results with fresh data
4. Updates the last-checked timestamp

The frontend caches DNS results per domain for 60 seconds. "Check Again" bypasses the cache.

---

## Health Score Model

### Core principle

> **The Health Score is context-aware. Inapplicable checks are excluded. Required checks penalise more than recommended ones. Informational checks never reduce the score.**

### Applicability detection

Each check has an applicability rule. If the rule returns `false`, the check is marked `not_applicable` and excluded entirely from the score calculation (both numerator and denominator).

> **Important note on `site_id`:** ContainerCP uses `site_id = 0` for the built-in admin panel. `site_id = 0` does NOT mean "no site". It means the domain belongs to the administrative panel and is fully operational. Applicability should only be `not_applicable` when `site_id` is truly absent (negative or explicitly unlinked). Domains with `site_id = 0` must still receive HTTP, HTTPS, SSL, and DNS checks.

| Check | Applicable when |
|-------|----------------|
| A record | **Always** (every domain has at least one A record expected) |
| AAAA (IPv6) | Always (informational — never penalises) |
| MX record | `MailDomain` exists for this domain |
| SPF record | `MailDomain` exists for this domain |
| DKIM record | `MailDomain` exists AND `dkim_public_key_dns` is not empty |
| DMARC record | `MailDomain` exists for this domain |
| SSL certificate | Domain has `ssl_enabled = true` OR `site_id >= 0` (linked to any site including admin panel) |
| HTTP reachability | Domain is linked to a site (`site_id >= 0`). `site_id = 0` is the admin panel and IS reachable. |
| MTA-STS | `MailDomain` exists AND mode is `local-primary` |
| CAA | Domain is linked to a site (`site_id >= 0`) |
| TLS-RPT | `MailDomain` exists for this domain |
| Autodiscover | `MailDomain` exists AND mode is `local-primary` |

**Key rule:** `site_id >= 0` means "linked to a real site (including admin panel)". Only domains with `site_id < 0` or completely unlinked should receive `not_applicable` for site-dependent checks.

### Weight classes

| Class | Label | Score impact | Example |
|-------|-------|-------------|---------|
| **Required** | `req` | Full weight — missing record reduces score | A record, MX, SPF, DKIM, DMARC, SSL, HTTP |
| **Recommended** | `rec` | Reduced weight — minor penalty if missing | MTA-STS, Autodiscover, CAA |
| **Informational** | `info` | **No penalty** regardless of status | IPv6, TLS-RPT |

### Status values per check

| Status | Meaning | Score effect for `req` | Score effect for `rec` |
|--------|---------|----------------------|----------------------|
| ✅ ok | Record found and matches expectations | Full points | Full points |
| ⚠️ warning | Record missing or partial | Half points | Zero points (no penalty, but no bonus) |
| ❌ error | Record required but missing | Zero points | N/A (rec checks don't get error) |
| ⬜ n/a | Not applicable | Excluded | Excluded |

### Weight distribution

The total possible score is **always 100 points**, but the distribution depends on which checks are applicable.

**Algorithm:**
1. Start with a base set of checks with predefined max weights
2. Remove checks where applicability = `false`
3. If no checks remain, score = N/A (domain with no applicable checks)
4. Otherwise, normalise remaining weights proportionally so they sum to 100

**Base weights (before normalisation):**

| Check | Raw weight | Class | When applicable |
|-------|-----------|-------|----------------|
| DNS: A record | 25 | req | Always |
| DNS: AAAA (IPv6) | 0 (info) | info | Always — never adds weight |
| Mail: MX | 12 | req | MailDomain exists |
| Mail: SPF | 10 | req | MailDomain exists |
| Mail: DKIM | 10 | req | MailDomain exists + DKIM generated |
| Mail: DMARC | 8 | req | MailDomain exists |
| SSL: Certificate | 20 | req | `ssl_enabled` OR `site_id >= 0` (including admin panel) |
| HTTP: Reachability | 15 | req | `site_id >= 0` (admin panel at `site_id = 0` included) |
| MTA-STS | 3 | rec | MailDomain + local-primary |
| CAA | 2 | rec | `site_id >= 0` (SSL-aware domains) |
| TLS-RPT | 0 | info | MailDomain exists — no weight |
| Autodiscover | 3 | rec | MailDomain + local-primary |

**Normalisation formula:**
```
normalised_weight = raw_weight × (100 / sum_of_applicable_raw_weights)
```

### Example scenarios

**Scenario A: Full configuration (site + mail + SSL)**
```
Applicable: A(25), MX(12), SPF(10), DKIM(10), DMARC(8), SSL(20), HTTP(15), MTA-STS(3), CAA(2)
Sum: 105
Normalisation factor: 100/105 = 0.952
All checks OK → 100/100
```

**Scenario B: Site without mail (no MailDomain)**
```
Applicable: A(25), SSL(20), HTTP(15), CAA(2)
Sum: 62
Normalisation factor: 100/62 = 1.613
All checks OK → 100/100
```

**Scenario C: Site without mail, missing SSL**
```
Applicable: A(25), SSL(20), HTTP(15), CAA(2)
Sum: 62, factor: 1.613
A ok → 25×1.613 = 40.3
SSL error → 0×1.613 = 0
HTTP ok → 15×1.613 = 24.2
CAA missing (rec, warning) → 0
Score: 64.5 → 65/100
```

**Scenario D: Standalone domain with no site, no mail**
```
Applicable: A(25)
Sum: 25, factor: 4.0
A ok → 100/100 (single applicable check fully satisfied)
```

**Scenario E: Admin panel domain (site_id = 0) with no mail**
```
Applicable: A(25), SSL(20), HTTP(15), CAA(2)
Sum: 62, normalisation factor: 1.613
All OK → 100/100 (admin panel gets HTTP and SSL checks despite site_id = 0)
```

**Scenario F: Unlinked domain (site_id < 0 or site_id absent)**
```
No site, no mail → A(25) only
A missing → 0/100 (critical — domain without A record and no site linkage)
```

### Grade boundaries

| Score | Grade | Color |
|-------|-------|-------|
| 90–100 | Excellent | Green |
| 70–89 | Good | Blue |
| 40–69 | Fair | Yellow |
| 1–39 | Poor | Orange |
| 0 | Critical | Red |
| N/A | Not applicable | Grey |

### Implementation

The Health Score is computed **entirely on the frontend** using:
- `GET /api/domains/<id>/dns-check` — DNS record presence
- `GET /api/mail/domains` — MailDomain existence and DKIM generation status
- `GET /api/domains` — SSL status, site_id, ssl_enabled
- `GET /api/runtime/<site_id>` — HTTP reachability

The frontend function `computeHealthScore(domainData, dnsData, mailData)` runs the algorithm above and returns `{ score, grade, breakdown }`. The breakdown array is used to render the Health tab's check list.

No backend changes are needed for the scoring — it is pure frontend logic.

---

## New Resources

None. This proposal does NOT create new backend resources. All data models already exist:
- `Domain` — in `libs/domain/`
- `MailDomain` — in `libs/mail/`
- `SslCertificate` — in `libs/ssl/`
- `Site` — in `libs/site/`

---

## Managers

No new managers. This proposal adds a **read-only check service**:

### `DnsCheckService` (new file: `libs/dns/DnsCheckService.h/.cpp`)

A lightweight service using the **c-ares** DNS resolution library that returns structured results. It is **read-only** — no DNS zone editing.

```
class DnsCheckService {
public:
    struct DnsRecord {
        std::string type;      // "A", "AAAA", "MX", "TXT", "CNAME", "NS"
        std::string name;      // e.g. "example.com"
        std::string value;     // e.g. "192.168.1.1"
        int ttl = 0;
        int priority = 0;      // for MX records
        std::string dns_response_details;  // structured c-ares result for evidence panel
    };

    struct DnsCheckResult {
        std::string domain;
        std::string resolved_at;   // ISO 8601
        std::vector<DnsRecord> records;
        struct {
            std::string mname;     // primary nameserver
            std::string rname;     // responsible admin email
            uint64_t serial = 0;
        } soa;
        bool success;
        std::string error;
    };

    DnsCheckResult check(const std::string& domain,
                          const std::vector<std::string>& record_types);

    // Cache DNS results for N seconds to avoid repeated queries
    void set_cache_ttl(int seconds);
    void clear_cache(const std::string& domain);
};
```

**Implementation:**
- Uses **c-ares** (asynchronous DNS resolution library) — no shell commands, no `popen()`, no `dig` binary required
- Initialises c-ares channel with `ares_init()` (timeout 5s, 2 retries)
- For each requested record type, calls `ares_query()` with the appropriate DNS record type constant
- Processes responses via `ares_callback` — extracts structured data into `DnsRecord` structs
- Handles all DNS response statuses: `NOERROR`, `NXDOMAIN`, `NODATA`, `SERVFAIL`, `TIMEOUT`
- Returns structured JSON — **never returns raw protocol bytes** (only formatted details)
- Caching: in-memory, 60-second TTL by default

**Security:**
- Domain validated against strict allowlist (alphanumeric, dots, hyphens only)
- Record types validated against enum allowlist
- No shell invocation → no injection possible
- Timeout prevents resource exhaustion

### Extend `DomainViewService`

Adds mail domain info to the enriched domain JSON. Currently `DomainViewService` enriches with `site_name` and `ssl_status`. We add:

```cpp
// Extended enriched JSON adds:
// "mail_domain_id": 5 or 0 (null)
// "mail_domain_mode": "local-primary" or ""
// "dkim_selector": "dkim" or ""
// "dkim_generated": true/false
// "dkim_public_key_dns": "v=DKIM1;..." or ""
```

This runs **zero additional queries** beyond what the frontend already does — it just moves the aggregation from client to server, following the API-first principle.

---

## Storage

No changes. Existing pipe-delimited storage is sufficient for all current data models.

---

## Providers

No new providers. `DnsCheckService` is a **service**, not a provider. No DNS provider interface is created.

---

## REST API

### Existing APIs used (NO changes)

| Endpoint | Used for |
|----------|----------|
| `GET /api/domains` | List domains with SSL + site + mail info |
| `GET /api/mail/domains` | Detect if mail domain exists for each domain |
| `GET /api/mail/domains/<id>` | Get DKIM DNS record, mail mode |
| `POST /api/mail/domains/<id>/dkim/generate` | Generate DKIM key pair |
| `GET /api/ssl/<domain>` | SSL details per domain |
| `GET /api/sites/<id>/mail-status` | PHP mail enabled status |
| `GET /api/runtime/<site_id>` | HTTP service status |

### New API endpoint

#### `GET /api/domains/<domain>/dns-check`

Performs live DNS resolution for a domain via the **c-ares** library and returns all found records as structured JSON.

**Request:**
```
GET /api/domains/example.com/dns-check
```

Optional query parameter to filter record types (default: all):
```
GET /api/domains/example.com/dns-check?types=A,AAAA,MX,TXT,NS,CNAME
```

Optional query parameter to bypass cache:
```
GET /api/domains/example.com/dns-check?refresh=1
```

**Response:**
```json
{
  "success": true,
  "data": {
    "domain": "example.com",
    "resolved_at": "2026-07-15T12:00:00Z",
    "cached": false,
    "records": [
      {
        "type": "A",
        "name": "example.com",
        "value": "192.168.1.1",
        "ttl": 3600,
        "raw": "example.com. 3600 IN A 192.168.1.1",
        "configured": null
      },
      {
        "type": "AAAA",
        "name": "example.com",
        "value": "",
        "ttl": 0,
        "raw": "",
        "configured": null
      },
      {
        "type": "MX",
        "name": "example.com",
        "value": "mail.example.com",
        "ttl": 3600,
        "priority": 10,
        "raw": "example.com. 3600 IN MX 10 mail.example.com.",
        "configured": null
      },
      {
        "type": "TXT",
        "name": "dkim._domainkey.example.com",
        "value": "v=DKIM1; k=rsa; p=...",
        "ttl": 3600,
        "raw": "dkim._domainkey.example.com. 3600 IN TXT \"v=DKIM1; k=rsa; p=...\"",
        "configured": null
      },
      {
        "type": "TXT",
        "name": "_dmarc.example.com",
        "value": "v=DMARC1; p=none;",
        "ttl": 3600,
        "raw": "_dmarc.example.com. 3600 IN TXT \"v=DMARC1; p=none;\"",
        "configured": null
      }
    ],
    "soa": {
      "mname": "ns1.example.com",
      "rname": "admin.example.com",
      "serial": 2026071501
    }
  }
}
```

Each record includes a `raw` field containing the full dig response line for that record. This is used by the frontend's evidence panel to show the raw DNS response to experienced administrators.

The `configured` field is populated by the frontend by merging DNS check results with ContainerCP's stored values (DKIM, SPF template). For DMARC, the Wizard generates a **Recommended** value (not stored — ephemeral session comparison). See the "Configured vs Published" section for the merge logic.

**Error:**
```json
{
  "success": false,
  "error": "DNS resolution failed for example.com: query timed out"
}
```

**Owner:** `libs/dns/DnsCheckService`  
**HTTP status codes:** 200 (success), 400 (invalid domain format), 502 (DNS resolution failure)  
**Cache:** 60 seconds per domain, bypassed with `?refresh=1`

### Extended existing API

#### `GET /api/domains` — new fields added to response

Currently returns per-domain: `id`, `domain`, `type`, `site_id`, `site_name`, `site_domain`, `target`, `ssl_enabled`, `ssl_status`, `enabled`.

New fields (backward-compatible):
```json
{
  "id": 1,
  "domain": "example.com",
  ...
  "mail_domain_id": 5,
  "mail_domain_mode": "local-primary",
  "dkim_selector": "dkim",
  "dkim_generated": true,
  "dkim_public_key_dns": "v=DKIM1; k=rsa; p=..."
}
```

When no MailDomain exists, `mail_domain_id` is `0` and all mail fields are empty strings or `false`.

---

## Web UI

### File changes

All changes are in `web/app.js` (single SPA file). The existing DOM-building pattern is preserved.

### New pages

| Page | Function | Description |
|------|----------|-------------|
| `loadDomains(p)` | Enhanced | Existing page — adds live DNS/Mail/HTTP columns and Health Score |
| `loadDomainDetail(p, id)` | **New** | Domain detail dashboard with 5 tabs |

### New functions

| Function | Purpose |
|----------|---------|
| `loadDomainOverview(domainId)` | Tab 1: Overview with summary cards + DNS check results |
| `loadDomainDnsRecords(domainId)` | Tab 2: DNS records table with copy helpers + expected vs actual |
| `loadDomainMail(domainId)` | Tab 3: Mail configuration status with expected vs actual DNS check |
| `loadDomainSecurity(domainId)` | Tab 4: DMARC Wizard + MTA-STS + CAA + TLS-RPT |
| `loadDomainHealth(domainId)` | Tab 5: Health score and check results |
| `refreshDnsCheck(domainId, section)` | Called by [Check Again] — re-fetches DNS check for a specific section |
| `computeHealthScore(domain)` | Calculate percentage from DNS + Mail + SSL + HTTP |
| `showDmarcWizard()` | Modal: DMARC policy selector (Monitor/Quarantine/Reject) |
| `domainDnsCopy(el, type)` | Copy helper for DNS records (Host/Value/FQDN/Full) |
| `compareDnsRecord(configured, published)` | Returns match/mismatch/not-published/not-configured status |
| `showEvidence(recordType, configured, published, rawDig)` | Expands inline evidence panel with Expected, Published, Reason, Raw DNS |
| `formatRawDig(domain, type)` | Formats the raw dig response for display in evidence panel |
| `computeApplicableChecks(domain)` | Returns list of checks with applicability status (site_id >= 0 logic) |

### Component: DMARC Wizard (modal)

A modal with 3 policy options presented as cards:

```
┌─────────────────────────────────────────────────────────┐
│  DMARC Policy Wizard                                     │
│                                                          │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────┐ │
│  │   Monitor        │  │   Quarantine    │  │  Reject  │ │
│  │                  │  │   (Recommended) │  │          │ │
│  │  p=none          │  │  p=quarantine   │  │ p=reject │ │
│  │                  │  │                  │  │          │ │
│  │  No action taken │  │  Tag as spam     │  │ Block    │ │
│  │  on failing msgs │  │  in quarantine   │  │ delivery │ │
│  └─────────────────┘  └─────────────────┘  └──────────┘ │
│                                                          │
│  Your DNS Record (DMARC):                                │
│  _dmarc.example.com. 3600 IN TXT "v=DMARC1;             │
│  p=quarantine; rua=mailto:dmarc@example.com"            │
│                                                          │
│  [Copy Record]    [Copy with RUA]                        │
│                                                          │
│  ⚠️  Set p=none first to monitor, then escalate          │
│  to quarantine once you're confident.                    │
└─────────────────────────────────────────────────────────┘
```

### Component: Recommendation card

Used throughout the detail page to show required/recommended records:

```html
<div class="recommendation-card recommendation-required">
  <div class="recommendation-icon">⚠️</div>
  <div class="recommendation-body">
    <div class="recommendation-title">SPF Record Missing</div>
    <div class="recommendation-desc">
      Sender Policy Framework prevents email spoofing.
      Add a TXT record listing authorized mail servers.
    </div>
    <div class="recommendation-action">
      <button class="btn btn-sm" onclick="copyRecord('v=spf1 mx ~all')">
        Copy SPF Record
      </button>
    </div>
  </div>
</div>
```

Three severity levels:
- `recommendation-required` — Red border, missing required record for active service
- `recommendation-recommended` — Yellow border, best practice but not blocking
- `recommendation-ok` — Green border, everything is configured

### Navigation

The sidebar "Domains" link remains unchanged. Clicking a domain in the table navigates to the detail page via `navigate('domain-detail', domainId)`.

Breadcrumb pattern (already used in Mail):
```
Domains / example.com
```

### Nav registration

```javascript
// In navigate() dispatch:
else if (page === 'domain-detail') loadDomainDetail(p, params);
```

---

## CLI

No changes. CLI is not affected by this proposal — it only modifies the Web UI and adds one read-only API endpoint.

---

## Configuration

| Key | Default | Description |
|-----|---------|-------------|
| `dns_check_timeout` | `5` | Seconds to wait per DNS query |
| `dns_check_retries` | `2` | Number of retries on failure |
| `dns_check_cache_ttl` | `60` | Seconds to cache DNS results |

All values are optional with sensible defaults. Added to the existing settings system.

---

## Migration Strategy

No migration needed. All changes are additive:
- New API endpoint doesn't affect existing consumers
- Extended `GET /api/domains` response is backward-compatible (new optional fields)
- Web UI changes are purely frontend

---

## Backward Compatibility

- **`GET /api/domains`** — extended with new optional fields. Existing fields unchanged. Old frontend continues to work.
- **`GET /api/domains/<domain>/dns-check`** — entirely new endpoint, no conflict.
- **Web UI** — the existing `loadDomains` function is enhanced, not replaced. The new `domain-detail` page is additive.
- No existing API endpoints are modified or removed.

---

## Roadmap update

The original DNS-001–DNS-004 items were scoped for a full DNS zone management system. Since that is no longer the goal, the items are renamed and re-scoped:

| Old ID | Old name | New ID | New scope |
|--------|----------|--------|-----------|
| DNS-001 | DNS resource and manager | **DNS-001** | `DnsCheckService` — DNS resolution checker using **c-ares**, structured JSON output, caching, timeout config |
| DNS-002 | DNS provider interface | **DNS-002** | `GET /api/domains/<domain>/dns-check` endpoint — live DNS record lookup with expected-vs-actual comparison |
| DNS-003 | DNS CLI and REST API | **DNS-003** | Domain detail Web UI (Overview, DNS Records, Mail, Security, Health tabs) with copy helpers |
| DNS-004 | DNS Web UI pages | **DNS-004** | Enhanced domain list with live DNS/Mail/SSL/HTTP columns and Health Score |

**Core change:** The subsystem is now called `DNS Check` (not `DNS Management`). All four tasks are purely read-only. No zone editing, no provider interface, no DNS publishing.

---

## Rejected Alternatives

### 1. Full DNS zone editor
Create a complete DNS management system where ContainerCP controls DNS zones (like Cloudflare, AWS Route53).

**Rejected because:** It contradicts the Product Vision — ContainerCP is a hosting control panel, not a DNS management platform. This would require a provider interface, storage, manager, and full CRUD API. Out of scope for v0.5 and beyond.

### 2. Client-side DNS-over-HTTPS (DoH)
Use a third-party DoH service (like Cloudflare's `1.1.1.1/dns-query`) directly from the browser to avoid any backend changes.

**Rejected because:** It violates the API-first principle. The frontend would depend on an external service that may be unavailable or blocked. It also exposes DNS queries to a third party. A backend service using c-ares is more reliable, private, and consistent with ContainerCP's architecture.

### 3. Embed DNS recommendations in the backend
Have the backend analyze DNS records and generate recommendations (e.g., "SPF is missing") rather than doing it in the frontend.

**Rejected because:** Recommendations are presentation logic. The backend should return raw DNS data. Analysis rules (what's required, what's recommended) may change and are easier to iterate on in the frontend. This follows the existing pattern where the backend returns data and the UI interprets it.

### 4. Create a separate "DNS Health" module
A separate tab/page for DNS health that aggregates data from multiple sources.

**Partially accepted:** Health is a tab within the domain detail page, not a separate page. This keeps the domain as the central organizational unit.

### 5. Remove the existing domains table entirely
Replace the list page with just a search/detail flow.

**Rejected because:** The list view is useful for overview. We enhance it with live status columns instead.

### 6. Use `dig` via `popen()` instead of a DNS library
Invoke the system `dig` command via `popen()` instead of linking against a C/C++ DNS library.

**Rejected because:** Shell invocation creates security surface (even with validated arguments), requires `dig` to be installed on the target system, and parsing text output is fragile across `bind9` versions. The c-ares library provides structured DNS responses natively, distinguishes all required response statuses (`NOERROR`, `NXDOMAIN`, `NODATA`, `SERVFAIL`, `TIMEOUT`), and avoids any shell dependency.

---

## Risks

| Risk | Mitigation |
|------|------------|
| DNS resolution can be slow (multiple record types × many domains) | Cache results per domain for 60 seconds. Show stale data while refreshing. Only check visible records. |
| DNS resolution may fail (firewall, no DNS, c-ares not available) | Graceful degradation — show "Unable to check" badge. API returns 502 with clear message. Add `libcares-dev` to build dependencies. |
| Large number of domains (100+) could cause many API calls | Batch DNS check. Add rate limiting. The detail page only checks one domain at a time. |
| The single `web/app.js` file grows too large | This is an existing risk. The new code follows existing patterns. A future refactoring could split into modules. |
| DMARC Wizard suggests policies that break email | Add clear warnings: "Start with p=none to monitor. Only escalate after monitoring." |
| c-ares API complexity | Wrap c-ares async queries in a synchronous helper. The complexity is encapsulated within `DnsCheckService` — callers see a simple `check(domain, types)` interface. |

---

## Validation Plan

### Unit tests
- `DnsCheckService::check()` returns correct records for known domains
- `DnsCheckService::check()` handles NXDOMAIN, timeout, no records gracefully
- `DnsCheckService::check()` validates domain names (rejects invalid chars)
- Cache hit returns cached data; `?refresh=1` bypasses cache
- DomainViewService enrichment includes new mail fields

### Integration tests
- `GET /api/domains/example.com/dns-check` returns valid structured JSON
- `GET /api/domains/example.com/dns-check?types=A,MX` returns only A and MX records
- `GET /api/domains/example.com/dns-check?refresh=1` returns fresh data
- Extended `GET /api/domains` includes new fields when MailDomain exists
- Extended `GET /api/domains` returns empty/null mail fields when no MailDomain

### Web UI validation
- Domain list shows correct health indicators for all states
- Domain detail loads all 5 tabs without errors
- [Check Again] refreshes each section independently
- DMARC Wizard generates correct TXT records for all 3 policies
- Copy buttons work for all DNS record types
- Mail tab shows clean "not configured" state when no MailDomain exists
- Expected vs actual comparison shows correct match/mismatch/missing for DKIM
- Health score calculation matches across all test scenarios

### Manual validation
- Open a domain with full configuration → all green, high health score
- Open a domain with DKIM generated but not published → shows "Generated but Missing from DNS"
- Open a domain with no mail → mail tab shows clean state, no false errors
- Click each copy button → verifies clipboard content is correct
- Click [Check Again] → shows spinner, then updates results

---

## Implementation order

1. **Backend:** `DnsCheckService` (`libs/dns/DnsCheckService.h/.cpp`) — dig wrapper with structured output
2. **Backend:** API endpoint `GET /api/domains/<domain>/dns-check`
3. **Backend:** Extend `GET /api/domains` with mail domain fields
4. **Frontend:** Enhanced domain list (live columns + health score)
5. **Frontend:** Domain detail page → Overview tab
6. **Frontend:** Domain detail page → DNS Records tab with copy helpers + expected vs actual
7. **Frontend:** Domain detail page → Mail tab with conditional display
8. **Frontend:** Domain detail page → Security tab with DMARC Wizard
9. **Frontend:** Domain detail page → Health tab
10. **Documentation:** Update `docs/api/API_REFERENCE.md` with new endpoint
11. **Documentation:** Update `planning/project-status.md` with new DNS-001–DNS-004 scope
12. **Tests:** Unit + integration tests for backend
13. **Changelog:** Record in CHANGELOG.md

---

## Appendix: Mockup reference

### Domain list (enhanced)

```
Domains  (12 domains)

┌─────┬──────────┬──────┬────────────┬──────┬──────┬──────┬──────┬────────┬──────────────┐
│ #   │ Domain   │ Type │ Site       │ DNS  │ Mail │ SSL  │ HTTP │ Health │ Checks       │
├─────┼──────────┼──────┼────────────┼──────┼──────┼──────┼──────┼────────┼──────────────┤
│ 1   │ example  │ prim │ MySite     │  🟢  │  ✅  │  🟢  │  🟢  │  100%  │ 9/9 req      │
│     │ .com     │      │            │      │      │      │      │  🟢    │              │
│ 2   │ cp.      │ prim │ Admin Panel│  🟢  │  ⬜  │  🟢  │  🟢  │  100%  │ 4/4 req      │
│     │ example  │      │ (site_id=0)│      │ n/a  │      │      │  🟢    │ (mail n/a)   │
│     │ .com     │      │            │      │      │      │      │        │              │
│ 3   │ test.org │ alias│ MySite     │  ⚠️  │  ⬜  │  🟢  │  🟢  │  82%   │ 4/5 req      │
│     │          │      │            │      │ n/a  │      │      │  🟡    │ (mail n/a)   │
│ 4   │ old.net  │ prim │ (unlinked) │  ❌  │  ⬜  │  ❌  │  ❌  │  20%   │ 1/5 req      │
│     │          │      │            │      │ n/a  │ n/a  │ n/a  │  🔴    │ (site n/a)   │
└─────┴──────────┴──────┴────────────┴──────┴──────┴──────┴──────┴────────┴──────────────┘
```

Key: `n/a` = not applicable (excluded from score), `req` = required checks passed/total.
Note: Admin Panel (site_id=0) gets SSL + HTTP checks despite having no "regular" site.
Note: Unlinked domains (no site_id) also get no SSL/HTTP checks (n/a).

### Domain detail — Overview tab (site with mail + SSL)

```
Domains / example.com                       [Open] [Copy] [Remove]

Health Score: 92/100 (Excellent)   9 checks • 0 n/a • 8 ok • 1 ⚠️

┌─ DNS Check ────────────────────────────────────────────────────┐
│  Type  │ Configured        │ Published              │ Status    │
├───────┼───────────────────┼────────────────────────┼───────────┤
│ A     │ —                 │ 192.168.1.1           │ ✅ Match  │
│ AAAA  │ —                 │ Not found             │ ℹ️ Info   │
│ MX    │ —                 │ mail.example.com (10) │ ✅ Match  │
│ SPF   │ v=spf1 mx ~all    │ v=spf1 mx ~all        │ ✅ Match  │
│ DKIM  │ v=DKIM1; k=rsa... │ Not found             │ ❌ Not    │
│       │                   │                        │   Publ.  │
│       │                   │   [Copy H][Copy V]    │ [Why?]   │
│ DMARC │ p=reject          │ p=none                │ ⚠️ Mismat │
│       │                   │                        │   ch     │
│       │                   │   [Copy Record]       │[Show Det]│
│                             [Check Again]                     │
└───────────────────────────────────────────────────────────────┘

┌──────────────────────┬──────────────────────┬──────────────────────┐
│ Mail                 │ SSL                  │ Site                 │
│ Domain: example.com  │ Certificate: Active  │ MySite               │
│ Mode: Local Primary  │ HTTPS: Enabled       │ Backend: Apache2     │
│ DKIM: ✅ Generated   │ Redirect: Enabled    │ PHP: 8.4             │
│ Mailboxes: 3         │ Expires: 2026-10-06 │ Runtime: All Running │
└──────────────────────┴──────────────────────┴──────────────────────┘
```

### Domain detail — Overview tab (site without mail)

```
Domains / test.org                          [Open] [Copy] [Remove]

Health Score: 100/100 (Excellent)   5 checks • 4 n/a (mail) • all ok

┌─ DNS Check ────────────────────────────────────────────────────┐
│  Type  │ Configured        │ Published              │ Status    │
├───────┼───────────────────┼────────────────────────┼───────────┤
│ A     │ —                 │ 10.0.0.1              │ ✅ Match  │
│ AAAA  │ —                 │ Not found             │ ℹ️ Info   │
│ ⬜ MX  │ —                 │ (mail not in use)     │ ⬜ n/a    │
│ ⬜ SPF │ —                 │ (mail not in use)     │ ⬜ n/a    │
│ ⬜ DKIM│ —                 │ (mail not in use)     │ ⬜ n/a    │
│ ⬜ DMAR│ —                 │ (mail not in use)     │ ⬜ n/a    │
│   C   │                    │                        │           │
│                             [Check Again]                       │
└─────────────────────────────────────────────────────────────────┘

┌──────────────────────┬──────────────────────┬──────────────────────┐
│ Mail                 │ SSL                  │ Site                 │
│ Not configured       │ Certificate: Active  │ MySite               │
│ ℹ️ Mail service is   │ HTTPS: Enabled       │ Backend: Nginx       │
│   not in use for     │ Redirect: Enabled    │ PHP: 8.4             │
│   this domain.       │ Expires: 2026-10-06 │ Runtime: All Running │
└──────────────────────┴──────────────────────┴──────────────────────┘
```

### Domain detail — Overview tab (admin panel, site_id=0)

```
Domains / cp.example.com                    [Open] [Copy] [Remove]

Health Score: 100/100 (Excellent)   4 checks • 0 n/a • all ok

┌─ DNS Check ────────────────────────────────────────────────────┐
│  Type  │ Configured        │ Published              │ Status    │
├───────┼───────────────────┼────────────────────────┼───────────┤
│ A     │ —                 │ 10.0.0.1              │ ✅ Match  │
│ ⬜ MX  │ —                 │ (mail not in use)     │ ⬜ n/a    │
│ ⬜ SPF │ —                 │ (mail not in use)     │ ⬜ n/a    │
│ SSL   │ Active             │ Active                │ ✅ Match  │
│ HTTP  │ —                 │ 200 OK                │ ✅ Match  │
│                             [Check Again]                       │
└─────────────────────────────────────────────────────────────────┘

┌──────────────────────┬──────────────────────┬──────────────────────┐
│ Mail                 │ SSL                  │ Site                 │
│ Not configured       │ Certificate: Active  │ Admin Panel          │
│ ℹ️ Mail service is   │ HTTPS: Enabled       │ (site_id=0)          │
│   not in use for     │ Redirect: Enabled    │ HTTP + SSL checks    │
│   this domain.       │ Expires: 2026-10-06 │ are ACTIVE           │
└──────────────────────┴──────────────────────┴──────────────────────┘
```

### Domain detail — DNS Records tab

```
DNS Records for example.com                         [Check Again]

Status  Type  Name                     Value                      TTL
─────────────────────────────────────────────────────────────────────
   ✅   A     @                        192.168.1.1                3600
   ⚠️   AAAA  @                        (not found)                 —
   ✅   MX    @                        mail.example.com (10)     3600
   ✅   TXT   @                        v=spf1 mx ~all            3600
   ❌   TXT   dkim._domainkey          (missing from DNS)          —
              .example.com             Expected: v=DKIM1; k=rsa;...
                                       [Copy Host] [Copy Value]
                                       [Copy FQDN] [Copy Full]
   ✅   TXT   _dmarc.example.com       v=DMARC1; p=none;         3600
```

### DMARC Wizard

```
DMARC Policy Wizard

Current status: Mismatch
  Recommended: p=reject    Published: p=none    [Show Details]

[Monitor]         [Quarantine]         [Reject]
  p=none            p=quarantine         p=reject
  Monitor only      Tag as spam          Block delivery

▼ Show Details — DMARC Mismatch

  Recommended (from DMARC Wizard)
    v=DMARC1; p=reject; rua=mailto:dmarc@example.com

  Published (public DNS)
    v=DMARC1; p=none; rua=mailto:dmarc@example.com

  Reason
    The policy (p=) field differs.
    Recommended: p=reject (block failing emails)
    Published:   p=none (monitor only, no action)
    Update the TXT record at _dmarc.example.com to activate Reject.

  Raw DNS Response
    _dmarc.example.com. 3600 IN TXT "v=DMARC1; p=none; rua=mailto:dmarc@example.com"

  [Copy Reject Record]  [Dismiss]

Preview (if Reject selected):
_dmarc.example.com. 3600 IN TXT "v=DMARC1; p=reject; rua=mailto:dmarc@example.com"

[Copy Record]    [Copy with RUA]

⚠️ Start with p=none to monitor, then escalate to quarantine after 1-2 weeks.
```

### Health Score weight indicator

Throughout the UI, each row shows its weight class as a small label:

| Label | Meaning |
|-------|---------|
| `[req ✅]` | Required check — passed |
| `[req ❌]` | Required check — failed (penalises score) |
| `[req ⚠️]` | Required check — warning (partial penalty) |
| `[rec]` | Recommended check — minor or no penalty |
| `[info]` | Informational — no score impact |
| `[n/a]` | Not applicable — excluded from score |
