# DNS GUI Redesign — Informational Domain Dashboard

**Status:** Draft  
**Author:** ContainerCP Architecture  
**Version:** 1  
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

Admin opens a domain → immediately sees:
- What's configured correctly
- What's missing
- What's recommended
- All DNS records with one-click copy
- Mail, SSL, and HTTP status

This eliminates the need to open third-party instructions or switch between multiple tools.

---

## Current Architecture

### Domain subsystem (`libs/domain/`)
- `Domain` resource: `id`, `fqdn`, `site_id`, `type`, `target`, `ssl_enabled`, `enabled`
- `DomainManager`: CRUD operations
- `DomainViewService`: builds enriched JSON with site name + SSL status
- API: `GET /api/domains` (enriched list), `POST /api/domains/remove`

### No DNS subsystem
- There is **no** `libs/dns/` directory
- No DNS provider interface exists
- No DNS resolution/checking capability
- `ProfileType::DNS` enum exists but no implementation

### Mail DKIM
- Fully implemented in `libs/mail/DkimManager`
- API: `POST /api/mail/domains/<id>/dkim/generate`
- Returns DNS TXT record value with copy buttons in UI

### Web UI (`web/app.js`)
- Single-page application (~2300 lines)
- Domains table shows: Domain, Type, Site, Target, **DNS (Unknown)**, HTTP (Unknown), SSL, Actions
- Mail detail page shows DKIM record with copy buttons (Host, Value, FQDN, Full Record)

---

## Proposed Architecture

### Core principle

> **The Domains section is an INFORMATIONAL dashboard. It does NOT create Sites, Mail Domains, or SSL certificates. It does NOT edit DNS zones. It shows the current state and provides recommendations.**

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
| Health Score | Computed from all above | Simple percentage or letter grade |
| Actions | — | Open, Copy domain, View detail, Remove |

Each domain row shows a **Health Score** indicator — a small colored dot plus percentage. This score aggregates:
- DNS check (25%)
- Mail status (25%)
- SSL status (25%)
- HTTP reachability (25%)

Health Score is computed **entirely on the frontend** from existing API responses.

#### 2. Domain Detail (`/domains/<id>`) — NEW PAGE

This is the main informational dashboard. Layout uses the existing tab system (`style.css` already has `.tabs` and `.tab` classes).

**Header bar:**
```
← Domains  /  example.com        [Open in browser] [Copy domain] [Remove]
Health Score: 85%  (Good)
```

**Tabs:**

### Tab 1: Overview

Summary of everything at a glance.

```
┌─────────────────────────────────────────────────────────────┐
│  DNS Records Check (from live DNS resolution)               │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ ✅ A       → 192.168.1.1          TTL: 3600            ││
│  │ ⚠️ AAAA   → Not configured        (recommended: IPv6)  ││
│  │ ✅ MX      → mail.example.com      Priority: 10         ││
│  │ ⚠️ SPF     → Not configured        (required for mail)  ││
│  │ ❌ DKIM    → Missing               (generated but not   ││
│  │                                      published in DNS)  ││
│  │ ❌ DMARC   → Not configured                              ││
│  └─────────────────────────────────────────────────────────┘│
│                                                              │
│  Quick Actions: [Copy All Records] [DMARC Wizard]            │
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

Shows DNS resolution results with one-click copy helpers.

```
DNS Records for example.com

┌──────────────────────────────────────────────────────────────┐
│ Type │ Name           │ Value                    │ Actions   │
├──────────────────────────────────────────────────────────────┤
│ A    │ @              │ 192.168.1.1             │ [Copy]   │
│ AAAA │ @              │ — (not found)           │ [Copy]   │
│ MX   │ @              │ mail.example.com (10)   │ [Copy]   │
│ TXT  │ @              │ v=spf1 ...              │ [Copy]   │
│ TXT  │ dkim._domainkey│ v=DKIM1; k=rsa; p=...  │ [Copy H] │
│      │                │                         │ [Copy V] │
│      │                │                         │ [Copy F] │
│      │                │                         │ [Copy R] │
│ TXT  │ _dmarc         │ v=DMARC1; p=quarantine; │ [Copy]   │
└──────────────────────────────────────────────────────────────┘
```

Each DNS record type gets dedicated copy buttons:
- **Copy** → copies the full record value
- **DKIM** records get 4 buttons: Copy Host, Copy Value, Copy FQDN, Copy Full Record (already implemented pattern from mail detail page)

### Tab 3: Mail

Shows mail configuration status. Integrates with the existing Mail module.

**Layout depends on whether a MailDomain exists:**

**Scenario A — MailDomain exists:**
```
┌────────────────────────────────────────────────────────────┐
│  Mail Domain: example.com                                  │
│  Mode: Local Primary                                       │
│  Status: ✅ Active                                          │
│                                                            │
│  ┌─ Required Records ───────────────────────────────────┐  │
│  │ ✅ MX     → mail.example.com (10)     In DNS: ✅     │  │
│  │ ✅ SPF    → v=spf1 mx ~all           In DNS: ⚠️      │  │
│  │ ✅ DKIM   → Generated                In DNS: ❌      │  │
│  │ ⚠️ DMARC  → Not configured           [DMARC Wizard]  │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                            │
│  ┌─ Recommended Records ───────────────────────────────┐  │
│  │ ⚠️ Autodiscover → Not configured                    │  │
│  │ ⚠️ MTA-STS     → Not configured   [Learn more]      │  │
│  │ ⚠️ TLS-RPT     → Not configured                     │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                            │
│  Mailboxes: 3       Aliases: 2                             │
│  PHP Mail: ✅ Enabled for WordPress                        │
└────────────────────────────────────────────────────────────┘
```

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
┌────────────────────────────────────────────────────────────┐
│  DMARC Policy                                              │
│  Current: none detected in DNS                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  [Monitor]    p=none       — Monitor only, no action │  │
│  │  [Quarantine] p=quarantine — Tag suspicious as spam  │  │
│  │  [Reject]     p=reject     — Block failing emails    │  │
│  └──────────────────────────────────────────────────────┘  │
│  Each option shows the full DMARC TXT record ready to      │
│  copy to your DNS provider.                                │
│                                                            │
│  ┌─ DMARC Record Preview ──────────────────────────────┐  │
│  │  _dmarc.example.com.  3600  IN  TXT  "v=DMARC1;     │  │
│  │                                        p=quarantine; │  │
│  │                                        rua=mailto:   │  │
│  │                                        dmarc@        │  │
│  │                                        example.com;" │  │
│  │                                                     │  │
│  │  [Copy Record]                                      │  │
│  └─────────────────────────────────────────────────────┘  │
│                                                            │
│  MTA-STS                                                    │
│  ℹ️  MTA-STS (RFC 8461) ensures TLS is used for mail       │
│  delivery. Requires a TXT record and a policy file.         │
│  [Copy _mta-sts TXT] [Copy Policy Template]                 │
│                                                            │
│  CAA Record                                                 │
│  ℹ️  Certification Authority Authorization lets you         │
│  specify which CAs can issue certificates for your domain.  │
│  [Copy CAA Record]                                          │
│                                                            │
│  TLS-RPT                                                     │
│  ℹ️  TLS-RPT sends delivery failure reports to your email.  │
│  [Copy TLS-RPT Record]                                      │
└────────────────────────────────────────────────────────────┘
```

The **DMARC Wizard** is the key UX element here. It's not a multi-step wizard — it's a three-card selection that generates the correct TXT record.

### Tab 5: Health

Future-ready tab. For now it shows what's available:

```
┌────────────────────────────────────────────────────────────┐
│  DNS Health Score: 85% (Good)                              │
│                                                            │
│  ┌─ Checks ─────────────────────────────────────────────┐  │
│  │ ✅ DNS Resolution          (A record found)           │  │
│  │ ⚠️ IPv6                   (AAAA record missing)       │  │
│  │ ✅ Mail Configuration     (MX + SPF + DKIM + DMARC)  │  │
│  │ ✅ SSL Certificate        (Valid, 85 days remaining)  │  │
│  │ ⚠️ HTTP Reachability      (Responds with 301)         │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                            │
│  Future: DNS propagation check, TTL analysis,              │
│  certificate transparency, DNSSEC validation               │
└────────────────────────────────────────────────────────────┘
```

The architecture is designed so that future tabs (or additions to the Health tab) can show:
- DNSSEC status
- DNS propagation across global nameservers
- Certificate Transparency logs
- HTTP security headers (HSTS, CSP, etc.)
- Phishing/Domain squatting checks

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

A lightweight service that performs DNS resolution queries and returns structured results. It is **read-only** — no DNS zone editing.

```
class DnsCheckService {
public:
    struct DnsRecord {
        std::string type;    // "A", "AAAA", "MX", "TXT", "CNAME", "NS", "SOA"
        std::string name;    // e.g. "example.com"
        std::string value;   // e.g. "192.168.1.1"
        int ttl = 0;
        int priority = 0;    // for MX records
    };

    struct DnsCheckResult {
        std::vector<DnsRecord> records;
        bool success;
        std::string error;
    };

    DnsCheckResult check(const std::string& domain,
                          const std::vector<std::string>& record_types);
};
```

**Implementation:** Uses system `dig` or a DNS library. Returns raw DNS data without interpretation — all analysis happens in the frontend.

### `DomainEnrichmentService` (new file, or extend `DomainViewService`)

Adds mail domain info to the enriched domain JSON. Currently `DomainViewService` enriches with `site_name` and `ssl_status`. We add:

```cpp
// Extended enriched JSON adds:
// "mail_domain_id": 5 or null
// "mail_domain_mode": "local-primary" or null
// "mail_enabled": true/false
// "dkim_generated": true/false
// "php_mail_enabled": true/false
```

This runs **zero additional queries** beyond what the frontend already does — it just moves the aggregation from client to server, following the API-first principle.

---

## Storage

No changes. Existing pipe-delimited storage is sufficient for all current data models.

---

## Providers

No new providers. The DNS checking is a **service** (`DnsCheckService`), not a provider. A future DNS provider interface would be created when ContainerCP needs to *write* DNS records to an external service (Cloudflare, AWS Route53, etc.) — which is **out of scope** for this proposal.

---

## REST API

### Existing APIs used (NO changes)

| Endpoint | Used for |
|----------|----------|
| `GET /api/domains` | List domains with SSL + site info |
| `GET /api/mail/domains` | Detect if mail domain exists for each domain |
| `GET /api/mail/domains/<id>` | Get DKIM DNS record, mail mode |
| `GET /api/mail/domains/<id>/dkim/generate` | Generate DKIM key pair |
| `GET /api/ssl/<domain>` | SSL details per domain |
| `GET /api/sites/<id>/mail-status` | PHP mail enabled status |
| `GET /api/runtime/<site_id>` | HTTP service status |

### New API endpoint

#### `GET /api/domains/<domain>/dns-check`

Performs live DNS resolution for a domain and returns all found records.

**Request:**
```
GET /api/domains/example.com/dns-check
```

Optional query parameter to filter record types:
```
GET /api/domains/example.com/dns-check?types=A,AAAA,MX,TXT
```

**Response:**
```json
{
  "success": true,
  "data": {
    "domain": "example.com",
    "resolved_at": "2026-07-15T12:00:00Z",
    "records": [
      {"type": "A",     "name": "example.com",       "value": "192.168.1.1",      "ttl": 3600},
      {"type": "MX",    "name": "example.com",       "value": "mail.example.com", "ttl": 3600, "priority": 10},
      {"type": "TXT",   "name": "example.com",       "value": "v=spf1 mx ~all",  "ttl": 3600},
      {"type": "TXT",   "name": "dkim._domainkey.example.com", "value": "v=DKIM1; k=rsa; p=...", "ttl": 3600},
      {"type": "TXT",   "name": "_dmarc.example.com", "value": "v=DMARC1; p=none;", "ttl": 3600}
    ],
    "soa": {
      "mname": "ns1.example.com",
      "rname": "admin.example.com",
      "serial": 2026071501
    }
  }
}
```

**Error:**
```json
{
  "success": false,
  "error": "DNS resolution failed for example.com: No DNS servers reachable"
}
```

**Owner:** `libs/dns/DnsCheckService`  
**HTTP status codes:** 200 (success), 400 (invalid domain), 502 (DNS resolution failure)

### New API endpoint (optional — for richer domain list)

#### `GET /api/domains/enriched` (or extend existing `GET /api/domains`)

Currently `GET /api/domains` returns per-domain: `id`, `domain`, `type`, `site_id`, `site_name`, `site_domain`, `target`, `ssl_enabled`, `ssl_status`, `enabled`.

We extend the response to also include mail info by querying `MailDomainManager`:

**New fields added to existing response:**
```json
{
  "id": 1,
  "domain": "example.com",
  ...
  "mail_domain_id": 5,
  "mail_domain_mode": "local-primary",
  "dkim_generated": true,
  "php_mail_enabled": true
}
```

When no MailDomain exists, `mail_domain_id` is `null` (JSON `0` → frontend shows `null`).

**Note:** This is a backward-compatible addition. Existing fields are unchanged. The frontend is updated to use the new fields where available.

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
| `loadDomainOverview(domainId)` | Tab 1: Overview with summary cards |
| `loadDomainDnsRecords(domainId)` | Tab 2: DNS records table with copy helpers |
| `loadDomainMail(domainId)` | Tab 3: Mail configuration status |
| `loadDomainSecurity(domainId)` | Tab 4: DMARC Wizard + MTA-STS + CAA + TLS-RPT |
| `loadDomainHealth(domainId)` | Tab 5: Health score and check results |
| `computeHealthScore(domain)` | Calculate percentage from DNS + Mail + SSL + HTTP |
| `showDmarcWizard()` | Modal: DMARC policy selector (Monitor/Quarantine/Reject) |
| `domainDnsCopy(el, type)` | Copy helper for DNS records (Host/Value/FQDN/Full) |

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
│  Your DNS Record:                                         │
│  _dmarc.example.com. 3600 IN TXT "v=DMARC1;              │
│  p=quarantine; rua=mailto:dmarc@example.com"             │
│                                                          │
│  [Copy Record]    [Copy with RUA]                        │
│                                                          │
│  ⚠️  Set p=none first to monitor, then escalate          │
│  to quarantine once you're confident.                     │
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

No new configuration values.

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

## Rejected Alternatives

### 1. Full DNS zone editor
Create a complete DNS management system where ContainerCP controls DNS zones (like Cloudflare, AWS Route53).

**Rejected because:** It contradicts the Product Vision — ContainerCP is a hosting control panel, not a DNS management platform. The existing `libs/dns/` would need a provider interface, storage, manager, and full CRUD API. This is premature and out of scope for v0.5.

### 2. Client-side DNS-over-HTTPS (DoH)
Use a third-party DoH service (like Cloudflare's `1.1.1.1/dns-query`) directly from the browser to avoid any backend changes.

**Rejected because:** It violates the API-first principle. The frontend would depend on an external service that may be unavailable or blocked. It also exposes DNS queries to a third party. A backend endpoint is more reliable, private, and consistent with ContainerCP's architecture.

### 3. Embed DNS recommendations in the backend
Have the backend analyze DNS records and generate recommendations (e.g., "SPF is missing") rather than doing it in the frontend.

**Rejected because:** Recommendations are presentation logic. The backend should return raw DNS data. Analysis rules (what's required, what's recommended) may change and are easier to iterate on in the frontend. This follows the existing pattern where the backend returns data and the UI interprets it.

### 4. Create a separate "DNS Health" module
A separate tab/page for DNS health that aggregates data from multiple sources.

**Partially accepted:** Health is a tab within the domain detail page, not a separate page. This keeps the domain as the central organizational unit.

### 5. Remove the existing domains table entirely
Replace the list page with just a search/detail flow.

**Rejected because:** The list view is useful for overview. We enhance it with live status columns instead.

---

## Risks

| Risk | Mitigation |
|------|------------|
| DNS resolution can be slow (multiple record types × many domains) | Cache results per domain for 60 seconds. Show stale data while refreshing. Only check visible records. |
| DNS resolution may fail (firewall, no DNS) | Graceful degradation — show "Unable to check" badge. API returns 502 with clear message. |
| Large number of domains (100+) could cause many API calls | Batch DNS check. Add rate limiting. The detail page only checks one domain at a time. |
| The single `web/app.js` file grows too large | This is an existing risk. The new code follows existing patterns. A future refactoring could split into modules. |
| DMARC Wizard suggests policies that break email | Add clear warnings: "Start with p=none to monitor. Only escalate after monitoring." |

---

## Validation Plan

### Unit tests
- `DnsCheckService::check()` returns correct records for known domains
- `DnsCheckService::check()` handles resolution failures gracefully
- DomainViewService enrichment includes new mail fields

### Integration tests
- `GET /api/domains/example.com/dns-check` returns valid JSON
- Extended `GET /api/domains` includes new fields when MailDomain exists
- Extended `GET /api/domains` omits new fields (or returns null) when no MailDomain

### Web UI validation
- Domain list shows correct health indicators for all states
- Domain detail loads all 5 tabs without errors
- DMARC Wizard generates correct TXT records for all 3 policies
- Copy buttons work for all DNS record types
- Mail tab shows clean "not configured" state when no MailDomain exists
- Health score calculation matches across all test scenarios

### Manual validation
- Open a domain with full configuration → all green, high health score
- Open a domain with no mail → mail tab shows clean state, no false errors
- Open a domain with DKIM generated but not published → shows "Generated" but DNS check shows "Missing"
- Copy each button type → verifies clipboard content is correct

---

## Implementation order

1. **Backend:** `DnsCheckService` + API endpoint `GET /api/domains/<domain>/dns-check`
2. **Backend:** Extend `GET /api/domains` with mail domain fields
3. **Frontend:** Enhanced domain list (live columns + health score)
4. **Frontend:** Domain detail page → Overview tab
5. **Frontend:** Domain detail page → DNS Records tab with copy helpers
6. **Frontend:** Domain detail page → Mail tab with conditional display
7. **Frontend:** Domain detail page → Security tab with DMARC Wizard
8. **Frontend:** Domain detail page → Health tab
9. **Documentation:** Update `docs/api/API_REFERENCE.md` with new endpoint
10. **Tests:** Unit + integration tests for backend
11. **Changelog:** Record in CHANGELOG.md

---

## Appendix: Mockup reference

### Domain list (enhanced)

```
Domains  (12 domains)

┌─────┬──────────┬──────┬────────┬──────┬──────┬──────┬──────┬────────┐
│ #   │ Domain   │ Type │ Site   │ DNS  │ Mail │ SSL  │ HTTP │ Health │
├─────┼──────────┼──────┼────────┼──────┼──────┼──────┼──────┼────────┤
│ 1   │ example  │ prim │ MySite │  🟢  │  ✅  │  🟢  │  🟢  │  95%   │
│     │ .com     │      │        │      │      │      │      │        │
│ 2   │ test.org │ alias│ MySite │  ⚠️  │  ⬜  │  🟢  │  🟢  │  65%   │
│ 3   │ old.net  │ prim │ Old    │  ❌  │  ⬜  │  ❌  │  ❌  │  15%   │
└─────┴──────────┴──────┴────────┴──────┴──────┴──────┴──────┴────────┘
```

### Domain detail — Overview tab

```
Domains / example.com                       [Open] [Copy] [Remove]

Health Score: 95/100 (Excellent)                          [History]

┌──────────────────────┬──────────────────────┬──────────────────────┐
│ DNS                  │ Mail                 │ SSL                  │
│ A     ✅ 192.168.1.1 │ Domain: example.com  │ Certificate: Active  │
│ AAAA  ⚠️ Missing     │ Mode: Local Primary  │ HTTPS: Enabled       │
│ MX    ✅ mail.ex.com  │ DKIM: ✅ Generated   │ Redirect: Enabled    │
│ SPF   ✅ v=spf1 ...   │ Mailboxes: 3         │ Expires: 2026-10-06 │
│ DKIM  ✅ Found in DNS │ PHP Mail: Enabled    │ Issuer: Let's Encrypt│
│ DMARC ✅ p=quarantine │                     │                     │
└──────────────────────┴──────────────────────┴──────────────────────┘

Required Records                          Recommended Records
┌────────────────────────────────────┐   ┌────────────────────────────┐
│ ✅ MX    → mail.example.com       │   │ ⚠️ Autodiscover → Add     │
│ ✅ SPF   → v=spf1 mx ~all         │   │ ⚠️ MTA-STS     → Add     │
│ ✅ DKIM  → Generated + Published  │   │ ⚠️ TLS-RPT     → Add     │
│ ✅ DMARC → p=quarantine            │   │ ℹ️  CAA        → Add     │
└────────────────────────────────────┘   └────────────────────────────┘
```

### DMARC Wizard

```
DMARC Policy Wizard

[Monitor]         [Quarantine]         [Reject]
  p=none            p=quarantine         p=reject
  Monitor only      Tag as spam          Block delivery

Preivew:
_dmarc.example.com. 3600 IN TXT "v=DMARC1; p=quarantine; rua=mailto:dmarc@example.com"

[Copy Record]    [Copy with RUA]

⚠️ Start with p=none to monitor, then escalate to quarantine after 1-2 weeks.
```
