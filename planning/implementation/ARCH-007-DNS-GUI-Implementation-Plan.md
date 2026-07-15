# ARCH-007 — DNS GUI Implementation Plan

> **Based on:** `planning/proposals/ARCH-007-DNS-GUI-Redesign.md` v4
>
> **Status:** Draft  
> **Total phases:** 10  
> **Total checklist items:** 84  
> **Definition of Done:** see Phase 10

---

## Phase 0 — Existing Code Verification (prerequisite)

Before any implementation, verify the current state of all files that will be touched.

- [ ] Verify `Domain::site_id` field semantics in `libs/domain/Domain.h:13` — `uint64_t site_id = 0`. Confirm: `site_id=0` means domain is not linked to any ContainerCP site (including admin panel). The user requires `site_id >= 0` to be treated as "has a site" for applicability.
- [ ] Verify `DomainViewService::write_enriched()` output at `libs/domain/DomainViewService.cpp:37-47` — current JSON keys: `id`, `domain`, `type`, `site_id`, `site_name`, `site_domain`, `target`, `ssl_enabled`, `ssl_status`, `enabled`. No `dns` or `mail` fields exist yet.
- [ ] Verify `loadDomains()` in `web/app.js:842-889` — DNS column at line 872 is hardcoded `"Unknown"`. HTTP column at line 873 is hardcoded `"Unknown"`.
- [ ] Verify `navigate()` dispatch at `web/app.js:261-286` — all page handlers. Must add `domain-detail` case.
- [ ] Verify `buildTable()` helper at `web/app.js:345-351` — signature: `buildTable(columns, rows, emptyMsg)`. Used for domain list; domain detail will need custom layout.
- [ ] Verify `MailDomain::dkim_public_key_dns` field at `libs/mail/MailDomain.h:41` — source of truth for DKIM configured value.
- [ ] Verify `MailDomainManager::find_by_domain()` at `libs/mail/MailDomainManager.h:40` — used to detect if a MailDomain exists for a given domain name.
- [ ] Verify `DkimManager::generate_key()` signature at `libs/mail/DkimManager.h:29-34` — returns `std::string` (TXT value).
- [ ] Verify `CertificateStore::load_metadata(site_id)` at `libs/ssl/CertificateStore.h:83` — returns `MetadataLoadResult`. Used for SSL status.
- [ ] Verify runtime API response at `libs/api/ApiServer.cpp:453-488` — `GET /api/runtime/<site_id>` returns `{"web":"Running","php":"Running","db":"Running","cache":"Running","https":"Active"}`.
- [ ] Verify `MailDomain` JSON format in `mail_domain_json` lambda at `libs/api/ApiServer.cpp:1479-1498` — keys: `id`, `domain`, `mode`, `domain_id`, `site_id`, `enabled`, `relay_host`, `dkim_selector`, `dkim_public_key_dns`, `max_mailboxes`, `max_aliases`, `catch_all`, `created_at`, `updated_at`.
- [ ] Verify test framework at `tests/main.cpp:1-2` — doctest single-header. Test pattern: `TEST_CASE("name") { CHECK(...); }`.
- [ ] Verify API addition procedure at `docs/development/api-rules.md:49-77` — 8-step process. Must follow for new endpoint.
- [ ] Verify `Config::data_root()` exists at `libs/core/Config.h:14` — will be needed for dig path detection.
- [ ] Verify `router_.add_prefix()` usage pattern at `libs/api/ApiServer.cpp:453,1590,1630` — prefix routing works by path remainder parsing.

---

## Phase 1 — DNS Check Backend Service

### 1.1 — Create `libs/dns/` directory and `DnsCheckService`

**Files:** NEW: `libs/dns/DnsCheckService.h`, `libs/dns/DnsCheckService.cpp`  
**Depends on:** Phase 0  
**Criteria:** Service compiles, dig invocations work, structured output returned.

- [ ] Create `libs/dns/CMakeLists.txt` — add `DnsCheckService.cpp` to the build. Follow pattern from other lib CMakeLists.
- [ ] Create `libs/dns/DnsCheckService.h` with:
  - `struct DnsRecord` — fields: `type` (string), `name` (string), `value` (string), `ttl` (int), `priority` (int, default 0), `raw` (string — full dig line for evidence panel)
  - `struct DnsCheckResult` — fields: `domain` (string), `resolved_at` (string, ISO 8601), `records` (vector<DnsRecord>), `soa` (struct with `mname`, `rname`, `serial`), `success` (bool), `error` (string)
  - `DnsCheckResult check(const std::string& domain, const std::vector<std::string>& record_types)` — main method
  - Cache methods: `set_cache_ttl(int seconds)`, `clear_cache(const std::string& domain)`, `bool has_cached(const std::string& domain)` — in-memory cache with TTL
- [ ] Implement `DnsCheckService::check()` in `DnsCheckService.cpp`:
  - Validate domain: lowercase, alphanumeric, dots, hyphens only. Reject IPs, spaces, shell chars. Max length 253 chars.
  - Validate record_types against allowlist: `A`, `AAAA`, `MX`, `TXT`, `CNAME`, `NS`, `SOA`, `CAA`. Reject unknown types.
  - Execute `dig +noall +answer +time=5 +tries=2 <domain> <type>` via `popen()` for each requested type.
  - Parse each output line:
    - A: `name ttl IN A value`
    - AAAA: `name ttl IN AAAA value`
    - MX: `name ttl IN MX priority value`
    - TXT: `name ttl IN TXT "value"` — join fragments for long DKIM records
    - CNAME: `name ttl IN CNAME value`
    - NS: `name ttl IN NS value`
    - SOA: `name ttl IN SOA mname rname serial ...`
    - CAA: `name ttl IN CAA flag tag value`
  - Store full raw line in `DnsRecord::raw` for evidence panel.
  - Handle `NXDOMAIN`, `SERVFAIL`, timeout, empty response → structured errors, not exceptions.
  - Set timeout via `+time=5` (seconds), retries via `+tries=2`.
- [ ] Implement caching:
  - Use `std::map<std::string, std::pair<time_t, DnsCheckResult>>` for in-memory cache.
  - Default TTL: 60 seconds.
  - `clear_cache(domain)` removes entry — called when `?refresh=1`.
  - Cache key format: `domain:type1,type2` (sorted types).
- [ ] Add `#include` guard and `namespace containercp::dns`.
- [ ] Update root `CMakeLists.txt` to include `libs/dns/` subdirectory.
- [ ] **Tests:** `tests/test_dns_service.cpp` — see Phase 8 for full test list.

**Tests for 1.1:**
- Domain validation: valid (`example.com`), invalid (`rm -rf /`, ``, `192.168.1.1`)
- Type allowlist: valid (`A`, `MX`), invalid (`SPF`, `DMARC`, `INVALID`)
- A record parsing: `example.com. 3600 IN A 192.168.1.1`
- AAAA record parsing: `example.com. 3600 IN AAAA 2001:db8::1`
- MX parsing with priority: `example.com. 3600 IN MX 10 mail.example.com.`
- TXT parsing with quoted fragments
- DKIM long TXT fragment joining
- NXDOMAIN handling
- SERVFAIL handling
- Timeout handling
- Empty response handling
- Cache hit returns cached data
- `clear_cache` removes entry
- Cache TTL expiry
- Raw evidence line preserved in `DnsRecord::raw`

---

## Phase 2 — DNS Check REST API

### 2.1 — Register `GET /api/domains/<domain>/dns-check` endpoint

**Files:** `libs/api/ApiServer.cpp` (NEW handler), `libs/api/ApiServer.h` (no changes needed)  
**Depends on:** 1.1  
**Criteria:** Endpoint accepts requests, calls DnsCheckService, returns structured JSON.

- [ ] In `ApiServer.cpp`, after existing domain handlers (around line 348), add:
  ```cpp
  router_.add("GET", "/api/domains/<domain>/dns-check", [&s](const Request& req) {
      // parse domain from path
      // check for ?refresh=1 and ?types=A,MX,TXT query params
      // call s.dns_check().check(domain, types)
      // return structured JSON response
  });
  ```
- [ ] Parse domain from path: extract between `/api/domains/` and `/dns-check`. Validate (same rules as DnsCheckService).
- [ ] Parse optional query parameters:
  - `refresh=1` → call `clear_cache(domain)` before check
  - `types=A,AAAA,MX,TXT` → split by comma, validate each against allowlist. Default: all supported types.
- [ ] Handle errors:
  - Invalid domain → 400 `{"success":false,"error":"Invalid domain format"}`
  - Invalid type → 400 `{"success":false,"error":"Unsupported DNS record type: SPF"}`
  - DNS resolution failure → 502 `{"success":false,"error":"DNS resolution failed: timeout"}`
- [ ] Success response format (matches proposal v4):
  ```json
  {
    "success": true,
    "data": {
      "domain": "example.com",
      "resolved_at": "2026-07-15T12:00:00Z",
      "cached": false,
      "records": [
        {"type": "A", "name": "example.com", "value": "192.168.1.1", "ttl": 3600, "raw": "example.com. 3600 IN A 192.168.1.1"},
        {"type": "MX", "name": "example.com", "value": "mail.example.com", "ttl": 3600, "priority": 10, "raw": "..."}
      ],
      "soa": {
        "mname": "ns1.example.com",
        "rname": "admin.example.com",
        "serial": 2026071501
      }
    }
  }
  ```
- [ ] Register `DnsCheckService` in `ServiceRegistry` — follow existing pattern (e.g., how `DkimManager` is registered).
- [ ] Add `dns_check_service` accessor to `ServiceRegistry` — e.g., `dns_check() → DnsCheckService&`.
- [ ] Add `#include "dns/DnsCheckService.h"` to `ApiServer.cpp`.
- [ ] **Tests:** `tests/test_dns_api.cpp` — API-level tests (see Phase 8).

### 2.2 — Update `DomainViewService` to include mail info

**Files:** `libs/domain/DomainViewService.h`, `libs/domain/DomainViewService.cpp`
**Depends on:** Phase 0  
**Criteria:** `GET /api/domains` response includes `mail_domain_id`, `dkim_generated`, `dkim_selector`, `dkim_public_key_dns` fields.

- [ ] Add `MailDomainManager& mail_domains_` member to `DomainViewService` (pass through constructor).
- [ ] In `write_enriched()`, after existing fields, look up `MailDomain* md = mail_domains_.find_by_domain(d.fqdn)`.
- [ ] If found, append to JSON:
  ```json
  "mail_domain_id": <id>,
  "mail_domain_mode": "<mode>",
  "dkim_selector": "<selector>",
  "dkim_generated": <true|false>,
  "dkim_public_key_dns": "<value>"
  ```
- [ ] If not found, append:
  ```json
  "mail_domain_id": 0,
  "mail_domain_mode": "",
  "dkim_selector": "",
  "dkim_generated": false,
  "dkim_public_key_dns": ""
  ```
- [ ] Update `DomainViewService` constructor callers in `ApiServer.cpp` (pass `s.mail()`).
- [ ] **Tests:** `tests/test_domain_view.cpp` — verify mail fields present/absent.

---

## Phase 3 — Domain List GUI (Enhanced)

### 3.1 — Add `domain-detail` navigation route

**Files:** `web/app.js`  
**Depends on:** Phase 0  
**Criteria:** Clicking a domain name navigates to `/domains/<id>`.

- [ ] In `navigate()` at line 270, add:
  ```js
  else if (page === 'domain-detail') loadDomainDetail(p, params);
  ```
- [ ] Update `loadDomains()` table — change domain column link from opening URL to:
  ```js
  onclick="navigate('domain-detail', r.id)"
  ```
- [ ] Add sidebar active state: `domain-detail` highlights `domains` nav link.

### 3.2 — Replace DNS column with live fetch

**Files:** `web/app.js`  
**Depends on:** 2.1  
**Criteria:** DNS column shows real status instead of `"Unknown"`.

- [ ] Replace line 872:
  ```js
  {label:'DNS', html: r => `<span class="badge badge-info" id="dns-${r.id}">...</span>`}
  ```
- [ ] After `renderTable()`, add async batch DNS check for visible rows:
  ```js
  async function updateDnsColumn(rows) {
    for (const r of rows) {
      try {
        const res = await api(`/api/domains/${r.domain}/dns-check?types=A,AAAA,MX`);
        // Compute status based on found records
        // Update span#dns-${r.id} text + badge class
      } catch(e) {
        // Show "Error" badge
      }
    }
  }
  ```
- [ ] Add staggered loading: use `Promise.allSettled` with a semaphore (max 3 concurrent requests) to avoid DNS storm.
- [ ] Cache DNS results client-side in `window._dnsCache` (Map<domain, {results, timestamp}>) with 60s TTL.
- [ ] Handle empty state, error state, loading state.

### 3.3 — Add Health Score column

**Files:** `web/app.js`  
**Depends on:** 3.2  
**Criteria:** Each domain row shows a colored Health Score percentage.

- [ ] After DNS column, add Health Score column:
  ```js
  {label:'Health', html: r => {
    const cached = window._healthCache?.[r.id];
    if (!cached) return '<span class="badge badge-info">...</span>';
    return `<span class="badge ${gradeClass(cached.grade)}">${cached.score}%</span>`;
  }}
  ```
- [ ] Implement `computeHealthScore(domainRow, dnsResults, mailDomainData)`:
  - Input: domain enriched data (has `site_id`, `ssl_enabled`, `mail_domain_id`), DNS check results, mail domain data
  - Apply context-aware model: determine applicable checks based on `site_id >= 0`, `mail_domain_id > 0`, `ssl_enabled`
  - Assign weights per proposal v4 Health Score Model table
  - Normalise to 100
  - Return `{ score, grade, breakdown }`
- [ ] Store computed scores in `window._healthCache` (Map<domainId, {score, grade, timestamp}>).
- [ ] Add `gradeClass(grade)` helper → badge color: Excellent=green, Good=blue, Fair=yellow, Poor=orange, Critical=red.

### 3.4 — Add Mail and HTTP status columns

**Files:** `web/app.js`  
**Depends on:** 2.2  
**Criteria:** Mail column shows Active/Not configured, HTTP shows status.

- [ ] Replace HTTP column (line 873) — use enriched data from `GET /api/domains` + runtime API:
  ```js
  {label:'HTTP', html: r => {
    if (!r.site_id && r.site_id !== 0) return '<span class="badge badge-info">N/A</span>';
    return `<span class="badge badge-info" id="http-${r.id}">...</span>`;
  }}
  ```
- [ ] After renderTable, batch-fetch runtime status for rows with `site_id >= 0`.
- [ ] Replace Mail column — use `mail_domain_id` from enriched JSON (added in 2.2):
  ```js
  {label:'Mail', html: r => {
    if (!r.mail_domain_id) return '<span class="badge badge-info">Not configured</span>';
    return '<span class="badge badge-ok">Active</span>';
  }}
  ```
- [ ] Handle loading: show `"..."` placeholder, update in place after async fetch.
- [ ] Cache runtime results per site_id with 30s TTL.

---

## Phase 4 — Domain Detail GUI (Base Layout)

### 4.1 — Create `loadDomainDetail()` with tab navigation

**Files:** `web/app.js`  
**Depends on:** 3.1  
**Criteria:** Clicking a domain opens a detail page with 5 tabs (Overview, DNS Records, Mail, Security, Health).

- [ ] Implement `async function loadDomainDetail(p, domainId)`:
  ```js
  async function loadDomainDetail(p, domainId) {
    // Fetch domain data: GET /api/domains → find by id
    // Fetch mail domain data: GET /api/mail/domains
    // Fetch SSL data: GET /api/ssl/<domain>
    // Render header with back link, domain name, action buttons
    // Render Health Score badge in header
    // Render 5 tabs
    // Activate first tab (Overview) by default
    // Each tab click updates #domain-tab-content
  }
  ```
- [ ] Use existing `.tabs` / `.tab` CSS classes from `style.css:103-107`.
- [ ] Tab IDs: `overview`, `dns-records`, `mail`, `security`, `health`.
- [ ] Tab switching: hide all, show selected. Load content on first activation (lazy).
- [ ] Breadcrumb: `Domains / example.com` (existing pattern from mail detail).
- [ ] Header actions: `[Open in browser]` `[Copy domain]` `[Remove]`.
- [ ] Health Score badge in header: use `computeHealthScore()` from 3.3.
- [ ] Loading state: `<div class="empty-state">Loading...</div>`.
- [ ] Error state: `<div class="empty-state">Failed to load domain</div>` with retry button.
- [ ] **Caching:** domain data cached for 30s, health score recomputed on tab switch.

### 4.2 — Implement Overview tab

**Files:** `web/app.js`  
**Depends on:** 4.1, 2.1  
**Criteria:** Overview shows DNS check summary, Mail status, SSL status, Site info.

- [ ] Fetch DNS check: `GET /api/domains/<domain>/dns-check`.
- [ ] Render DNS summary table with columns: Type, Configured, Published, Status.
- [ ] For each expected record type (A, AAAA, MX, SPF, DKIM, DMARC):
  - Determine **Configured** value:
    - DKIM: from `MailDomain::dkim_public_key_dns`
    - SPF: template `v=spf1 mx ~all` if MailDomain exists
    - DMARC: from DMARC Wizard selection (stored in localStorage per domain, fallback: no configured value)
    - A/AAAA/MX: `"—"` (no expected value in ContainerCP)
  - Determine **Published** value: from DNS check response.
  - Determine **Status**: Match / Mismatch / Not Published / Not Configured / Not Applicable.
- [ ] Render Mail card: domain, mode, DKIM status, mailboxes count. If no MailDomain: "Not configured".
- [ ] Render SSL card: status, HTTPS enabled, redirect, expiry date.
- [ ] Render Site card: site name, backend, PHP version, runtime status.
- [ ] Add `[Check Again]` button → re-fetches DNS check, updates table.
- [ ] Empty state: all records n/a → show neutral message.
- [ ] Error state: DNS check fails → show error with retry.

---

## Phase 5 — DNS Records Tab (Configured vs Published)

### 5.1 — Implement DNS Records tab

**Files:** `web/app.js`  
**Depends on:** 4.1, 2.1  
**Criteria:** Full DNS records table showing Type, Name, Configured, Published, Status, TTL, Actions.

- [ ] Tab content: full-width record table with columns:
  | Status | Type | Name | Configured | Published | TTL | Actions |
- [ ] Record order: A, AAAA, MX, TXT (SPF), TXT (DKIM), TXT (DMARC), CNAME, NS, CAA, MTA-STS.
- [ ] For each record, determine `recordStatus(configured, published)`:
  ```js
  function recordStatus(cfg, pub) {
    if (!cfg && !pub) return {code:'NOT_CONFIGURED', label:'—'};
    if (cfg && !pub)  return {code:'NOT_PUBLISHED', label:'Not Published', class:'badge-err'};
    if (!cfg && pub)  return {code:'FOUND', label:'Found', class:'badge-ok'};
    if (cfg === pub)  return {code:'MATCH', label:'Match', class:'badge-ok'};
    return {code:'MISMATCH', label:'Mismatch', class:'badge-warn'};
  }
  ```
- [ ] **Configured values per record type:**
  | Type | Configured source |
  |------|------------------|
  | A | `null` (no expected value) |
  | AAAA | `null` (informational) |
  | MX | `null` (no expected value) |
  | SPF | Template: `v=spf1 mx ~all` if MailDomain exists AND mode is local-primary |
  | DKIM | `MailDomain::dkim_public_key_dns` if present |
  | DMARC | localStorage value from DMARC Wizard selection |
  | CAA | Template: `0 issue "letsencrypt.org"` (optional) |
  | MTA-STS | Template: `v=STSv1; id=1` |
  | TLS-RPT | Template: `v=TLSRPTv1; rua=mailto:...` |
  | Autodiscover | URL template |
- [ ] **Published values:** from `GET /api/domains/<domain>/dns-check` response records.
- [ ] **Copy actions per record type:**
  | Type | Buttons |
  |------|---------|
  | A, AAAA | [Copy Value] |
  | MX | [Copy Value] |
  | SPF | [Copy Record] |
  | DKIM | [Copy Host] [Copy Value] [Copy FQDN] [Copy Full] |
  | DMARC | [Copy Record] [Copy with RUA] |
  | CAA | [Copy Record] |
  | MTA-STS | [Copy TXT] [Copy Policy Template] |
  | Autodiscover | [Copy Record] |
- [ ] Copy helpers (reuse pattern from Mail DKIM at `web/app.js:1136-1204` — `data-copy` attributes + event listeners):
  ```js
  function copyDkimRecord(domain, part) {
    // part: 'host', 'value', 'fqdn', 'full'
    // Construct value based on DKIM data + domain
    navigator.clipboard.writeText(value).then(() => toast('Copied', 'success'));
  }
  ```
- [ ] Add `[Check Again]` button → re-fetches DNS check, re-renders table.
- [ ] Show "Last checked: X minutes ago" below table.

### 5.2 — Add Configured/Published/Status labels for mail records

**Files:** `web/app.js`  
**Depends on:** 5.1  
**Criteria:** Mail records (SPF, DKIM, DMARC) clearly show Configured vs Published.

- [ ] For DKIM row: if `dkim_public_key_dns` exists, show first 60 chars + `...` in Configured column. Full value in tooltip or copy button.
- [ ] For SPF row: if MailDomain exists in local-primary mode, show `v=spf1 mx ~all` in Configured. Otherwise show `—`.
- [ ] For DMARC row: if DMARC Wizard value exists in localStorage, show it in Configured. Otherwise show "Not configured".
- [ ] Status column uses `recordStatus()` from 5.1.

---

## Phase 6 — Mail Tab and Conditional Display

### 6.1 — Implement Mail tab

**Files:** `web/app.js`  
**Depends on:** 4.1, 2.1, 5.1  
**Criteria:** Mail tab shows mail configuration with conditional display based on MailDomain existence.

- [ ] Fetch `GET /api/mail/domains` → find MailDomain for this domain.
- [ ] **Scenario A — MailDomain exists:**
  - Show mail domain details: mode, status, mailboxes count, aliases count.
  - Required records section: MX, SPF, DKIM, DMARC with Configured/Published/Status.
  - Recommended records section: Autodiscover, MTA-STS, TLS-RPT, CAA.
  - For each record: show Copy buttons + `[Why?]` if status ≠ Match.
  - PHP Mail status card (reuse `loadPhpMailCard` pattern from `web/app.js:581-654`).
- [ ] **Scenario B — No MailDomain:**
  ```html
  <div class="card">
    <h3>Mail</h3>
    <p>Mail service is not configured for this domain.</p>
    <p>MX, SPF, DKIM, DMARC are not shown as errors because mail is not in use.</p>
    <button class="btn btn-sm btn-primary" onclick="navigate('mail')">
      Enable Mail for this Domain
    </button>
  </div>
  ```
- [ ] Verify: **no false errors** for MX/SPF/DKIM/DMARC when MailDomain absent.
- [ ] Verify: all mail checks get `not_applicable` status when no MailDomain.
- [ ] Verify: Health Score is NOT penalised for absent mail (handled by `computeHealthScore`).

---

## Phase 7 — Security Tab and Evidence Panels

### 7.1 — Implement Security tab with DMARC Wizard

**Files:** `web/app.js`  
**Depends on:** 4.1, 5.2  
**Criteria:** DMARC Wizard generates correct TXT records. CAA, MTA-STS, TLS-RPT recommendations shown.

- [ ] DMARC current status: show Configured vs Published with status badge.
- [ ] Three policy cards:
  - Monitor: `v=DMARC1; p=none;`
  - Quarantine: `v=DMARC1; p=quarantine; rua=mailto:dmarc@<domain>`
  - Reject: `v=DMARC1; p=reject; rua=mailto:dmarc@<domain>`
- [ ] On card click (or radio select):
  - Show preview of full DNS record.
  - Store selection in `localStorage['dmarc_' + domain]`.
  - Show `[Copy Record]` and `[Copy with RUA]` buttons.
- [ ] MTA-STS section:
  - TXT record template: `v=STSv1; id=1`
  - Policy file template (informational, not hosted by ContainerCP).
  - `[Copy TXT]` and `[Copy Policy Template]` buttons.
- [ ] CAA section:
  - Template: `0 issue "letsencrypt.org"`
  - `[Copy Record]` button.
- [ ] TLS-RPT section:
  - Template: `v=TLSRPTv1; rua=mailto:tls-reports@<domain>`
  - `[Copy Record]` button.
- [ ] Autodiscover section:
  - Template: autodiscover URL for email client configuration.
  - `[Copy Record]` button.

### 7.2 — Implement Evidence / Show Details panel

**Files:** `web/app.js`  
**Depends on:** 5.1, 7.1  
**Criteria:** Every non-Match record shows `[Why?]` or `[Show Details]` with expandable evidence.

- [ ] Implement `async function showEvidence(recordType, configured, published, rawDig, domain)`:
  - Receives record metadata.
  - Generates inline HTML panel below the record row:
    ```html
    <div class="evidence-panel">
      <div class="evidence-section">
        <strong>Configured (ContainerCP)</strong>
        <pre>configured_value</pre>
      </div>
      <div class="evidence-section">
        <strong>Published (public DNS)</strong>
        <pre>published_value</pre>
      </div>
      <div class="evidence-section">
        <strong>Reason</strong>
        <p>human_readable_explanation</p>
      </div>
      <div class="evidence-section">
        <strong>Raw DNS Response</strong>
        <pre>escaped_raw_dig_output</pre>
      </div>
      <button class="btn btn-sm btn-primary">Copy Correct Record</button>
      <button class="btn btn-sm" onclick="this.closest('.evidence-panel').remove()">Dismiss</button>
    </div>
    ```
- [ ] **Reason codes table (frontend-generated, not from backend):**
  | Code | Template |
  |------|----------|
  | `DKIM_NOT_PUBLISHED` | ContainerCP generated the DKIM key pair, but no TXT record exists at `<selector>._domainkey.<domain>`. Add this record to your DNS provider. |
  | `DKIM_KEY_MISMATCH` | The public key in DNS differs from the one ContainerCP generated. This may happen if the key was regenerated without updating DNS. Copy the new record below. |
  | `SPF_NOT_FOUND` | No SPF record found in DNS. Without SPF, spammers can forge emails from your domain. Add `v=spf1 mx ~all` to authorize your mail servers. |
  | `DMARC_POLICY_MISMATCH` | The DMARC policy field (p=) differs. Configured: `<value>`. Published: `<value>`. Update your DMARC TXT record at `_dmarc.<domain>`. |
  | `MX_NOT_FOUND` | No MX record found in DNS. Email delivery to this domain will fail. |
  | `SSL_EXPIRING` | Certificate expires in `<N>` days. Renew via the SSL section. |
  | `CAA_MISSING` | No CAA record found. Any CA can issue certificates. Add `0 issue "letsencrypt.org"` to restrict to Let's Encrypt. |
  | `DNS_LOOKUP_FAILED` | Public DNS lookup failed. Check your DNS provider or try again later. |
- [ ] Generate reason based on: `recordType` + `configured` + `published` + domain context.
- [ ] Accordion behavior: only one evidence panel open at a time.
- [ ] `[Check Again]` auto-closes all open evidence panels.
- [ ] **Security:** `esc()` (existing function at `web/app.js:58`) applied to all raw DNS output before rendering.

### 7.3 — Implement "How to fix" guidance

**Files:** `web/app.js`  
**Depends on:** 7.2  
**Criteria:** Each evidence panel includes actionable fix guidance.

- [ ] Add "How to fix" section to evidence panel:
  ```html
  <div class="evidence-section">
    <strong>How to fix</strong>
    <ol>
      <li>Copy the correct DNS record using the button below.</li>
      <li>Log in to your DNS provider's control panel.</li>
      <li>Navigate to the DNS zone for <domain>.</li>
      <li>Add or update the TXT record with the copied value.</li>
      <li>Click "Check Again" below to verify the change.</li>
    </ol>
  </div>
  ```
- [ ] Steps are context-specific per reason code (e.g., for DKIM it's "Add a TXT record", for DMARC it's "Update the existing TXT record").

---

## Phase 8 — Health Tab

### 8.1 — Implement Health tab

**Files:** `web/app.js`  
**Depends on:** 4.1, 3.3  
**Criteria:** Health tab shows detailed breakdown with context-aware scoring.

- [ ] Reuse `computeHealthScore()` from 3.3 to get `{ score, grade, breakdown }`.
- [ ] Render detailed breakdown table:
  | Check | Status | Weight | Configured | Published | Score |
- [ ] Applicability logic in `computeHealthScore()`:
  ```js
  function getApplicableChecks(domain, mailDomain, dnsData) {
    const checks = [];
    // Always applicable:
    checks.push({id:'a', label:'A record', weight:25, class:'req'});
    checks.push({id:'aaaa', label:'AAAA (IPv6)', weight:0, class:'info'});
    // Mail-dependent:
    if (mailDomain) {
      checks.push({id:'mx', label:'MX', weight:12, class:'req'});
      checks.push({id:'spf', label:'SPF', weight:10, class:'req'});
      if (mailDomain.dkim_public_key_dns) {
        checks.push({id:'dkim', label:'DKIM', weight:10, class:'req'});
      }
      checks.push({id:'dmarc', label:'DMARC', weight:8, class:'req'});
      if (mailDomain.mode === 'local-primary') {
        checks.push({id:'mta-sts', label:'MTA-STS', weight:3, class:'rec'});
        checks.push({id:'autodiscover', label:'Autodiscover', weight:3, class:'rec'});
      }
    }
    // Site-dependent (site_id >= 0, including admin panel):
    if (domain.site_id >= 0) {
      checks.push({id:'ssl', label:'SSL Certificate', weight:20, class:'req'});
      checks.push({id:'http', label:'HTTP Reachability', weight:15, class:'req'});
      checks.push({id:'caa', label:'CAA', weight:2, class:'rec'});
    }
    return checks;
  }
  ```
- [ ] Normalise weights: `norm = 100 / sum(applicable_weights)`.
- [ ] Grade boundaries: 90-100 Excellent, 70-89 Good, 40-69 Fair, 1-39 Poor, 0 Critical.
- [ ] Show `[Check Again]` → re-fetches DNS check, recomputes score.
- [ ] Show "Last checked: X minutes ago" timestamp.
- [ ] Empty state: no applicable checks → "No checks applicable for this domain".
- [ ] Error state: DNS check fails → "Score unavailable. [Retry]".

### 8.2 — Health Score unit tests

**Files:** `web/app.js` test harness (or `tests/` if moved to backend)  
**Depends on:** 8.1  
**Criteria:** All scoring scenarios validated.

- [ ] **Test: site with full mail + SSL → expected 100/100**
- [ ] **Test: site without mail → mail checks n/a, score still 100 if all else ok**
- [ ] **Test: admin panel site_id=0 → SSL + HTTP checks active**
- [ ] **Test: unlinked domain → only A record check**
- [ ] **Test: DKIM not published → score reduced**
- [ ] **Test: DMARC mismatch → partial score**
- [ ] **Test: DNS lookup error → degraded score (we can't evaluate DNS-dependent checks)**
- [ ] **Test: all checks not_applicable → score = N/A**
- [ ] **Test: recommended checks missing → minor penalty**
- [ ] **Test: informational IPv6 missing → no penalty**

---

## Phase 9 — Tests

### 9.1 — Backend unit tests

**Files:** NEW: `tests/test_dns_service.cpp`, EXTEND: `tests/test_domain_view.cpp`  
**Depends on:** 1.1, 2.2  
**Criteria:** All DNS check service and domain view tests pass.

- [ ] Create `tests/test_dns_service.cpp` with doctest test cases:
  - [ ] Domain validation — valid domains pass
  - [ ] Domain validation — invalid domains rejected (shell chars, IPs, empty)
  - [ ] Record type allowlist — valid types pass
  - [ ] Record type allowlist — invalid types rejected
  - [ ] A record parsing — single value
  - [ ] AAAA record parsing — IPv6
  - [ ] MX record parsing — priority extracted correctly
  - [ ] TXT record parsing — quoted string with whitespace
  - [ ] DKIM long TXT — multi-fragment joining
  - [ ] NXDOMAIN response — structured error, no crash
  - [ ] SERVFAIL response — structured error, no crash
  - [ ] Timeout simulation — structured error
  - [ ] Empty response — empty records array
  - [ ] Cache hit — returns same data within TTL
  - [ ] Cache bypass — refresh=1 returns fresh data
  - [ ] Cache expiry — data older than TTL is re-fetched
  - [ ] Raw evidence — DnsRecord::raw contains full dig line
  - [ ] Max records limit — output truncated if too large
- [ ] Extend `tests/test_domain_view.cpp`:
  - [ ] Enriched JSON includes `mail_domain_id` when MailDomain exists
  - [ ] Enriched JSON has empty mail fields when no MailDomain
  - [ ] Backward compatible — old fields unchanged

### 9.2 — API integration tests

**Files:** NEW: `tests/test_dns_api.cpp`, or extend `tests/test_api.cpp`  
**Depends on:** 2.1, 2.2  
**Criteria:** API endpoint responds correctly to all request variants.

- [ ] `GET /api/domains/example.com/dns-check` → 200 with valid structure
- [ ] `GET /api/domains/example.com/dns-check?types=A,MX` → only A and MX records
- [ ] `GET /api/domains/example.com/dns-check?refresh=1` → fresh data (cache bypassed)
- [ ] `GET /api/domains/INVALID` → 400 with error message
- [ ] `GET /api/domains/example.com/dns-check?types=SPF` → 400 (unsupported type)
- [ ] `GET /api/domains/example.com/dns-check?types=A,SPF,INVALID` → 400 (first invalid detected)
- [ ] Extended `GET /api/domains` includes `mail_domain_id` for domains with MailDomain
- [ ] Extended `GET /api/domains` has empty mail fields for domains without MailDomain

### 9.3 — Frontend behaviour tests

**Files:** `web/app.js` (manual verification checklist)  
**Depends on:** 3.1–8.1  
**Criteria:** All UI states verified manually.

- [ ] Domain list shows DNS column with loading → badge → status
- [ ] Domain list shows Health Score column with all grade colors
- [ ] Domain list shows Mail column as Active/Not configured
- [ ] Domain list shows HTTP column with runtime status
- [ ] Domain detail: Overview tab renders with all data
- [ ] Domain detail: DNS Records tab shows Configured vs Published
- [ ] Domain detail: Mail tab shows correct conditional state
- [ ] Domain detail: Security tab with DMARC Wizard generates correct records
- [ ] Domain detail: Health tab shows breakdown with correct scoring
- [ ] Copy buttons: all types (Host/Value/FQDN/Full) copy correct text
- [ ] Evidence panel: `[Why?]` shows expected → actual → reason → raw → fix
- [ ] Evidence panel: `[Dismiss]` closes panel
- [ ] Evidence panel: only one open at a time (accordion)
- [ ] `[Check Again]` refreshes section, closes evidence panels
- [ ] Loading states show spinners or "..." placeholders
- [ ] Error states show error message with retry button
- [ ] MailDomain absent: no false errors, neutral message shown
- [ ] Site with site_id=0 (admin panel): HTTP + SSL checks active
- [ ] Health Score: site with mail = 100% all ok
- [ ] Health Score: site without mail = mail n/a, still 100% if others ok
- [ ] Health Score: DKIM missing = reduced
- [ ] Health Score: all n/a = N/A

---

## Phase 10 — Documentation and Finalisation

### 10.1 — Update API documentation

**Files:** `docs/api/API_REFERENCE.md`  
**Depends on:** 2.1, 2.2  
**Criteria:** New endpoint and new response fields documented.

- [ ] Add new section "2.23 DNS Check" after existing "2.22 Site Mail" section:
  - Endpoint: `GET /api/domains/<domain>/dns-check`
  - Purpose: Live DNS resolution check for a domain
  - Query params: `types` (optional, comma-separated), `refresh` (optional, 1/0)
  - Response schema with all fields
  - Error responses with status codes
  - Cache semantics (60s TTL, refresh=1 bypass)
  - Implementation: uses system `dig` via `DnsCheckService`
  - Example request and response
- [ ] Update section "2.9 Domains" `GET /api/domains` response to document new mail fields:
  - `mail_domain_id`, `mail_domain_mode`, `dkim_selector`, `dkim_generated`, `dkim_public_key_dns`

### 10.2 — Update project status

**Files:** `planning/project-status.md`  
**Depends on:** All phases complete  
**Criteria:** DNS-001–DNS-004 updated to reflect new scope.

- [ ] Mark DNS-001 (DnsCheckService) as ✅ Implemented
- [ ] Mark DNS-002 (REST API) as ✅ Implemented
- [ ] Mark DNS-003 (Domain detail Web UI) as ✅ Implemented
- [ ] Mark DNS-004 (Enhanced domain list) as ✅ Implemented
- [ ] Update description text to reflect read-only DNS check scope (not zone management)

### 10.3 — Update CHANGELOG

**Files:** `CHANGELOG.md`  
**Depends on:** All phases complete  
**Criteria:** One changelog entry per phase or per logical commit.

- [ ] Add entry for DNS check backend service
- [ ] Add entry for DNS check REST API
- [ ] Add entry for enhanced domain list GUI
- [ ] Add entry for domain detail GUI with tabs
- [ ] Add entry for Configured vs Published comparison
- [ ] Add entry for Evidence/Why/How to fix
- [ ] Add entry for Health Score
- [ ] Each entry includes: date, commit hash, summary, files changed, user-visible behaviour, validation result

### 10.4 — Update ADR if needed

**Files:** `docs/ADR/`  
**Depends on:** All phases complete  
**Criteria:** Architectural decision recorded if new patterns introduced.

- [ ] Evaluate if a new ADR is needed for:
  - The read-only DNS check pattern (DnsCheckService vs DnsProvider)
  - The Configured vs Published comparison model
  - The site_id=0 semantics clarification
- [ ] If needed, create new ADR following existing format

---

## Phase 10 (continued) — Definition of Done

ARCH-007 is considered **fully implemented** only when all of the following are true:

- [ ] `GET /api/domains/<domain>/dns-check` returns structured DNS records
- [ ] `GET /api/domains` includes `mail_domain_id`, `dkim_generated`, `mail_domain_mode`
- [ ] Domain list shows real DNS status (not "Unknown")
- [ ] Domain list shows Mail status (Active / Not configured)
- [ ] Domain list shows HTTP/runtime status
- [ ] Domain list shows Health Score with correct context-aware calculation
- [ ] Domain detail page opens with 5 tabs (Overview, DNS Records, Mail, Security, Health)
- [ ] DNS Records tab shows:
  - Type, Name, Configured, Published, Status, TTL, Actions
  - Copy Host / Copy Value / Copy FQDN / Copy Full for DKIM
  - Copy Record for SPF, DMARC, CAA, MTA-STS, TLS-RPT
- [ ] Configured vs Published comparison works for DKIM, SPF, DMARC
- [ ] Mail tab shows:
  - MailDomain exists → full mail configuration with checks
  - MailDomain absent → neutral informational message, no false errors
  - `site_id >= 0` → HTTP + SSL checks active (including admin panel)
- [ ] Security tab shows:
  - DMARC Wizard with 3 policies (Monitor/Quarantine/Reject)
  - Preview of generated TXT record
  - CAA, MTA-STS, TLS-RPT, Autodiscover recommendations
- [ ] Evidence/Why works for all non-Match records:
  - Expected (Configured)
  - Actual (Published)
  - Reason (human-readable)
  - Raw DNS response (HTML-escaped)
  - How to fix (actionable steps)
  - Copy correct record button
  - Dismiss button
  - Accordion (one panel at a time)
- [ ] Health tab shows:
  - Context-aware score (applicable checks only)
  - Detailed breakdown with weights
  - Correct normalisation to 100
  - Grade boundaries applied
- [ ] `[Check Again]` works on all tabs, closes evidence panels, fetches fresh data
- [ ] All tests pass (unit, API, frontend verification)
- [ ] API documentation updated (`docs/api/API_REFERENCE.md`)
- [ ] Project status updated (`planning/project-status.md`)
- [ ] CHANGELOG updated
- [ ] Existing ContainerCP functionality is not broken (regression verified)

---

## Commit boundaries

Logical commits that preserve a working state after each:

| # | Commit description | Phases included |
|---|-------------------|-----------------|
| 1 | `DnsCheckService + parser + unit tests` | 1.1 |
| 2 | `DNS check REST API + DomainViewService mail fields` | 2.1, 2.2 |
| 3 | `Domain list: DNS/Mail/HTTP/Health columns` | 3.1–3.4 |
| 4 | `Domain detail: base layout + Overview tab` | 4.1, 4.2 |
| 5 | `Domain detail: DNS Records tab (Configured vs Published)` | 5.1, 5.2 |
| 6 | `Mail tab with conditional MailDomain display` | 6.1 |
| 7 | `Security tab + DMARC Wizard + Evidence panels` | 7.1–7.3 |
| 8 | `Health tab with context-aware scoring` | 8.1, 8.2 |
| 9 | `Documentation + final tests + project status` | 9.1–9.3, 10.1–10.4 |

---

## Files to be created

| File | Phase | Purpose |
|------|-------|---------|
| `libs/dns/CMakeLists.txt` | 1.1 | Build configuration for DNS module |
| `libs/dns/DnsCheckService.h` | 1.1 | DnsCheckService class declaration |
| `libs/dns/DnsCheckService.cpp` | 1.1 | DnsCheckService implementation |
| `tests/test_dns_service.cpp` | 9.1 | Unit tests for DnsCheckService |
| `tests/test_dns_api.cpp` | 9.2 | API integration tests for dns-check endpoint |

## Files to be modified

| File | Phase | Change |
|------|-------|--------|
| `CMakeLists.txt` (root) | 1.1 | Add `libs/dns/` subdirectory |
| `libs/api/ApiServer.cpp` | 2.1 | Add `GET /api/domains/<domain>/dns-check` route |
| `libs/api/ApiServer.h` | 2.1 | (if needed) Add DnsCheckService reference |
| `libs/domain/DomainViewService.h` | 2.2 | Add `MailDomainManager&` parameter |
| `libs/domain/DomainViewService.cpp` | 2.2 | Add mail domain fields to enriched JSON |
| `web/app.js` | 3.1–8.1 | All GUI changes |
| `tests/test_domain_view.cpp` | 9.1 | Add mail field tests |
| `tests/test_api.cpp` | 9.2 | Add dns-check endpoint tests (or new file) |
| `tests/CMakeLists.txt` | 9.1 | Add new test source files |
| `docs/api/API_REFERENCE.md` | 10.1 | Document new endpoint and extended response |
| `planning/project-status.md` | 10.2 | Update DNS-001–DNS-004 status |
| `CHANGELOG.md` | 10.3 | Record all changes |
| `docs/ADR/` | 10.4 | New ADR if needed |

---

## Risks and uncertainties

| # | Risk | Mitigation |
|---|------|------------|
| 1 | **`dig` not installed** on target system. | Add `bind9-dnsutils` to system dependencies (install.sh, packaging). Detect at runtime, return clear error. |
| 2 | **`site_id=0` ambiguity** — Domain struct defaults to `site_id=0` for unlinking, but user says `site_id=0` is admin panel (which is in the proxy, not in Domain). | Need to clarify: does user want ALL domains with `site_id=0` treated as admin panel, or only specific admin panel domains? Implementation plan assumes `site_id >= 0` = applicable for site checks, as instructed. |
| 3 | **DMARC Wizard persistence** — currently no backend storage for DMARC selection. | Use `localStorage` per domain (keyed by `dmarc_<domain>`). This means DMARC expected value is browser-local, not synced across devices. Acceptable for v1. |
| 4 | **SPF expected value** — ContainerCP has no SPF configuration. The template `v=spf1 mx ~all` is a reasonable default but may not match the admin's actual mail setup. | Show SPF template as a "recommended" value, not a "configured" value. Mark status as advisory. Clearly label "Recommended SPF record" vs "Your current DNS value". |
| 5 | **Raw DNS response size** — large TXT records (DKIM 2048-bit keys) could produce long raw lines. | Truncate raw evidence display to first 500 chars, with "Show full" toggle. API returns full value but frontend truncates for display. |
| 6 | **No HTTP check API** — ContainerCP has no endpoint to check HTTP response codes/status. | For v1, the HTTP column shows runtime container status (Running/Stopped) from `GET /api/runtime/<site_id>`, NOT external HTTP reachability. Clearly label as "Runtime" not "HTTP". A true HTTP check endpoint can be added in a future iteration. |
| 7 | **`loadDomainDetail` depends on `GET /api/domains` list** — currently there's no `GET /api/domains/<id>` endpoint. | Option A: Add `GET /api/domains/<id>` (new endpoint, follows existing pattern from SSL and Mail). Option B: Fetch all domains and filter by ID (works for small datasets, doesn't scale). Plan assumes Option A for correctness. |
| 8 | **Evidence reason codes are frontend-only** — no backend standardisation. | Acceptable for v1. Frontend generates reasons from record type, configured value, published value, and context. Reason codes (e.g., `DKIM_NOT_PUBLISHED`) provide a stable key for future i18n. |
