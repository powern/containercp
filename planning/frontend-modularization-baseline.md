# Frontend Modularization Phase 0 Baseline

Purpose: implementation baseline and regression reference before extracting `web/app.js`.

Architecture source of truth: `planning/frontend-modularization-architecture.md`.

Master tracker: `planning/frontend-modularization-checklist.md`.

Scope: Phase 0 only. This document records current behavior and risks. It does not authorize production frontend restructuring, routing changes, module extraction, CSS redesign, backend changes, API contract changes, or build-system changes.

## 1. Baseline Summary

| Item | Baseline |
|------|----------|
| Main application file | `web/app.js` |
| Current `web/app.js` line count | 4,396 |
| Current `web/app.js` size | 240,383 bytes |
| Current frontend model | Zero-build vanilla JavaScript SPA |
| Module system | Classic ordered scripts, not ES modules |
| Bundler/transpiler | None |
| Frontend package manager | None found |
| CSS model | Single global file, `web/style.css` |
| API base used by Web UI | `/ui-api` |
| Session token storage | `localStorage` key `session_token` |
| Current route model | In-memory route key passed to `navigate(page, params)` |
| Browser history/deep links | Not used |
| Page cleanup on navigation | Not centralized |

## 2. Frontend File Sizes

| File | Lines | Bytes | Baseline role |
|------|-------|-------|---------------|
| `web/index.html` | 15 | 393 | Static shell and ordered script tags |
| `web/style.css` | 198 | 13,697 | Global CSS, including DB-2 styles |
| `web/js/cache.js` | 293 | 10,091 | Global DNS/runtime/health cache objects |
| `web/js/utils.js` | 524 | 24,440 | Global DNS/status/health helper functions |
| `web/app.js` | 4,396 | 240,383 | Bootstrap, auth, router, helpers, every page, page state |

## 3. Script Loading Model

Actual script order from `web/index.html`:

```html
<script src="/js/cache.js"></script>
<script src="/js/utils.js"></script>
<script src="/app.js"></script>
```

Baseline contract:

- `/js/cache.js` loads first and creates `window.DnsCache`, `window.RuntimeCache`, and `window.HealthCache`.
- `/js/utils.js` loads second and creates many `window.*` DNS/status/health helpers.
- `/app.js` loads last and defines `api`, `esc`, `copyText`, route functions, page functions, and action handlers that earlier scripts call at runtime.
- Scripts are classic scripts. Inline HTML event attributes expect many top-level functions to be globally reachable.
- Phase 0 must not change this script order.

## 4. Route And Menu Regression Matrix

Manual smoke status for every entry: `Not Testable` in this environment because no browser session is available. Static inspection only.

| Entry | Navigation key | Page title or visible heading | Render entry point | Primary API calls | Primary visible elements | Empty state | Error state | Timers/polling | Modal/drawer | Cleanup requirement | Smoke status |
|-------|----------------|-------------------------------|--------------------|-------------------|--------------------------|-------------|-------------|----------------|--------------|---------------------|--------------|
| Dashboard | `dashboard` | `Dashboard` | `loadDashboard(p)` | `/api/health`, `/api/sites`, `/api/jobs`, deferred `/api/domains`, `/api/backups`, `/api/ssl`, `/api/mail/health` | Summary cards, health grid, recent jobs | `No recent jobs` | `Failed to load dashboard` | Deferred async updates; global status interval | None | Ignore stale async dashboard updates | Not Testable |
| Sites | `sites` | `Sites`, `All Sites` | `loadSites(p)` | `/api/sites`, `/api/runtime/<site_id>`, site create/remove/job endpoints | Create Site button, sites table | `No sites` | `Failed to load sites` | Site creation job interval; success redirect timeout | Create Site modal, progress overlay, native confirm | Clear job poller/overlay; reset table renderer | Not Testable |
| Site Detail | `site-detail` | Site domain | `loadSiteDetail(p, siteId)` | `/api/sites`, `/api/domains`, `/api/databases`, `/api/ssl`, `/api/proxy`, `/api/backups`, runtime, WordPress credential, PHP mail APIs | Details panel, runtime card, resource cards, WordPress credentials, PHP mail | `Site not found` | `Failed to load site`; child card errors | Runtime refresh timeout; rotation poller | Native confirms | Cancel pollers and delayed card refresh | Not Testable |
| Domains | `domains` | `Domains`, `All Domains` | `loadDomains(p)` | `/api/domains`, DNS checks, runtime status, health cache dependencies | Domains table with DNS/mail/runtime/SSL/health columns | `No domains` | `Failed to load domains` | Progressive async batch loading | Remove confirm | Ignore stale row updates; reset table renderer | Not Testable |
| Domain Detail | `domain-detail` | Domain name with tabs | `loadDomainDetail(p, domainId)` plus tab loaders | Domains, mail domains, settings, DNS checks, runtime, SSL, proxy, mail APIs | Overview, DNS Records, Mail, Security, Health tabs | `Domain not found`; tab `No data` | `Failed to load domain`; tab errors | Health/DNS async cache loaders | Evidence panel; proxy/SSL/remove confirms | Close evidence panel; ignore stale tab loads | Not Testable |
| Databases | `databases` | `Databases` | `loadDatabases(p)` | `/api/databases`, `/api/databases/<id>`, WordPress credential status/rotate, `/api/jobs?id=` | DB-2 summary cards, filters, table/mobile cards, drawer | `No managed databases were found`; filtered empty text | DB load/detail error with retry | Rotation poller; drawer focus timeout | Detail drawer, rotation modal | Close drawer/modal; cancel rotation poller | Not Testable |
| SSL | `ssl` | `SSL Certificates`, `All Sites` | `loadSsl(p)` | `/api/ssl`, SSL issue/renew/enable/disable/redirect actions | SSL table and action buttons | Generic `No data` | `Failed to load SSL` | None | None | Reset table renderer; handle existing undefined `loadSite()` baseline | Not Testable |
| Mail | `mail` | `Mail`, `Module Status`, `Mail Domains` | `loadMail(p)` | `/api/mail/status`, `/api/mail/domains`, activation/deactivation/create/remove endpoints | Module status card, mail domains table | `No mail domains` | `Failed to load mail` | None | Add Mail Domain modal; native confirms | Hide modal; reset table renderer | Not Testable |
| Mail Domain | `mail-domain` | `Mail / {domain}` | `loadMailDomain(p, id)` | `/api/mail/domains/<id>`, mailboxes, aliases, DKIM/regenerate APIs | Detail panel, mailboxes, aliases, DKIM card | `Mail domain not found`; `No mailboxes`; `No aliases`; DKIM `Not generated` | `Failed to load mail domain` | None | Mailbox/Alias modals; native confirms | Hide modal; remove global DKIM state | Not Testable |
| Mail Health | `mail-health` | `Mail Health` | `loadMailHealth(p)` | `/api/mail/health` | Status card, service rows, Back to Mail button | None identified | `Failed to load mail health` | None | None | Stale async guard | Not Testable |
| Webmail | `webmail` | `Webmail`, `SnappyMail Webmail` | `loadWebmail(p)` | None | External Open Webmail button | None identified | None identified | None | None | None | Not Testable |
| Proxy | `proxy` | `Reverse Proxy`, `Proxy Entries` | `loadProxy(p)` | `/api/proxy`, `/api/proxy/health`, proxy test/reload/sync/recover/remove | Health card, global action buttons, proxy table | `No proxy entries` | `Failed to load proxy` | None | Remove confirm | Reset pending action/table renderer | Not Testable |
| Access | `access` | `Access Users`, `All Access Users` | `loadAccess(p)` | `/api/access-users`, remove access user | Access users table | Generic `No data` | `Failed to load access users` | None | Remove confirm | Reset table renderer | Not Testable |
| Backups | `backups` | `Backups`, `All Backups` | `loadBackups(p)` | `/api/backups`, `/api/sites`, backup create/restore/remove | Create Backup button, backups table | Generic `No data`; modal `No sites available` | `Failed to load backups`; modal site load error | None | Create Backup modal; restore/remove confirms | Hide modal; reset operation flags | Not Testable |
| Migration | `migration` | `Migration`, `Import from myVestaCP` | `loadMigration(p)` | Vesta backup list, inspect, create-site, import-files, import-sql, jobs | Backup/domain/owner form, stage cards | `No backups found` | `Failed to load migration page`; action alerts | SQL import interval poller | None | Clear SQL poller; prevent stale writes | Not Testable |
| Profiles | `profiles` | `Configuration Profiles`, `All Profiles` | `loadProfiles(p)` | `/api/profiles` | Profiles table | Generic `No data` | `Failed to load profiles` | None | None | Reset table renderer | Not Testable |
| Templates | `templates` | `Web Server Templates`, `Templates` | `loadTemplates(p)` | `/api/profiles` filtered to web-server templates | Templates table | `No templates` | `Failed to load templates` | None | None | Reset table renderer | Not Testable |
| Nodes | `nodes` | `Nodes`, `Node Details` | `loadNodes(p)` | `/api/nodes` | Nodes table | `No nodes` | `Failed to load nodes` | None | None | Reset table renderer | Not Testable |
| Logs | `logs` | `Logs`, `System Logs` | `loadLogs(p)` | `/api/logs` | Refresh button, logs table | `No logs` | `Failed to load logs` | None | None | Reset table renderer; stale refresh guard | Not Testable |
| Settings | `settings` | `Settings`, `Admin Panel HTTPS`, `Change Password` | `loadSettings(p)` | `/api/settings`, settings save, admin SSL issue/renew, password change | Hostname field, SSL buttons, password form | Defaults if settings load fails | Form messages; no full-page error | None | None | Password fields must remain private | Not Testable |
| Database Detail Behavior | Not a route | Database drawer | `openDatabaseDetail(id)` | `/api/databases/<id>`, WordPress credential status | Right drawer details/actions | Detail-specific error drawer | Detail error with retry | Rotation poller when action starts | Drawer and rotation modal | Close drawer and cancel poller on navigation | Not Testable |

Documented navigation entries: 21.

## 5. Page Render Entry Points

| Area | Entry point |
|------|-------------|
| Auth login | `renderLogin(error)` |
| Forced password change | `renderChangePassword(error, success)` |
| App shell | `initApp()` |
| Auth bootstrap | `checkAuth()` |
| Router | `navigate(page, params)` |
| Dashboard | `loadDashboard(p)` |
| Sites | `loadSites(p)` |
| Site Detail | `loadSiteDetail(p, siteId)` |
| Domains | `loadDomains(p)` |
| Domain Detail | `loadDomainDetail(p, domainId)` |
| Domain tabs | `switchDomainTab(tabId)` and tab loaders |
| Webmail | `loadWebmail(p)` |
| Mail | `loadMail(p)` |
| Mail Domain | `loadMailDomain(p, id)` |
| Mail Health | `loadMailHealth(p)` |
| Databases | `loadDatabases(p)` |
| Database detail drawer | `openDatabaseDetail(id)` |
| SSL | `loadSsl(p)` |
| Proxy | `loadProxy(p)` |
| Access | `loadAccess(p)` |
| Backups | `loadBackups(p)` |
| Migration | `loadMigration(p)` |
| Profiles | `loadProfiles(p)` |
| Templates | `loadTemplates(p)` |
| Nodes | `loadNodes(p)` |
| Logs | `loadLogs(p)` |
| Settings | `loadSettings(p)` |

## 6. Public Global Compatibility Inventory

This is the Phase 0 compatibility baseline. Do not remove, rename, or change these globals in Phase 0.

| Global name(s) | Definition location | Representative callers | Security sensitivity | Future owner | Shim required | Removal target |
|----------------|---------------------|------------------------|----------------------|--------------|---------------|----------------|
| `API_BASE` | `web/app.js` | `api()` | Medium | `core/api.js` | No public shim | Phase 3/15 |
| `sessionToken`, `currentUser` | `web/app.js` | Auth/API flow | Critical | `core/auth.js` | No; keep private | Phase 3 |
| `currentPage` | `web/app.js` | `navigate()` | Medium | `core/router.js` | Maybe | Phase 2/15 |
| `searchTerm`, `window.renderTable` | `web/app.js` dynamic assignments | Toolbar `oninput`, global input listener | Medium | Page-local table/search state | Yes | Phase 15 |
| `navigateTo`, `navigate` | `web/app.js` | Nav links, detail links, mail/SSL/proxy links | Medium | `core/router.js` | Yes | Phase 15 |
| `api`, `apiPost` | `web/app.js` | All page loaders/actions; `cache.js` calls `api` | Critical | `core/api.js` | Yes until imports | Phase 15 |
| `toast` | `web/app.js` | Most mutating actions, copy feedback | Medium | `core/notifications.js` | Yes until imports | Phase 15 |
| `esc`, `escAttr`, `dbJsArg` | `web/app.js` | HTML, attribute, JS-string rendering | High | `core/utils.js` | Yes until imports | Phase 15 |
| `$`, `qs`, `qsa` | `web/app.js` | Inline `loadLogs($('page'))`, internal DOM access | Medium | `core/dom.js` or local DOM APIs | Yes for `$` | Phase 15 |
| `showModal`, `hideModal` | `web/app.js` | Modal action functions, inline close buttons | Medium | `core/modals.js` | Yes | Phase 15 |
| `copyText` | `web/app.js` | DNS, domain, DB, DKIM copy controls; `utils.js` | High | `core/clipboard.js` | Yes | Phase 15 |
| `doLogin`, `doChangePassword`, `doLogout` | `web/app.js` | Auth buttons | Critical | `core/auth.js` | Yes | Phase 15 |
| `showCreateSiteWizard`, `startSiteWizard`, `removeSite` | `web/app.js` | Sites buttons/table/actions | Critical | Sites module | Yes | Phase 12/15 |
| `loadWordPressCredentialCard`, `rotateWordPressDatabasePassword` | `web/app.js` | Site Detail WordPress credential card | Critical | Sites credential submodule and jobs | Yes | Phase 12/15 |
| `pollRotationJob`, `renderWordPressRotationDiagnostics`, `renderRotationJobTimeline` | `web/app.js` | Site and DB rotation flows | High | `core/jobs.js`, `components/job-timeline.js` | Imported, not public | Phase 5/12/15 |
| `loadPhpMailCard`, `enablePhpMail`, `disablePhpMail` | `web/app.js` | Site Detail PHP mail card | High | Sites PHP mail submodule | Yes | Phase 12/15 |
| `runRuntimeAction` | `web/app.js` | Runtime card buttons | High | Runtime/Sites submodule | Yes | Phase 12/15 |
| `switchDomainTab`, `refreshDomainOverview`, `refreshDnsRecordsTab`, `refreshMailTab` | `web/app.js` | Domain Detail tabs/buttons | Medium | Domains module/subtabs | Yes | Phase 14/15 |
| `fetchDnsForFqdn` | `web/app.js` | Domain tabs and `HealthCache` | Medium | DNS/cache module | Yes until imports | Phase 14/15 |
| `runSystemAction`, `loadSslDetails` | `web/app.js` | Domain proxy/SSL system actions | High | Domains/SSL/Proxy modules | Yes if inline callers remain | Phase 14/15 |
| `selectDmarcPolicy`, `_dmarcSelection`, `_openEvidencePanel` | `web/app.js` | Domain Security tab | Medium | Domain security tab state | Maybe | Phase 14/15 |
| `buildFullDnsRecord`, `normalizeHostname`, `attachDataCopyListener` | `web/app.js`, duplicated in `web/js/utils.js` | DNS records and mail tab copy helpers | Medium | DNS/clipboard utilities | Yes until imports | Phase 14/15 |
| `removeDomain` | `web/app.js` | Domain header/table actions | Critical | Domains module | Yes | Phase 14/15 |
| `activateMail`, `deactivateMail`, `loadMailHealth` | `web/app.js` | Mail page buttons | High | Mail module | Yes | Phase 11/15 |
| `showCreateMailDomain`, `showCreateMailDomainSimple`, `onMailDomainSelectChange`, `createMailDomain`, `removeMailDomain` | `web/app.js` | Mail domain modal/table | High | Mail module | Yes | Phase 11/15 |
| `generateMailDkim`, `regenMailConfig` | `web/app.js` | Mail Domain DKIM/config buttons | High | Mail module | Yes | Phase 11/15 |
| `showCreateMailbox`, `createMailbox`, `removeMailbox`, `showCreateAlias`, `createAlias`, `removeAlias` | `web/app.js` | Mail Domain mailbox/alias flows | Critical | Mail module | Yes | Phase 11/15 |
| `renderDatabaseInventory`, `toggleDatabaseSortDirection`, `resetDatabaseFilters`, `refreshDatabases` | `web/app.js` | DB filter/search/sort/retry controls | Medium | Databases module | Yes | Phase 5/15 |
| `dbDashboardState`, `computeDatabaseHealthState` | `web/app.js` | DB-2 inventory/drawer state | Medium | Databases module | Maybe for tests/debug | Phase 5/15 |
| `openDatabaseDetail`, `closeDatabaseDrawer` | `web/app.js` | DB table/card/drawer controls | High | Databases drawer module | Yes | Phase 5/15 |
| `showDatabaseRotationConfirm`, `confirmDatabasePasswordRotation` | `web/app.js` | DB rotation modal | Critical | Databases/jobs modules | Yes | Phase 5/15 |
| `renderDatabaseRotationJob`, `renderDatabaseRotationSuccess`, `renderDatabaseRotationFailure` | `web/app.js` | DB rotation poller callbacks | Medium | Databases/jobs modules | Imported, not public | Phase 5/15 |
| `issueSsl`, `renewSsl`, `toggleSsl`, `toggleRedirect` | `web/app.js` | SSL action buttons | High | SSL module | Yes | Phase 7/15 |
| `loadSite` | Not defined | SSL table domain link | High, broken baseline | Sites/router decision | No; fix rather than shim | Phase 7 |
| `proxyAction`, `removeProxy` | `web/app.js` | Proxy action buttons/table | Critical | Proxy module | Yes | Phase 8/15 |
| `removeAccessUser` | `web/app.js` | Access table action | Critical | Access module | Yes | Phase 6/15 |
| `showBackupModal`, `createBackup`, `restoreBackup`, `removeBackup` | `web/app.js` | Backup actions | Critical | Backups module | Yes | Phase 9/15 |
| `analyzeBackup`, `importMigrationSite`, `importMigrationFiles`, `importMigrationSql` | `web/app.js` | Migration staged buttons | Critical | Migration module | Yes | Phase 13/15 |
| `loadLogs` | `web/app.js` | Logs refresh button | Medium | Logs module | Yes | Phase 6/15 |
| `saveHostname`, `issueAdminSsl`, `renewAdminSsl`, `changeAdminPassword` | `web/app.js` | Settings actions | Critical | Settings/auth/SSL module | Yes | Phase 10/15 |
| `DnsCache`, `RuntimeCache`, `HealthCache` | `web/js/cache.js` | Domains, Domain Detail, health logic | Medium/High | Cache modules | Yes until imports | Phase 14/15 |
| `window._domainDetailData` | `web/app.js`, read/write in `cache.js` | Domain Detail and HealthCache | Medium | Domain detail state | Yes until state injection | Phase 14/15 |
| `window._mailDomains`, `window._dkimData` | `web/app.js` | Mail modal/DKIM copy | Medium | Mail module state | Yes until delegation | Phase 11/15 |
| `processBatch` | `web/js/utils.js` | Domain progressive loading | Medium | Async utility | Yes until imports | Phase 14/15 |
| `dnsStatusBadge`, `runtimeStatusBadge`, `healthGradeBadge`, `healthGradeLabel` | `web/js/utils.js` | Domain list/detail | Medium | Badges/health modules | Yes until imports | Phase 14/15 |
| `getDnsRecs`, `fmtVal`, `statusBadge` | `web/js/utils.js` | Domain/Mail DNS renderers | Medium | DNS/render utilities | Yes until imports | Phase 14/15 |
| `normalizeDmarcValue`, `normalizeDnsValue`, `formatMxRecords`, `formatMxPublished` | `web/js/utils.js` | DNS comparisons | Medium | DNS/Mail utility modules | Yes until imports | Phase 14/15 |
| `copyRowButtons`, `getExpectedMxTarget`, `computeRecordStatus` | `web/js/utils.js` | DNS/Mail table renderers | Medium | DNS/Mail utility modules | Yes until imports | Phase 14/15 |
| `compareIpRecords`, `compareMxRecords`, `compareSpfRecords`, `compareDkimRecords`, `compareDmarcRecords` | `web/js/utils.js` | Domain DNS comparisons and health | Medium/High | DNS/Mail security utilities | Yes until imports | Phase 14/15 |
| `computeDomainHealthScore` | `web/js/utils.js` | Domain health and `HealthCache` | High product correctness | Domain health module or future backend owner | Yes until decision | Phase 14/15 |
| `currentParams` | Not defined | `removeMailbox`, `removeAlias` | High, broken baseline | Router/Mail detail state | No; fix rather than shim | Phase 11 |

Documented global names and compatibility assumptions: 129.

## 7. Timer, Poller, Listener, And Route Handler Inventory

### 7.1 Timers, Pollers, And Async Chains

| Owner | Start location | Type | Cleanup behavior | Duplicate risk | After-navigation risk | Planned cleanup owner |
|-------|----------------|------|------------------|----------------|-----------------------|-----------------------|
| Toast fade/remove | `toast()` | Nested `setTimeout` | No handle retained | Low | Low | Notifications owner |
| App status refresh | `initApp()` | `setInterval(updateStatus, 30000)` | Never cleared | High on re-init | High after logout/re-init | App lifecycle/status owner |
| Site creation success redirect | `startSiteWizard()` | `setTimeout(..., 1500)` | No handle retained | Low/Medium | Medium; can force `navigate('sites')` | Site wizard/jobs owner |
| Site creation no-job redirect | `startSiteWizard()` | `setTimeout(..., 1500)` | No handle retained | Low/Medium | Medium | Site wizard/jobs owner |
| Deployment job progress | `pollJobProgress()` | `setInterval(..., 500)` | Clears on terminal/catch only | Medium | High | `core/jobs.js` later |
| Rotation job progress | `pollRotationJob()` | Recursive `setTimeout(..., 2000)` | Stops terminal/max attempts only | Medium | Medium/High | `core/jobs.js` later |
| Runtime action refresh | `runRuntimeAction()` | `setTimeout(..., 1500)` | No handle retained | Low | Low/Medium | Runtime card owner |
| DNS cache wait loop | `DnsCache.waitFor()` | Repeated `setTimeout(100)` | Stops when loading flag clears | Medium | Medium | Cache owner with abort/timeout |
| DB drawer focus | `showDatabaseDrawer()` | `setTimeout(0)` | No handle retained | Low | Low | Drawer owner |
| DB rotation modal focus | `showDatabaseRotationConfirm()` | `setTimeout(0)` | No handle retained | Low | Low | Modal owner |
| Migration SQL polling | `importMigrationSql()` | `setInterval(..., 2000)` | Clears on terminal/catch only | Medium | High | `core/jobs.js` and Migration owner |
| Dashboard deferred updates | `loadDashboard()` | Promise chains | No route check | Low | Medium | Dashboard owner |
| Sites runtime row loading | `loadSites()` | Per-row async calls | No abort/cancel | Medium | Medium | Sites owner |
| Domains progressive loading | `loadDomains()` | Async batches | No abort/cancel | Medium | Medium/High | Domains owner |
| Health cache loader | `HealthCache.load()` | Deduplicated async load | Generation protected, no route cancel | Low/Medium | Medium | Health/cache owner |

### 7.2 Event Listeners And Handlers

| Owner | Start location | Handler type | Cleanup behavior | Duplicate risk | After-navigation risk | Planned cleanup owner |
|-------|----------------|--------------|------------------|----------------|-----------------------|-----------------------|
| Login Enter key | `renderLogin()` | Element `keydown` | Removed with login DOM | Low | Low | Auth owner |
| Sidebar nav links | `initApp()` | Element `click` | Removed with app DOM | Low | Low | Router/shell owner |
| Sidebar toggle | `initApp()` | Element `click` | Removed with app DOM | Low | Low | Shell owner |
| Theme toggle | `initApp()` | Element `click` | Removed with app DOM | Low | Low | Shell owner |
| App boot | Script load | `DOMContentLoaded` | Never removed, expected once | Low | Low | Bootstrap owner |
| Modal overlay dismiss | `showModal()` first create | Overlay `click` | Overlay persists | Low | Low/Medium | Modal owner |
| Site wizard backend select | `showCreateSiteWizard()` | Element `change` | Removed with modal DOM | Low | Low | Site wizard owner |
| Domain overview system buttons | `loadDomainOverview()` | Assigned `onclick` delegation | Replaced on assignment | Low | Medium | Domain overview owner |
| App-local data copy | `attachDataCopyListener()` in `app.js` | Container `click` | No explicit cleanup | Medium | Low/Medium | Clipboard owner |
| Evidence panel dismiss | `toggleEvidencePanel()` | Button `click` | Removed with panel | Low | Low | Domain security owner |
| Security tab delegation | `attachSecurityDelegation()` | Container `click` | Removed with container | Low/Medium | Medium | Domain security owner |
| Health tab delegation | `attachHealthDelegation()` | Container `click` | Removed with container | Low/Medium | Medium | Domain health owner |
| DKIM copy buttons | `loadMailDomain()` | Element `click` | Removed with DOM | Low | Low | Mail Domain owner |
| DB drawer backdrop | `showDatabaseDrawer()` first create | Backdrop `click` | Backdrop persists | Low | Medium | Drawer owner |
| DB drawer Escape key | Script load | Document `keydown` | Never removed | Low unless script duplicated | Low | Drawer/global keyboard owner |
| Global search input | Script load | Document `input` | Never removed | Low unless script duplicated | Medium | Table/search owner |
| Utils data copy | `window.attachDataCopyListener()` in `utils.js` | Container `click` | No explicit cleanup | Medium | Low/Medium | Clipboard owner |
| Inline route handlers | Rendered HTML | `onclick` | Removed with DOM | Low | Low/Medium | Router/page owners |
| Inline form/table handlers | Rendered HTML | `oninput`, `onchange`, `onkeydown` | Removed with DOM | Low | Medium due global state mutation | Form/table owners |
| Browser route handlers | None found | `hashchange`/`popstate` | Not applicable | Not applicable | No browser history integration | Router owner |

### 7.3 Route Change Handlers

| Owner | Location | Baseline |
|-------|----------|----------|
| `navigateTo(page)` | `web/app.js` | Alias to `navigate(page)` |
| `navigate(page, params)` | `web/app.js` | Central if/else dispatch; no cleanup hook |
| `switchDomainTab(tabId)` | `web/app.js` | Domain Detail tab swap; no universal async cancellation |
| Browser history | Not found | No `hashchange`, `popstate`, or History API integration |

Documented lifecycle entries: 39.

## 8. Modal And Drawer Ownership

| UI surface | Current owner | Current cleanup | Future owner |
|------------|---------------|-----------------|--------------|
| Generic modal overlay | `showModal`, `hideModal` in `web/app.js` | Overlay persists and is hidden; no route cleanup | `core/modals.js` |
| Site create wizard modal | Sites code in `web/app.js` | Hidden on success; no route cleanup | Sites/create-site dialog owner |
| Mail domain modal | Mail code in `web/app.js` | Hidden on success; no route cleanup | Mail module |
| Mailbox modal | Mail Domain code in `web/app.js` | Hidden on success; no route cleanup | Mail module |
| Alias modal | Mail Domain code in `web/app.js` | Hidden on success; no route cleanup | Mail module |
| Backup modal | Backups code in `web/app.js` | Hidden on success; no route cleanup | Backups module |
| DB detail drawer | Databases DB-2 code in `web/app.js` | Hidden by close button/backdrop/Escape; no route cleanup | `components/drawer.js` plus Databases module |
| DB rotation modal | Databases DB-2 code in `web/app.js` | Hidden on confirmation/cancel | Databases module plus confirmation dialog |
| Domain Security evidence panel | Domain Security code in `web/app.js` | Removed by close/toggle; no route cleanup hook | Domains security tab |

## 9. API Helper Ownership

| Helper | Current owner | Current behavior | Future owner | Duplication rule |
|--------|---------------|------------------|--------------|------------------|
| `API_BASE` | `web/app.js` | Fixed `/ui-api` | `core/api.js` | Do not duplicate |
| `api(path, opts)` | `web/app.js` | Adds `X-Session-Token`, parses JSON, throws enriched `Error` | `core/api.js` | Do not duplicate |
| `apiPost(path, body)` | `web/app.js` | Always sends `POST` JSON | `core/api.js` | Do not duplicate; known method argument issue tracked |
| `sessionToken` | `web/app.js` | Read from and written to `localStorage` | `core/auth.js` | Do not expose on `window` |

## 10. Security Boundary Baseline

| Boundary | Current function or pattern | Current owner | Baseline risk | Future owner |
|----------|-----------------------------|---------------|---------------|--------------|
| HTML escaping | `esc()` | `web/app.js` | Does not escape single quote; unsafe for JS-string contexts | `core/utils.js` |
| Attribute escaping | `escAttr()` | `web/app.js` Domain Security section | Localized; not consistently used | `core/utils.js` |
| JavaScript string serialization | `dbJsArg()` | `web/app.js` DB section | Best current JS-argument pattern, not universal | `core/utils.js` |
| Safe text rendering | `textContent` | Scattered | Safe pattern, not consistently used | DOM/render utilities |
| HTML rendering | `innerHTML` | Pervasive | Requires all interpolations escaped; some raw API errors exist | Component/render helpers |
| Server error rendering | `api()` errors plus page code | `web/app.js` | Some PHP mail errors inserted into `innerHTML` unescaped | `core/api.js` and error-state component |
| Job diagnostics rendering | `renderWordPressRotationDiagnostics`, `renderRotationJobTimeline` | `web/app.js` | Good public-safe baseline; no route cleanup | `core/jobs.js`, `components/job-timeline.js` |
| Password/secret redaction | Mostly by omission | Scattered | Passwords not rendered; raw API error logging remains risk | Auth/API/redacted logging owner |
| Console logging | Direct `console.error(..., e)` | `app.js`, `cache.js` | Error body can be logged because `api()` attaches `body` | Redacted logger |
| Storage | `localStorage` only | `web/app.js` | Persistent token vulnerable to XSS; no sessionStorage use | `core/auth.js`; later PO decision |
| Confirmation dialogs | Native `confirm()` and typed rotation checks | Page code | Inconsistent destructive-action UX | `components/confirmation-dialog.js` later |
| API response rendering | Escaped HTML, textContent, and some raw innerHTML | Page code | Mixed sinks; raw error paths need review | API/error/render components |

Functions that must not be duplicated during modularization:

- `api`
- `apiPost`
- `esc`
- `escAttr`
- `dbJsArg`
- `showModal`
- `hideModal`
- `toast`
- `copyText`
- `pollRotationJob`
- `renderWordPressRotationDiagnostics`
- `renderRotationJobTimeline`
- `DnsCache`
- `RuntimeCache`
- `HealthCache`
- `buildTable`
- `statusBadge`
- `fmtVal`
- `buildFullDnsRecord`
- `normalizeHostname`
- `attachDataCopyListener`
- `copyRowButtons`
- `getEvidenceSteps`
- `computeDomainHealthScore`

## 11. Behavioral Snapshot

Key DOM containers and selectors:

| Purpose | Selector or ID |
|---------|----------------|
| App root | `#app` |
| Page root after login | `#page` |
| Sidebar | `#sidebar` |
| Sidebar nav links | `.nav-link[data-page]` |
| User display | `#userDisplay` |
| Status indicator | `#statusDot`, `#statusLabel` |
| Theme toggle | `#themeToggle` |
| Modal overlay | `#modal-overlay` |
| Sites table | `#sites-table` |
| Domains table | `#domains-table` |
| Database inventory | `#db-inventory` |
| Database drawer | `#db-detail-drawer` |
| Database drawer backdrop | `#db-drawer-backdrop` |
| Database rotation message | `#db-rotation-msg` |
| Progress overlay | `#progress-overlay` |
| Migration SQL progress | `#migrate-sql-progress` |

Expected key headings:

- `Dashboard`
- `Sites`
- `Domains`
- `Databases`
- `SSL Certificates`
- `Mail`
- `Webmail`
- `Reverse Proxy`
- `Access Users`
- `Backups`
- `Migration`
- `Configuration Profiles`
- `Web Server Templates`
- `Nodes`
- `Logs`
- `Settings`

Important button labels and controls:

- `Create Site`
- `Open Webmail`
- `Add Domain`
- `Refresh`
- `Rotate Password`
- `Create Backup`
- `Analyze Backup`
- `Save Hostname`
- `Issue SSL`
- `Renew SSL`
- `Change Password`
- Database filters: search, runtime, connection, credentials, owner, sort, direction, reset

Expected behavior notes:

- Refresh starts at auth bootstrap and then Dashboard when authenticated.
- Detail routes are in-memory only; direct browser deep links are not supported.
- Database detail opens a right-side drawer; Escape closes the drawer.
- Database rotation entry point opens a typed confirmation modal; Phase 0 smoke must not perform live rotation.
- Native `confirm()` is used for many destructive or operational actions.
- Job progress is rendered for site creation, WordPress/database credential rotation, and migration SQL import.

## 12. Phase 0 Risk Register

| Risk | Current status | Target phase | Blocking | Acceptance condition |
|------|----------------|--------------|----------|----------------------|
| `apiPost(path, body)` ignores method argument | Known baseline; Mail delete callers pass `'DELETE'` but helper always POSTs | Phase 11 or dedicated bug fix before Mail extraction | Blocking for Mail extraction decision | Method behavior is fixed in a separate validated task or explicitly preserved/documented before Mail moves |
| Undefined `currentParams` in mailbox/alias delete refresh handlers | Known baseline | Phase 11 or dedicated bug fix before Mail extraction | Blocking for Mail Domain extraction decision | Mail detail route state is explicit and delete refresh works |
| Undefined SSL `loadSite(...)` link | Known baseline | Phase 7 or dedicated bug fix before SSL extraction | Blocking for SSL extraction decision | SSL domain link uses supported navigation or bug is separately tracked |
| Duplicate DNS helpers | Known baseline | Phase 14 after tests protect behavior | Non-blocking before Domains | Single owner selected; behavior preserved |
| Duplicate `getEvidenceSteps()` | Known baseline | Phase 14 | Non-blocking before Domains | Duplicate removed after security tab behavior is protected |
| `loadMail()` table mutation pattern | Known baseline | Phase 11 | Non-blocking before Mail | Mail table render state becomes page-local and deterministic |
| Existing `console.error` sensitive payload review | Known baseline | Security boundary work before high-risk extraction | Blocking for credential/migration diagnostic expansion | Redacted logging helper or direct logging audit complete |
| Inline dynamic event-handler generation | Known baseline | Phase 1 through Phase 15 | Blocking for final compatibility removal | Inline handlers removed or safe compatibility shims exist |
| Multiple pollers/timers without route cleanup | Known baseline | Phase 2 and page extraction phases | Blocking for final page extraction stability | Lifecycle/job cleanup prevents stale updates after navigation |
| Domain Health Score ownership decision | Open product-owner decision | Before or during Phase 14 | Blocking for final Domains architecture | Frontend owner remains explicit or backend owner/API migration is approved |
| Temporary `window.ContainerCP` namespace | Open product-owner decision, architecture recommends temporary namespace | Phase 1/2 | Blocking for module conversion if rejected | Compatibility namespace approved or alternative event-delegation-only strategy selected |
| `localStorage` versus later session storage review | Open product-owner decision | Later security task | Non-blocking for Phase 0 | Current behavior preserved; future review tracked |
| Native `confirm()` replacement timing | Open product-owner decision | Phase 4+ or legacy cleanup | Non-blocking for Phase 0 | Native confirm preserved until approved replacement and tests exist |
| One module per menu versus complex submodules | Open product-owner decision | Before Sites/Domains extraction | Non-blocking before Databases | Sites/Domains split strategy approved before extraction |

## 13. Baseline Validation Plan And Results

Frontend-only validation rule for this phase:

- Run `node --check` for every currently loaded JavaScript file: `web/js/cache.js`, `web/js/utils.js`, `web/app.js`.
- Run relevant frontend tests if available.
- Run browser/manual smoke checks where a browser environment is available.
- Run browser console error review where a browser environment is available.
- Run `git diff --check`.
- Do not run full CMake configure/build, `containercp` build, `containercpd` build, full CTest, or GitHub Actions.
- Do not create new `build-*` directories.

| Validation item | Result | Notes |
|-----------------|--------|-------|
| `node --check web/js/cache.js` | Passed | Completed during Phase 0 session |
| `node --check web/js/utils.js` | Passed | Completed during Phase 0 session |
| `node --check web/app.js` | Passed | Completed during Phase 0 session |
| `node --check scripts/check-frontend-baseline.js` | Passed | New frontend-only static check script syntax validation |
| `node scripts/check-frontend-baseline.js` | Passed | Verifies current script order, menu/detail routes, compatibility globals, and baseline secret-surface guardrails |
| `git diff --check` | Passed | Completed during Phase 0 session |
| Initial application load smoke | Not Testable | No browser session available in this environment |
| Login/session handling smoke | Not Testable | No browser session available and no approved live credentials used |
| Navigation through all main menu items | Not Testable | No browser session available |
| Databases inventory smoke | Not Testable | No browser session available |
| Databases detail drawer smoke | Not Testable | No browser session available |
| Rotate Password entry point smoke | Not Testable | No browser session available; live rotation must not be performed |
| One modal flow smoke | Not Testable | No browser session available |
| One drawer flow smoke | Not Testable | No browser session available |
| One job-details flow smoke | Not Testable | No browser session available and no safe live job data loaded |
| Browser console error review | Not Testable | No browser session available |

## 14. Scope Control Confirmation

Phase 0 baseline constraints:

- Do not split `web/app.js`.
- Do not create `web/core/`.
- Do not create page modules.
- Do not migrate Databases.
- Do not change routing architecture.
- Do not change CSS or visual style.
- Do not introduce a bundler, npm, or transpilation.
- Do not alter API behavior.
- Do not fix unrelated late-stage issues inside Phase 0.
- Do not modify backend, C++, CMake, or API contract files for frontend-only Phase 0.
