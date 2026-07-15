# ARCH-007 — DNS GUI Implementation Plan

> **Based on:** `planning/proposals/ARCH-007-DNS-GUI-Redesign.md` v5
>
> **Status:** Draft  
> **Total phases:** 10  
> **Total checklist items:** 84  
> **Definition of Done:** see Phase 10

---

### Production inspection results (web2.softico.ua, 2026-07-15)

**Method:** Read-only SSH + curl inspection of live ContainerCP instance.

**Confirmed facts:**
- **Server hostname:** `web2.softico.ua` — used as admin panel hostname
- **Admin panel proxy:** Configured via system proxy entry at `/srv/containercp/proxy/sites/web2.softico.ua.conf` — proxies directly to daemon API on `172.17.0.1:8081`
- **SSL for admin panel:** Stored at `/srv/containercp/ssl/0/` — `site_id=0`. Metadata confirms: status `"active"`, `https_enabled: true`, Let's Encrypt, domains `["web2.softico.ua"]`, expires Oct 7 2026
- **No Site record:** SiteManager has no site with `id=0`. SSL store uses `site_id=0` as a special sentinel for the admin panel
- **No Domain record:** The admin panel hostname does NOT appear in `domains.db` — admin panel is not a managed Domain resource
- **Runtime API:** `GET /api/runtime/0` returns 400 ("Invalid site ID") — no Site record exists
- **Admin panel HTTP accessibility:** Both HTTP and HTTPS respond 200 OK via nginx proxy (`nginx/1.31.2`)
- **Other sites:** 9 sites total with site_ids 1-11 (not contiguous). SSL present at site_ids 4, 8, 11
- **Mail domains:** Present for `softi.co` domains with DKIM records

**Impact on implementation:**
- `site_id=0` in Domains means a Domain record was created without linking to a Site (default value). This does NOT correspond to the admin panel Domain (which doesn't exist as a Domain record).
- However, `CertificateStore::load_metadata(0)` DOES return the admin panel SSL — so SSL check for `site_id=0` domains is meaningful.
- Runtime API cannot check admin panel — Runtime column shows N/A for `site_id=0`.
- All existing implementation plan decisions remain correct.

---

## Phase 0 — Existing Code Verification (prerequisite)

Before any implementation, verify the current state of all files that will be touched.

- [x] Verify `Domain::site_id` field semantics in `libs/domain/Domain.h:13` — `uint64_t site_id = 0`. **Confirmed:** `site_id=0` is the admin panel. Since `uint64_t`, all values `>= 0`. Site checks applicable for ALL domains.
- [x] Verify `DomainViewService::write_enriched()` output at `libs/domain/DomainViewService.cpp:37-47` — **Confirmed:** JSON keys: `id`, `domain`, `type`, `site_id`, `site_name`, `site_domain`, `target`, `ssl_enabled`, `ssl_status`, `enabled`. No `dns`/`mail` fields.
- [x] Verify `loadDomains()` in `web/app.js:842-889` — **Confirmed:** DNS column at line 872 hardcoded `"Unknown"`. HTTP at line 873 hardcoded `"Unknown"`.
- [x] Verify `navigate()` dispatch at `web/app.js:261-286` — **Confirmed:** all page handlers. Must add `domain-detail`.
- [x] Verify `buildTable()` helper at `web/app.js:345-351` — **Confirmed:** signature `buildTable(columns, rows, emptyMsg)`.
- [x] Verify `MailDomain::dkim_public_key_dns` field at `libs/mail/MailDomain.h:41` — **Confirmed.**
- [x] Verify `MailDomainManager::find_by_domain()` at `libs/mail/MailDomainManager.h:40` — **Confirmed.**
- [x] Verify `DkimManager::generate_key()` signature at `libs/mail/DkimManager.h:29-34` — **Confirmed:** returns `std::string` (TXT value).
- [x] Verify `CertificateStore::load_metadata(site_id)` at `libs/ssl/CertificateStore.h:83` — **Confirmed:** returns `MetadataLoadResult`.
- [x] Verify runtime API response at `libs/api/ApiServer.cpp:453-488` — **Confirmed:** returns `{"web","php","db","cache","https"}`. **Note:** `site_id=0` returns 400 ("Invalid site ID") — runtime API does NOT support admin panel queries. **Consequence:** Runtime check is `not_applicable` for `site_id=0`. Column must be named "Runtime" not "HTTP".
- [x] Verify `MailDomain` JSON format in `mail_domain_json` lambda at `libs/api/ApiServer.cpp:1479-1498` — **Confirmed:** all specified keys present.
- [x] Verify test framework at `tests/main.cpp:1-2` — **Confirmed:** doctest single-header.
- [x] Verify API addition procedure at `docs/development/api-rules.md:49-77` — **Confirmed:** 8-step process.
- [x] Verify `Config::data_root()` exists — **Confirmed:** used at `ServiceRegistry.cpp:18` (`config_.data_root()`).
- [x] Verify `router_.add_prefix()` usage pattern at `libs/api/ApiServer.cpp:453,1590,1630` — **Confirmed:** prefix routing by path remainder parsing.
- [x] **c-ares availability:** `libc-ares-dev` installed (v1.34.5). `pkg-config --libs libcares` returns `-lcares`. Headers at `/usr/include/ares.h`. **Note:** Debian package name is `libc-ares-dev` (with hyphen), but pkg-config module name is `libcares`.
- [x] **CMake approach confirmed:** Flat source list in `CONTAINERCP_SOURCES`. Add `libs/dns/DnsCheckService.cpp` to sources, add `cares` to `target_link_libraries()` for both `containercpd` and `containercp_tests`. No subdirectory CMakeLists.txt needed.
- [x] **ServiceRegistry pattern confirmed:** Add `dns::DnsCheckService dns_check_;` member, add `#include`, add accessor `dns_check()`. Initialise in constructor.

---

## Phase 1 — DNS Check Backend Service

### 1.1 — Create `libs/dns/` directory and `DnsCheckService` ✅

**Files:** NEW: `libs/dns/DnsCheckService.h`, `libs/dns/DnsCheckService.cpp`  
**Dependency:** `libc-ares-dev` (installed, v1.34.5). Link: `-lcares` (pkg-config: `libcares`).  
**Build approach:** Project uses flat source list in `CONTAINERCP_SOURCES`. Add `.cpp` to list, add `cares` to `target_link_libraries()` for both `containercpd` and `containercp_tests`.  
**Depends on:** Phase 0  
**Criteria:** ✅ Service compiles, c-ares queries work, structured output returned.

- [x] Add `cares` to `target_link_libraries(...)` in both `CMakeLists.txt` files.
- [x] Create `libs/dns/DnsCheckService.h` with:
  - `struct DnsRecord` — fields: `type` (string), `name` (string), `value` (string), `ttl` (int), `priority` (int, default 0), `dns_response_details` (string — structured c-ares result for evidence panel)
  - `struct DnsCheckResult` — fields: `domain` (string), `resolved_at` (string, ISO 8601), `records` (vector<DnsRecord>), `soa` (struct with `mname`, `rname`, `serial`), `status_code` (string: `NOERROR`, `NXDOMAIN`, `NODATA`, `SERVFAIL`, `TIMEOUT`), `success` (bool), `error` (string)
  - `DnsCheckResult check(const std::string& domain, const std::vector<std::string>& record_types)` — main method
  - Cache methods: `set_cache_ttl(int seconds)`, `clear_cache(const std::string& domain)`, `bool has_cached(const std::string& domain)` — in-memory cache with TTL
- [x] Implement `DnsCheckService::check()` in `DnsCheckService.cpp` using **c-ares** (v1.34+ `ares_query_dnsrec` API):
  - Validate domain: lowercase, alphanumeric, dots, hyphens only. Reject IPs, spaces, shell chars. Max length 253 chars.
  - Validate record_types against allowlist: `A`, `AAAA`, `MX`, `TXT`, `CNAME`, `NS`, `SOA`, `CAA`, `PTR`. Reject unknown types.
  - Initialize c-ares channel: `ares_init()` with timeout 5000ms, tries 2.
  - For each requested type, call the appropriate c-ares query function:
    - `ares_query(domain, ns_c_in, ns_t_a)` for A
    - `ares_query(domain, ns_c_in, ns_t_aaaa)` for AAAA
    - `ares_query(domain, ns_c_in, ns_t_mx)` for MX
    - `ares_query(domain, ns_c_in, ns_t_txt)` for TXT
    - `ares_query(domain, ns_c_in, ns_t_cname)` for CNAME
    - `ares_query(domain, ns_c_in, ns_t_ns)` for NS
    - `ares_query(domain, ns_c_in, ns_t_soa)` for SOA
    - `ares_query(domain, ns_c_in, ns_t_caa)` for CAA
  - Process each response via `ares_callback`:
    - Extract structured data from `ares_host_callback` or `ares_dns_record`:
      - A: `struct in_addr` → string via `inet_ntop`
      - AAAA: `struct in6_addr` → string via `inet_ntop`
      - MX: `priority` + `exchange` (hostname)
      - TXT: join all fragments into single string (for long DKIM records)
      - CNAME: `cname` (hostname)
      - NS: `nsname` (hostname)
      - SOA: `nsname`, `hostmaster`, `serial`, `refresh`, `retry`, `expire`, `minimum`
      - CAA: `critical` flag, `tag` property, `value`
    - Store structured string representation in `dns_response_details` for evidence panel.
    - Preserve raw DNS response bytes (hex-encoded or formatted) for expert review.
  - Handle all response statuses:
    - `ARES_SUCCESS` → data available
    - `ARES_ENODATA` → NODATA (domain exists, but no records of requested type)
    - `ARES_ENOTFOUND` → NXDOMAIN (domain does not exist)
    - `ARES_ESERVFAIL` → SERVFAIL (server failure)
    - `ARES_ETIMEOUT` → TIMEOUT
    - Map each to `DnsCheckResult::status_code` (string) and `DnsCheckResult::error`.
  - **No shell invocation, no popen(), no dig binary required.**
- [x] Implement caching: `std::map<std::string, CacheEntry>`, 60s TTL, keyed by `domain:type1,type2,...`.
- [x] `clear_cache(domain)` removes all entries with matching domain prefix.
- [x] Namespace `containercp::dns`, include guard `CONTAINERCP_DNS_DNS_CHECK_SERVICE_H`.
- [x] Updated root `CMakeLists.txt` — added source file and `cares` link lib.
- [x] Build dependency `libc-ares-dev` installed (will document in Phase 10).
- [x] Created `tests/test_dns_service.cpp` — 9 test cases, all pass.

**Tests for 1.1:**
- Domain validation: valid (`example.com`), invalid (`rm -rf /`, ``, `192.168.1.1`)
- Type allowlist: valid (`A`, `MX`), invalid (`SPF`, `DMARC`, `INVALID`)
- A record parsing from c-ares response: single IPv4
- AAAA record parsing: IPv6
- MX parsing: priority extracted correctly
- TXT record parsing: single fragment
- TXT fragment joining: multi-fragment (long DKIM records)
- CNAME parsing: canonical name extraction
- SOA parsing: all fields extracted
- CAA parsing: flag + tag + value
- NXDOMAIN handling → `status_code = "NXDOMAIN"`
- NODATA handling → `status_code = "NODATA"`, empty records
- SERVFAIL handling → structured error
- TIMEOUT handling → structured error
- Cache hit → returns same data within TTL
- Cache bypass → `clear_cache` forces fresh lookup
- Cache TTL expiry → data older than TTL is re-fetched
- `dns_response_details` contains structured representation
- No shell commands executed (verify with strace or mock)

---

## Phase 2 — DNS Check REST API ✅

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
- [ ] Success response format:
  ```json
  {
    "success": true,
    "data": {
      "domain": "example.com",
      "resolved_at": "2026-07-15T12:00:00Z",
      "cached": false,
      "status_code": "NOERROR",
      "records": [
        {"type": "A", "name": "example.com", "value": "192.168.1.1", "ttl": 3600, "dns_response_details": "A 192.168.1.1 (ttl=3600)"},
        {"type": "MX", "name": "example.com", "value": "mail.example.com", "ttl": 3600, "priority": 10, "dns_response_details": "MX 10 mail.example.com (ttl=3600)"}
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

### 2.2 — Update `DomainViewService` to include mail info ✅

**Files:** `libs/domain/DomainViewService.h`, `libs/domain/DomainViewService.cpp`, `tests/test_managers.cpp`  
**Depends on:** Phase 0  
**Criteria:** ✅ `GET /api/domains` response includes mail fields.

- [x] Added `#include "mail/MailDomainManager.h"` to `DomainViewService.h`.
- [x] Added `mail::MailDomainManager& mail_domains_` member (passed through constructor).
- [x] Updated constructor signature: added `mail::MailDomainManager& mail_domains` parameter.
- [x] In `write_enriched()`: looks up `mail_domains_.find_by_domain(d.fqdn)`. If found, appends `mail_domain_id`, `mail_domain_mode`, `dkim_generated`, `dkim_selector`, `dkim_public_key_dns`. Empty/null values when no MailDomain.
- [x] Updated `ServiceRegistry.cpp` constructor: passes `mail_` to `DomainViewService`.
- [x] Updated `tests/test_managers.cpp`: creates `MailDomainManager` and passes to `DomainViewService`.

---

## Phase 3 — Domain List GUI (Enhanced) ✅

### 3.1 — Add `domain-detail` navigation route ✅

**Files:** `web/app.js`  
**Depends on:** Phase 0  
**Criteria:** ✅ Clicking a domain name navigates to `/domains/<id>`.

- [x] Added `domain-detail` case in `navigate()` dispatch.
- [x] Domain column link uses `navigate('domain-detail', r.id)`.
- [x] Sidebar highlights `domains` nav link for domain-detail page.
- [x] Stub `loadDomainDetail(p, domainId)` added for Phase 4.

### 3.2 — Replace DNS column with live fetch ✅

**Files:** `web/app.js`  
**Depends on:** 2.1  
**Criteria:** ✅ DNS column shows real status, progressively loaded.

- [x] DNS column shows `overall_status` badge (complete/partial/failed).
- [x] Progressive loading via `processBatch(rows, 3, fn)` — max 3 concurrent DNS requests.
- [x] Client-side cache in `window._dnsCache` (Map<domain, {data, timestamp, loading}>) with 60s TTL.
- [x] Loading state: `"..."` placeholder, updates in-place after fetch.
- [x] In-flight request deduplication: if a request is already in flight, subsequent calls wait for it.
- [x] Error handling: failed DNS check shows `"..."` gracefully.

### 3.3 — Add Health Score column ✅

**Files:** `web/app.js`  
**Depends on:** 3.2  
**Criteria:** ✅ Each domain row shows context-aware Health Score.

- [x] `computeDomainHealthScore(domainRow, dnsResult)` — context-aware calculation.
- [x] Applicable checks: A record (25), DKIM (10 if mail + dkim_generated), SSL (20 if site_id >= 0 and ssl_status Active/Expiring).
- [x] Normalised to 100. Grade boundaries: 90+ Excellent, 70+ Good, 40+ Fair, 1+ Poor, 0 Critical.
- [x] Health Score badge: `healthGradeBadge(score, grade)` with color coding.
- [x] Recalculated after DNS result arrives for that domain.
- [x] No DNS data → shows `"..."`.

### 3.4 — Add Mail and Runtime status columns ✅

**Files:** `web/app.js`  
**Depends on:** 2.2  
**Criteria:** ✅ Mail column shows Active/Not configured, Runtime column shows container status.

- [x] Old "HTTP" column replaced with "Runtime" — shows container status from `GET /api/runtime/<site_id>`.
- [x] Admin panel (`site_id=0`) shows "N/A" for Runtime (API rejects id=0).
- [x] Only `site_id > 0` rows fetch Runtime status.
- [x] Runtime cache in `window._rtCache` (Map<site_id, {data, timestamp}>) with 30s TTL.
- [x] Mail column uses `mail_domain_id` from enriched JSON (empty → "—", present → "Active").
- [x] Progressive loading: Runtime fetched in separate pass, concurrency=3.

---

## Phase 4 — Domain Detail GUI (Base Layout) ✅

### 4.1 — Create `loadDomainDetail()` with tab navigation ✅

**Files:** `web/app.js`  
**Depends on:** 3.1  
**Criteria:** ✅ Clicking a domain opens a detail page with 5 tabs.

- [x] Implemented `loadDomainDetail(p, domainId)` — fetches domain from enriched list, mail domain, SSL, runtime.
- [x] Uses existing `.tabs` / `.tab` CSS classes.
- [x] Tab IDs: `overview`, `dns-records`, `mail`, `security`, `health`.
- [x] Tab switching via `switchDomainTab(tabId)` — lazy loading.
- [x] Breadcrumb: `← Domains / example.com`.
- [x] Header: Health Score badge, Open, Copy, Remove buttons.
- [x] Loading state: `<div class="empty-state">Loading...</div>`.
- [x] Error state: `<div class="empty-state">Failed to load domain</div>`.
- [x] Data stored in `window._domainDetailData` for tab access.

### 4.2 — Implement Overview tab ✅

**Files:** `web/app.js`  
**Depends on:** 4.1, 2.1  
**Criteria:** ✅ Overview shows DNS check summary, Mail, SSL, Site info.

- [x] Fetches `GET /api/domains/<domain>/dns-check?types=A,AAAA,MX,TXT` via `DnsCache`.
- [x] Shows DNS summary table: Type, Configured, Published, Status.
- [x] Per-type status: A, AAAA, MX, SPF (if MailDomain), DKIM (if generated), DMARC (if MailDomain).
- [x] Mail card: domain, mode, DKIM status. If no MailDomain: "Not configured".
- [x] SSL card: status, HTTPS, redirect, expiry.
- [x] Site card: name, domain, runtime status.
- [x] `[Check Again]` button → clears cache and re-fetches.
- [x] Loading state while DNS check runs.

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
  | DMARC | **Recommended** value from DMARC Wizard (not stored in ContainerCP). Label column as "Recommended" not "Configured" |
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
- [ ] For DMARC row: show the Recommended value from DMARC Wizard (if a policy was selected in the current session). Label column as "Recommended". If no selection was made, show "Select a policy in Security tab". **Do not use localStorage.**
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

- [ ] DMARC current status: show **Recommended** (from Wizard) vs **Published** (from DNS) with status badge. Label clearly: "Recommended" not "Configured".
- [ ] Three policy cards:
  - Monitor: `v=DMARC1; p=none;`
  - Quarantine: `v=DMARC1; p=quarantine; rua=mailto:dmarc@<domain>`
  - Reject: `v=DMARC1; p=reject; rua=mailto:dmarc@<domain>`
- [ ] On card click (or radio select):
  - Show preview of full DNS record.
  - Show `[Copy Record]` and `[Copy with RUA]` buttons.
  - Show Published vs Recommended comparison (if Published DMARC exists).
  - **No localStorage.** DMARC policy is not persisted. Wizard is ephemeral — generates recommendations on the fly. Comparison is visual only, for the current session.
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

- [ ] Implement `async function showEvidence(recordType, configured, published, dnsDetails, domain)`:
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
        <strong>DNS Response Details</strong>
        <pre>escaped_c_ares_structured_output</pre>
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
  | `DMARC_POLICY_MISMATCH` | The DMARC policy field (p=) differs. Recommended: `<value>`. Published: `<value>`. Update your DMARC TXT record at `_dmarc.<domain>`. |
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
    // Site-dependent: ALL domains have a valid site_id (uint64_t, always >= 0).
    // site_id = 0 is the admin panel and IS applicable for site checks.
    // There is no "negative id" sentinel for unlinked domains in the current model.
    {
      checks.push({id:'ssl', label:'SSL Certificate', weight:20, class:'req'});
      // Runtime check: only for site_id > 0. Runtime API rejects site_id=0 (admin panel).
      // Admin panel gets no Runtime check — does not reduce Health Score.
      if (domain.site_id > 0) {
        checks.push({id:'runtime', label:'Runtime Status', weight:15, class:'req'});
      }
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
- [ ] **Test: admin panel site_id=0 → SSL + DNS checks active, Runtime = N/A (no penalty)**
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
  - [ ] DNS Response Details — DnsRecord::dns_response_details contains structured c-ares output
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
- [ ] Domain list shows Runtime column (container status, not external HTTP)
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
  - Implementation: uses c-ares library via `DnsCheckService` (no shell commands)
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
- [ ] Domain list shows Runtime status (container status, admin panel shows N/A)
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
  - `site_id >= 0` → SSL checks active (including admin panel). Runtime check only for `site_id > 0` (admin panel shows N/A).
- [ ] Security tab shows:
  - DMARC Wizard with 3 policies (Monitor/Quarantine/Reject)
  - Preview of generated TXT record (Recommended, not Configured — no backend storage)
  - CAA, MTA-STS, TLS-RPT, Autodiscover recommendations
- [ ] Evidence/Why works for all non-Match records:
  - Expected (Configured)
  - Actual (Published)
  - Reason (human-readable)
  - DNS Response Details (structured c-ares output, HTML-escaped)
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
| *(flat build — no sub-CMakeLists needed)* | 1.1 | Add `libs/dns/DnsCheckService.cpp` to root `CMakeLists.txt` `CONTAINERCP_SOURCES`, add `cares` to `target_link_libraries` |
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
| 1 | **c-ares not installed** on target system. | Add `libcares-dev` to system dependencies in `scripts/install.sh`, `packaging/`, and `README.md`. Detect at build time via `pkg-config`. Fail at build with clear message if missing. |
| 2 | **c-ares API complexity** — asynchronous event loop adds complexity compared to synchronous `popen(dig)`. | Use `ares_query()` with a blocking wrapper (synchronous event loop via `ares_process()` or `ares_process_fd()`). Keep the public API synchronous. The complexity is encapsulated within `DnsCheckService.cpp`. |
| 3 | **CAA record type support** — c-ares may not natively support `ns_t_caa` on older versions. | Conditionally compile CAA support. If unavailable, skip CAA queries and note in documentation. Minimum c-ares version: 1.27.0 (supports all planned types). |
| 4 | **SPF expected value** — ContainerCP has no SPF configuration. The template `v=spf1 mx ~all` is a reasonable default but may not match the admin's actual mail setup. | Show SPF template as a "recommended" value, not a "configured" value. Mark status as advisory. Clearly label "Recommended SPF record" vs "Your current DNS value". |
| 5 | **DNS Response Details size** — large TXT records (DKIM 2048-bit keys) could produce long detail strings. | Truncate details display to first 500 chars, with "Show full" toggle. API returns full value but frontend truncates for display. |
| 6 | **No HTTP check API** — ContainerCP has no endpoint to check external HTTP reachability. Runtime API rejects `site_id=0` (admin panel), returning 400. | For v1, the column is named **"Runtime"** not "HTTP". It shows container status from `GET /api/runtime/<site_id>`. Admin panel (`site_id=0`) shows "N/A" — no error shown, no Health Score penalty. A true HTTP check endpoint can be added in a future iteration. |
| 7 | **`loadDomainDetail` depends on `GET /api/domains` list** — currently there's no `GET /api/domains/<id>` endpoint. | Option A: Add `GET /api/domains/<id>` (new endpoint, follows existing pattern from SSL and Mail). Option B: Fetch all domains and filter by ID (works for small datasets, doesn't scale). Plan assumes Option A for correctness. |
| 8 | **Evidence reason codes are frontend-only** — no backend standardisation. | Acceptable for v1. Frontend generates reasons from record type, configured value, published value, and context. Reason codes (e.g., `DKIM_NOT_PUBLISHED`) provide a stable key for future i18n. |
| 9 | **DMARC has no backend storage** — Wizard recommendations are ephemeral, not persisted. | Acceptable for v1. DMARC is "Recommended vs Published" not "Configured vs Published". Revisit when ContainerCP adds SQLite storage. |
| 10 | **`site_id` is `uint64_t`** — impossible to have negative values for "unlinked" sentinel. | All domains have a valid `site_id >= 0`. Site-dependent checks (SSL, HTTP) are applicable for ALL domains. No "unlinked" sentinel needed. |
