# Frontend Modularization Architecture Plan

## 1. Executive summary

ContainerCP's Web UI is a zero-build, vanilla JavaScript single-page application served directly by `containercpd`. The current runtime model loads three classic scripts in this exact order from `web/index.html`: `/js/cache.js`, `/js/utils.js`, and `/app.js`. The main application file, `web/app.js`, is 4,396 lines and contains bootstrap, authentication, routing, API calls, shared helpers, modal/toast/job utilities, all page implementations, detail pages, and page-specific state.

The new Databases DB-2 page establishes an approved future visual direction: health-first summary cards, explicit status badges, dense administrator controls, responsive card/table inventory, and right-side detail drawer. Modularization must preserve that behavior and must not become a visual redesign project.

Recommended target: native ES modules without a bundler, with a temporary compatibility bridge for existing inline event handlers while pages are extracted incrementally. This preserves the repository's zero-npm and direct-static-serving model while enabling real module boundaries, explicit imports, page lifecycle cleanup, and page-owned state.

The first implementation phase should not move pages. It should add safeguards and extract security-sensitive shared boundaries first: escaping, attribute escaping, API wrapper, notifications, modal/confirmation helpers, job polling, and a page lifecycle cleanup registry. Databases should be the first full page extraction because it is recently reviewed, visually strategic, has coherent local state, and exercises the desired drawer/filter/job patterns.

No production code is changed by this document. This is a read-only audit and implementation plan.

## 2. Current frontend architecture

### 2.1 Files and sizes

| File | Lines | Current role |
|------|-------|--------------|
| `web/index.html` | 15 | Static shell, stylesheet link, ordered classic script tags |
| `web/style.css` | 198 | All global CSS, including DB-2 dashboard styles |
| `web/js/cache.js` | 293 | Global DNS/runtime/health caches on `window` |
| `web/js/utils.js` | 524 | Global DNS/status/health helper functions on `window` |
| `web/app.js` | 4,396 | Application bootstrap, auth, routing, shared UI helpers, every page implementation |

### 2.2 Actual loading model

`web/index.html` loads:

```html
<link rel="stylesheet" href="/style.css">
<script src="/js/cache.js"></script>
<script src="/js/utils.js"></script>
<script src="/app.js"></script>
```

The scripts are classic scripts, not ES modules. All top-level `function`, `let`, and `const` declarations in `app.js` rely on classic-script global semantics or same-script scope. Many functions must remain globally reachable because inline HTML strings call handlers by name with `onclick`, `oninput`, `onchange`, and `onkeydown`.

There is no `package.json`, no Vite/Webpack config, no transpilation, and no frontend build step. `containercpd` serves files from `services_.config().source_root() + "/web"`. `WebServer::serve_static()` maps `.js` to `application/javascript`, `.css` to `text/css`, and all other unknown static extensions to `text/html`.

### 2.3 API and static serving model

The Web UI uses `API_BASE = '/ui-api'`. The WebServer handles:

| Path | Behavior |
|------|----------|
| `/` | Serves `web/index.html` |
| `/style.css`, `/app.js`, `/js/*.js` | Static file serving from `web/` |
| `/ui-api/auth/login` | Public login handler |
| `/ui-api/auth/logout` | Public logout handler |
| `/ui-api/auth/me` | Session-required auth metadata |
| `/ui-api/*` | Requires `X-Session-Token`, then proxies to REST API after stripping `/ui-api` |
| `/api/*` on Web UI listener | Also requires session and proxies to REST API |

Session tokens are stored in `localStorage` under `session_token` and sent as `X-Session-Token` on each API call.

### 2.4 Current route model

Routes are string keys passed to `navigate(page, params)`. The router is an if/else dispatch chain in `app.js:261-286`. Browser history is not used. Direct links to detail pages do not exist. Refresh always starts at auth check, then initializes and navigates to `dashboard`. Detail state is passed in memory through `params`.

Menu state is updated by toggling `.active` on `.nav-link` elements. `site-detail` is mapped to the `sites` nav item. `domain-detail`, `mail-domain`, and `mail-health` are routed but are not main menu items.

### 2.5 Current CSS model

There is one CSS file: `web/style.css`. It defines reset/base, layout, topbar/sidebar, buttons, cards, health grid, tables, details panels, tabs, activity list, badges, DB-2 dashboard/drawer styles, and responsive rules. Modularization must not split or redesign CSS in the initial work.

## 3. app.js inventory

### 3.1 Responsibility groups

| Group | Current location | Notes |
|-------|------------------|-------|
| Application bootstrap | `initApp`, `checkAuth`, DOMContentLoaded | Builds shell and sidebar from HTML string; registers top-level listeners; starts status interval |
| Router/navigation | `navigateTo`, `navigate` | String dispatch; no cleanup; no URL/history |
| Authentication/session | `sessionToken`, `currentUser`, login/change/logout/check functions | Uses localStorage and `/ui-api/auth/*` |
| API communication | `api`, `apiPost` | JSON only; throws on non-OK; limited 401 handling; no abort support |
| Global application state | `currentPage`, `searchTerm`, `window.renderTable`, caches | Cross-page coupling through shared mutable globals |
| Page-specific state | `_currentDomain`, `_domainIdForTab`, `_dmarcSelection`, `_openEvidencePanel`, `dbDashboardState`, `_proxyActionPending`, backup flags | Lives at top level and survives navigation |
| Shared rendering helpers | `card`, `tb`, `buildTable`, status/badge helpers across pages | Mixed with page code; several duplicated helpers |
| Shared UI components | Modal, toast, table, copy buttons, progress overlays | Implemented as free functions and inline HTML snippets |
| Modal/dialog logic | `showModal`, `hideModal`, native `confirm()` calls | Modal body accepts raw HTML and assumes caller escaped content |
| Drawer/detail-panel logic | DB drawer only, details-panel cards elsewhere | No shared drawer abstraction yet |
| Job polling and timeline | `pollJobProgress`, `pollRotationJob`, migration SQL polling | Multiple independent polling implementations with no cleanup contract |
| Notifications | `toast` | Central textContent-based toast creation, timeout not tracked |
| Formatting and escaping | `esc`, `escAttr`, `fmtDate`, copy helpers | `esc()` does not escape single quotes; `escAttr()` separate and localized |
| Page implementations | Dashboard, Sites, Site Detail, Domains, Domain Detail tabs, Mail, Databases, SSL, Proxy, Access, Backups, Migration, Profiles, Templates, Nodes, Logs, Settings | All in one file |
| Legacy/dead/duplicate code | Duplicate DNS helpers, duplicate `getEvidenceSteps`, undefined references | See Section 3.4 |

### 3.2 Section inventory by line range

| Section | Lines | Approx lines | Main responsibility |
|---------|-------|--------------|---------------------|
| Globals/API/toast/utils | 1-62 | 62 | API base, session globals, toast, escape/query helpers |
| Login/change/logout | 63-165 | 103 | Auth screens and auth calls |
| App shell/bootstrap | 166-236 | 71 | Sidebar/topbar, nav listeners, status interval, initial route |
| Auth check/navigation | 237-288 | 52 | Session bootstrap and route dispatch |
| Dashboard | 289-340 | 52 | Summary cards, health grid, recent jobs |
| Toolbar/table/modal helpers | 341-370 | 30 | Shared table/search and modal helpers |
| Sites/create/job/site detail/runtime/cards | 372-1011 | 640 | Sites list, create wizard, job progress, site detail, WordPress credentials, PHP mail card, runtime controls |
| Domains/domain detail/tabs/security/health | 1012-2592 | 1,581 | Domain list, progressive DNS/runtime loading, detail tabs, DNS/Mail/Security/Health logic |
| Webmail/Mail/Mail domain/Mail health | 2593-2996 | 404 | Webmail link, mail module, mail domains, mailboxes, aliases, DKIM, health |
| Databases DB-2 | 2997-3461 | 465 | Health dashboard, filters, drawer, rotation integration |
| SSL | 3462-3568 | 107 | SSL list and actions |
| Proxy | 3570-3733 | 164 | Proxy health, entries, global actions |
| Access | 3734-3757 | 24 | Access users list/remove |
| Backups | 3758-3858 | 101 | Backup list/create/restore/remove |
| Migration | 3860-4210 | 351 | myVesta backup analysis and staged migration, SQL job polling |
| Profiles/Templates/Nodes/Logs/Settings | 4212-4356 | 145 | Small read-only pages plus settings actions |
| Theme/version/status/search listeners | 4357-4396 | 40 | Global UI actions and global search listener |

### 3.3 Large functions and complex regions

| Function | Approx role | Risk |
|----------|-------------|------|
| `loadDomainOverview()` | Fetches DNS data, computes expected records, renders multiple cards, wires proxy/SSL system actions | Combines API, state, rendering, event binding, and privileged actions |
| `loadDomainDnsRecords()` | DNS tab with local duplicate formatting/copy helpers | Duplicates helpers from `utils.js` and app-level functions |
| `loadDomainMail()` | Mail-domain DNS comparison, PHP mail status, record tables | Heavy coupling to DNS cache, mail APIs, domain detail state |
| `loadDomainSecurity()` | DMARC wizard and evidence panels | Async IIFE, multiple DNS calls, event delegation, stateful evidence panels |
| `loadDomainHealth()` | Health score display from `HealthCache` | Async cache use without navigation cancellation |
| `loadMailDomain()` | Mail domain detail, mailboxes, aliases, DKIM display/copy buttons | Mixes detail rendering, secure copy pattern, and CRUD handlers |
| `loadDatabases()` plus DB helpers | Coherent DB-2 page state, filters, drawer, rotation | Good first extraction candidate but still uses globals and inline handlers |
| `loadMigration()` and migration actions | Multi-stage import UI and SQL job polling | Job interval not tied to route cleanup |

### 3.4 Legacy/dead/duplicate code identified

These are audit findings only. Do not fix them as part of the first modularization commit unless a dedicated regression task is opened.

| Finding | Current evidence | Risk |
|---------|------------------|------|
| `apiPost(path, body)` ignores method argument | Mail deletion calls `apiPost('/api/mail/domains/' + id, {}, 'DELETE')`, `apiPost('/api/mail/mailboxes/' + id, {}, 'DELETE')`, and `apiPost('/api/mail/aliases/' + id, {}, 'DELETE')` | Delete calls likely send POST rather than DELETE; extraction could accidentally preserve or expose the bug |
| `currentParams` is referenced but not defined | `removeMailbox()` and `removeAlias()` call `navigate('mail-domain', currentParams)` | Runtime error after mailbox/alias deletion |
| `loadSsl()` uses undefined `loadSite()` | SSL table domain link calls `loadSite('${esc(r.domain)}')` | Click runtime error |
| Duplicate DNS helpers | `buildFullDnsRecord`, `normalizeHostname`, `attachDataCopyListener`, `statusBadge`, `fmtVal`, `copyRowButtons` exist in both app-specific code and `utils.js` | Divergent behavior and escaping risk |
| Duplicate `getEvidenceSteps()` | Defined twice in Domain Security section | Future edits can change one copy only |
| `loadMail()` appends table to `html` inside `window.renderTable` before assigning `p.innerHTML` | `window.renderTable()` mutates local `html`, not a dedicated table container | Fragile pattern and different from other list pages |
| Multiple `console.error` calls | `cache.js`, domain detail, DNS, security, health, mail | Useful diagnostics but should be reviewed for no secret/error payload exposure |
| Multiple inline event handlers carry dynamic strings | Domains, Sites, SSL, Proxy, Mail, Migration | Escaping must be centralized before extraction |

## 4. Menu-to-code ownership matrix

The real main menu items are: Dashboard, Sites, Domains, Databases, SSL, Mail, Webmail, Proxy, Access, Backups, Migration, Profiles, Templates, Nodes, Logs, Settings.

### 4.1 Main routes

| Menu item | Route key | Entry point | Approx lines in app.js | API endpoints used | Local/page state | Global dependencies | Dialogs/jobs | CSS dependencies | Coupling |
|-----------|-----------|-------------|------------------------|--------------------|------------------|---------------------|--------------|------------------|----------|
| Dashboard | `dashboard` | `loadDashboard(p)` | 52 | `GET /api/health`, `/api/sites`, `/api/jobs`, `/api/domains`, `/api/backups`, `/api/ssl`, `/api/mail/health` | none | `api`, `card`, `qsa`, `$`, `esc` | none | `.page-header`, `.cards`, `.health-grid`, `.activity-list` | Uses async follow-up calls after initial render without cancellation |
| Sites | `sites` | `loadSites(p)` | 38 plus shared site actions | `GET /api/sites`, `GET /api/runtime/<site_id>`, `POST /api/sites/remove` | table closure over fetched data | `searchTerm`, `window.renderTable`, `buildTable`, `tb`, `api`, `apiPost`, `toast`, `confirm` | native confirm for remove | tables, badges, page header | Runtime calls update table cells by selectors after render; shared `window.renderTable` overwritten by other pages |
| Site Detail | `site-detail` | `loadSiteDetail(p, siteId)` | 477 including site cards | `GET /api/sites`, `/api/domains`, `/api/databases`, `/api/ssl`, `/api/proxy`, `/api/backups`, `/api/wordpress/database-credentials/status`, `/api/sites/<id>/mail-status`, `/api/runtime/<id>`, runtime and mail POSTs | `_rtSiteId` | WordPress credential helpers, PHP mail helpers, runtime card helpers, `navigate`, `showModal`, `pollRotationJob` | Site create job, WordPress rotation job, native confirms | `.details-panel`, `.details-grid`, `.card`, badges | Heavily coupled to Databases, Mail, Runtime, Proxy, Backups summary APIs |
| Domains | `domains` | `loadDomains(p)` | 140 plus domain detail helpers | `GET /api/domains`, `/api/domains/<fqdn>/dns-check`, `/api/runtime/<site_id>` | domains closure, progressive row state | `DnsCache`, `RuntimeCache`, `HealthCache`, `window.processBatch`, `searchTerm`, `window.renderTable` | none | table, badges | Async batch updates table row indexes; can update stale rows after navigation |
| Domain Detail | `domain-detail` | `loadDomainDetail(p, domainId)` | 1,435 including tabs | `GET /api/domains`, `/api/mail/domains`, `/api/settings`, `/api/runtime/<site_id>`, `/api/domains/<fqdn>/dns-check`, `/api/ssl/<domain>`, `/api/proxy/*`, `/api/ssl/<domain>/*`, `/api/sites/<site_id>/mail-status`, mailboxes/aliases APIs | `_currentDomain`, `_domainIdForTab`, `window._domainDetailData`, `_dmarcSelection`, `_openEvidencePanel` | `DnsCache`, `HealthCache`, `copyText`, many `window.*` DNS helpers, `runSystemAction` | Proxy/SSL confirms, evidence panel, health refresh | tabs, details, tables, cards, badges | Largest coupling point; stores detail state on `window`, mixes tab routing, DNS cache, mail, proxy, SSL, health |
| Databases | `databases` | `loadDatabases(p)` | 465 | `GET /api/databases`, `GET /api/databases/<id>`, `GET /api/wordpress/database-credentials/status?site_id=N`, `POST /api/wordpress/database-credentials/rotate`, `GET /api/jobs?id=N` | `dbDashboardState` | `api`, `apiPost`, `toast`, `showModal`, `hideModal`, `copyText`, `pollRotationJob`, `renderWordPressRotationDiagnostics` | Right drawer, rotation confirmation modal, rotation job polling | DB-2 classes, badges, details grid | Good cohesion; must preserve secret boundaries and job behavior |
| SSL | `ssl` | `loadSsl(p)` | 107 | `GET /api/ssl`, `POST /api/ssl/<domain>/issue`, `/renew`, `/enable`, `/disable`, `/redirect/enable`, `/redirect/disable` | none | `searchTerm`, `window.renderTable`, `api`, `apiPost`, `toast`, `fmtDate`, `buildTable` | action buttons, no explicit confirms in page list | table, badges | Uses undefined `loadSite()` link; actions refresh whole SSL page |
| Mail | `mail` | `loadMail(p)` | 390 including mail-domain pages | `GET /api/mail/status`, `/api/mail/domains`, `/api/domains`, `/api/mail/domains/<id>`, mailboxes/aliases APIs, DKIM/regenerate APIs, `POST /api/mail/activate`, `/deactivate`, `/mail/domains`, etc. | `window._mailDomains`, `window._dkimData` | `searchTerm`, `window.renderTable`, `showModal`, `hideModal`, `api`, `apiPost`, `toast`, `copyText`, `navigate` | Mail domain modal, mailbox modal, alias modal, native confirms | tables, details, cards, badges | Mail page and Domain Detail mail tab duplicate DNS/mail concepts; some delete calls likely broken by `apiPost` signature |
| Mail Domain | `mail-domain` | `loadMailDomain(p, id)` | included in Mail | Same as Mail plus DKIM display | `window._dkimData` | `currentParams` undefined in delete refresh handlers | Modal CRUD, native confirms | details, tables, cards | Detail route without nav item; cross-depends on Mail route |
| Mail Health | `mail-health` | `loadMailHealth(p)` | included in Mail | `GET /api/mail/health` | none | `navigate`, `api`, `esc` | none | card, badges | Route exists but main Mail page also has direct button to call `loadMailHealth($('page'))` |
| Webmail | `webmail` | `loadWebmail(p)` | 12 | none | none | none except `p` | external link | card | Standalone low-risk page |
| Proxy | `proxy` | `loadProxy(p)` | 164 | `GET /api/proxy`, `GET /api/proxy/health`, `POST /api/proxy/test`, `/reload`, `/sync`, `/recover`, `/api/proxy/remove` | `p._loading`, `_proxyActionPending` | `searchTerm`, `window.renderTable`, `api`, `apiPost`, `toast`, `navigate` | native confirm for remove; proxy action pending flag | cards, table, badges | Uses DOM property `p._loading` as state; reloads page after action |
| Access | `access` | `loadAccess(p)` | 24 | `GET /api/access-users`, `POST /api/access-users/remove` | none | `searchTerm`, `window.renderTable`, `api`, `apiPost`, `toast`, `confirm` | native confirm | table, badges | Low complexity |
| Backups | `backups` | `loadBackups(p)` | 101 | `GET /api/backups`, `GET /api/sites`, `POST /api/backups/create`, `/restore`, `/remove` | `creatingBackup`, `restoringBackup` | `showModal`, `hideModal`, `api`, `apiPost`, `toast`, native confirm | backup modal, restore/remove confirms | table, modal, badges | Operation flags global; no job progress for backup create/restore |
| Migration | `migration` | `loadMigration(p)` | 351 | `GET /api/migration/vesta/backups`, `POST /api/migration/vesta/inspect`, `/create-site`, `/import-files`, `/import-sql`, `GET /api/jobs?id=N` | form DOM state; SQL polling interval local only | `api`, `apiPost`, `esc`, `toast` | SQL import job polling interval | cards, alerts, form-group, badges | Poll interval not tied to route lifecycle; displays backend paths and messages after escaping |
| Profiles | `profiles` | `loadProfiles(p)` | 8 | `GET /api/profiles` | none | `api`, `tb`, `buildTable` | none | table, badges | Low-risk read-only page |
| Templates | `templates` | `loadTemplates(p)` | 9 | `GET /api/profiles` | none | `api`, `tb`, `buildTable` | none | table, badges | Low-risk read-only page |
| Nodes | `nodes` | `loadNodes(p)` | 8 | `GET /api/nodes` | none | `api`, `tb`, `buildTable` | none | table | Low-risk read-only page |
| Logs | `logs` | `loadLogs(p)` | 9 | `GET /api/logs` | none | `api`, `tb`, `buildTable` | refresh inline handler | table, badges | Low-risk read-only but log messages must be escaped |
| Settings | `settings` | `loadSettings(p)` | 104 | `GET /api/settings`, `POST /api/settings`, `POST /api/ssl/<host>/issue`, `/renew`, `POST /auth/change-password` | form DOM state | `api`, `apiPost`, `toast`, `$` | password form, admin SSL actions | details, cards, forms | Security-sensitive password fields and hostname/SSL operations |

### 4.2 Remaining navigation-adjacent sections

| Section | Current role | Recommended ownership |
|---------|--------------|-----------------------|
| Login and first password change | Auth screens before app shell | `core/auth.js` plus `components/form.js` later |
| Runtime card | Site Detail subcomponent | `pages/sites/runtime-card.js` or `components/runtime-card.js` after Sites extraction |
| WordPress credential card | Site Detail plus Databases rotation reuse | `core/jobs.js` for polling and `pages/sites/wordpress-credentials.js` for Site-specific card |
| Domain detail tabs | Domain module subroutes/tabs | `pages/domains/detail.js`, `pages/domains/tabs/*.js` in later phase |

## 5. Global state inventory

| State | Current owner | Classification | Recommended future owner |
|-------|---------------|----------------|--------------------------|
| `API_BASE` | `app.js` | Application-global | `core/api.js` config |
| `sessionToken` | `app.js`, localStorage | Session-global | `core/auth.js` with API injection |
| `currentUser` | `app.js` | Session-global | `core/state.js` session slice or `core/auth.js` |
| `currentPage` | `app.js` | Route-global | `core/router.js` current route |
| `searchTerm` | `app.js` | Route/page-global legacy | Page-local search state; remove global after table component migration |
| `window.renderTable` | Multiple pages | Route-global compatibility hack | Page instance `refresh()` or table controller owned by page |
| `DnsCache` | `web/js/cache.js` | Application-global cache | `core/cache/dns-cache.js` or `core/dns-cache.js` |
| `RuntimeCache` | `web/js/cache.js` | Application-global cache | `core/cache/runtime-cache.js` |
| `HealthCache` | `web/js/cache.js` | Application-global cache plus domain health logic | Split cache to `core/cache/health-cache.js`; pure scoring to `pages/domains/health-score.js` or backend later |
| `window._domainDetailData` | Domain Detail | Page-local but global | Domain detail page instance state |
| `_currentDomain`, `_domainIdForTab` | Domain Detail | Page-local | Domain detail page instance state |
| `_dmarcSelection`, `_openEvidencePanel` | Domain Security tab | Component/tab-local | Domain Security tab state object |
| `_rtSiteId` | Runtime card | Component-local | Runtime card controller |
| `window._mailDomains` | Mail create modal | Temporary operation state | Modal closure state or page state |
| `window._dkimData` | Mail domain DKIM copy | Temporary operation state | Event delegation data attributes or module closure |
| `dbDashboardState` | Databases page | Page-local | `pages/databases.js` state object with cleanup |
| `_proxyActionPending` | Proxy page | Page-local operation state | Proxy page instance state |
| `p._loading` | Proxy page DOM element | Page-local operation state | Proxy page instance state, not DOM mutation |
| `creatingBackup`, `restoringBackup` | Backups page | Page-local operation state | Backups page instance state |
| Status interval from `initApp()` | App shell | Application-global timer | `core/status.js` registered cleanup on logout/app destroy |
| Site creation job interval | Site create flow | Temporary operation/job state | `core/jobs.js` poller handle, cancelled when overlay removed or route changes |
| Rotation job timeout chain | WordPress/Databases | Temporary operation/job state | `core/jobs.js` cancellable poller with AbortController/clearTimeout |
| Migration SQL import interval | Migration page | Page-local job state | Migration page cleanup on unmount |

Avoid a heavy state framework. A small explicit state model is sufficient: `core/state.js` for session/app globals, router-managed page instances for page-local state, and cleanup handles for timers/pollers/listeners.

## 6. Dependency/coupling map

### 6.1 Hidden cross-page dependencies

| Dependency | Current form | Modularization risk |
|------------|--------------|---------------------|
| Inline handlers require globals | `onclick="removeSite(...)"`, `onclick="openDatabaseDetail(...)"`, etc. | ES module functions are not global by default; extraction must use event delegation or compatibility exports |
| Shared `searchTerm` and `window.renderTable` | List pages overwrite `window.renderTable` | Repeated navigation can leave stale table refresh behavior or search state from another page |
| Domain caches call app globals | `cache.js` calls `api()` and `fetchDnsForFqdn()` by global name | Loading order and module import cycles must be fixed before extraction |
| `utils.js` calls `esc()` and `copyText()` | Utility functions assume app.js loaded later will provide functions before runtime use | If utilities become modules, escaping and clipboard dependencies must be explicit imports |
| Domain Detail stores state on `window._domainDetailData` | Tabs read one global object | Detail page extraction must preserve tab access without leaking state to other pages |
| Job polling updates DOM by ID | `wp-rotate-msg`, `db-rotation-msg`, `progress-bar`, `migrate-sql-progress` | Pollers can update stale DOM after route changes unless cancellable |
| Global modal overlay | `showModal()` reuses `#modal-overlay` | Page unmount should close modal or modal actions can call functions from old page context |
| DB drawer global keydown listener | `document.addEventListener('keydown', ...)` near DB section | Listener is registered once today, but future module remount could duplicate it unless centralized |
| Native `confirm()` strings in pages | Direct calls throughout operations | Destructive action semantics are not centralized or testable |

### 6.2 Circular dependency risks in target modules

| Potential cycle | Cause | Avoidance |
|-----------------|-------|-----------|
| `core/api.js` <-> `core/auth.js` | API needs token; auth uses API | API accepts `getToken()` callback or auth injects token provider after initialization |
| `core/jobs.js` <-> `components/job-timeline.js` | Polling and rendering mixed today | `core/jobs.js` only fetches and schedules; component renders job objects |
| `components/table.js` <-> page modules | Column renderers need page actions | Table takes callbacks or event metadata; pages own action handlers |
| `core/router.js` <-> page modules | Router imports pages and pages import router | App registers pages; pages receive `navigate` in context |
| `core/modals.js` <-> components/confirmation | Confirmation uses modal | Keep confirmation component as thin wrapper around modal, not inverse |

## 7. Security boundaries

### 7.1 Centralize these functions first

| Boundary | Current function(s) | Future module | Reason |
|----------|---------------------|---------------|--------|
| HTML text escaping | `esc()` | `core/utils.js` | Every page interpolates API data into HTML strings |
| Attribute escaping | `escAttr()`, local ad-hoc replacements | `core/utils.js` | Data attributes and inline attribute strings require quote-safe escaping |
| JavaScript string serialization | `dbJsArg()` and ad-hoc single-quote interpolation | `core/utils.js` | Prevent handler injection while compatibility inline handlers remain |
| Safe text rendering | Direct `textContent` uses | `core/dom.js` optional | Prefer textContent for untrusted messages |
| API error normalization | `api()` throw shape | `core/api.js` | Avoid exposing stack traces, raw command output, config paths, or secrets |
| Session token handling | `localStorage.getItem/setItem/removeItem` | `core/auth.js` | One owner for token storage and logout on auth failure |
| Job diagnostics rendering | `renderWordPressRotationDiagnostics()`, `renderRotationJobTimeline()` | `components/job-timeline.js` plus `core/jobs.js` | Must keep public-safe diagnostic display and avoid raw secrets |
| Confirmation for destructive actions | scattered `confirm()` | `components/confirmation-dialog.js` | Standardize wording, typed confirmations where needed, and future accessibility |
| Modal HTML insertion | `showModal(title, bodyHtml)` | `core/modals.js` | Raw HTML modal bodies are high-risk; body builders must document escaped inputs |
| Copy button data attributes | multiple copy helpers | `components/copy-button.js` | Prevent inline handlers carrying long DKIM/DNS values |

### 7.2 Security risks during modularization

| Risk | Current evidence | Mitigation |
|------|------------------|------------|
| XSS via innerHTML interpolation | Most pages build HTML strings from API data | Require centralized escaping imports and static tests that forbid raw `message`, `domain`, `filename`, `path`, `command`, `config`, and job fields in HTML without `esc()` or textContent |
| Attribute injection | Many handlers interpolate dynamic strings into `onclick` and `data-copy` | Move to event delegation; while inline handlers remain, use `jsString()` and `escapeAttr()` centrally |
| Secret rendering | Auth passwords, mail passwords, WordPress DB credential status, migration database data, job diagnostics | Static secret-surface tests must expand across modules; no DB passwords, generated passwords, root credentials, option-file contents, SQL password literals, stack traces, or config paths in UI |
| Console logging sensitive API errors | Multiple `console.error` calls in domain/mail/cache paths | Central error logger should redact known sensitive keys or suppress raw objects for security-sensitive endpoints |
| Stale pollers after navigation | Job polling uses intervals/timeouts without route cleanup | Page lifecycle cleanup must cancel timers, AbortControllers, and pending pollers |
| Session token storage | `localStorage` stores session token | Keep centralized; consider product-owner decision on sessionStorage vs localStorage later; do not change during modularization |
| CSRF/session behavior | Header token, no cookie-based session | Keep `X-Session-Token`; modules must not bypass `core/api.js` |
| Destructive actions inconsistent | Native confirms vary by page; some operations lack typed confirmation | Central confirmation wrapper and per-action severity metadata, preserving current behavior first |
| Imported server HTML/messages | API error/job messages rendered via innerHTML in some flows | Normalize to text or escaped HTML only |

## 8. Proposed directory structure

Do not create empty modules merely to match a tree. Add modules only when code moves or a shared boundary is needed.

Recommended target:

```text
web/
  index.html
  app.js
  style.css
  core/
    api.js
    auth.js
    router.js
    state.js
    lifecycle.js
    jobs.js
    notifications.js
    modals.js
    utils.js
    clipboard.js
  components/
    badges.js
    cards.js
    table.js
    filters.js
    empty-state.js
    status-summary.js
    drawer.js
    confirmation-dialog.js
    job-timeline.js
    copy-button.js
  pages/
    dashboard.js
    sites.js
    domains.js
    databases.js
    ssl.js
    mail.js
    webmail.js
    proxy.js
    access.js
    backups.js
    migration.js
    profiles.js
    templates.js
    nodes.js
    logs.js
    settings.js
```

For Domains, later split submodules only after the main page extraction is stable:

```text
web/pages/domains/
  index.js
  detail.js
  dns-records-tab.js
  mail-tab.js
  security-tab.js
  health-tab.js
  health-score.js
```

For Sites, later split subcomponents only after page extraction is stable:

```text
web/pages/sites/
  index.js
  detail.js
  create-site-dialog.js
  runtime-card.js
  wordpress-credentials-card.js
  php-mail-card.js
```

## 9. Module responsibilities

### 9.1 Core modules

| Module | Responsibility | Must not contain |
|--------|----------------|------------------|
| `core/api.js` | Base path, token header, JSON/non-JSON parsing, HTTP error normalization, 401 handling hook, optional AbortController support | Page-specific endpoint semantics |
| `core/auth.js` | Session token storage, login/logout/change-password, `/auth/me`, auth bootstrap state | Page rendering except auth screens unless split later |
| `core/router.js` | Route registry, current route, nav active state, mount/unmount calls, optional hash/history later | Page implementation code |
| `core/state.js` | Small app/session state helpers if needed | Page-local state |
| `core/lifecycle.js` | Cleanup handle registry for listeners, timers, pollers, abort controllers | Rendering logic |
| `core/jobs.js` | Cancellable polling of `/api/jobs?id=N`; status callbacks | HTML timeline rendering |
| `core/notifications.js` | Toast creation, timeout cleanup | API semantics |
| `core/modals.js` | Modal open/close, focus, Escape/backdrop cleanup | Page-specific body generation |
| `core/utils.js` | `esc`, `escapeAttr`, `jsString`, date helpers, null display helpers | Domain/database business semantics |
| `core/clipboard.js` | Clipboard write and copy feedback | DNS-specific record construction |

### 9.2 Component modules

| Component | Responsibility | API style | Cleanup |
|-----------|----------------|-----------|---------|
| `components/badges.js` | Generic status badge HTML from label/class; optionally status maps | HTML string for current architecture | none |
| `components/cards.js` | Basic card and summary card helpers | HTML string | none |
| `components/table.js` | Table rendering with columns and escaped labels | HTML string first; later DOM controller for sorting/search | optional cleanup if it registers delegated events |
| `components/filters.js` | Search/filter/select row rendering and state callbacks | HTML string plus event delegation metadata | remove listeners via lifecycle |
| `components/empty-state.js` | Loading/empty/error states | HTML string | none |
| `components/status-summary.js` | DB-2-like health summary cards | HTML string from counts | none |
| `components/drawer.js` | Right-side drawer lifecycle, Escape/backdrop, focus | DOM controller preferred | remove listener/backdrop on cleanup |
| `components/confirmation-dialog.js` | Typed or severity confirmations using modal | Promise-based API | close modal on route unmount |
| `components/job-timeline.js` | Public-safe job timeline/failure rendering | HTML string from job object | none |
| `components/copy-button.js` | Safe data-copy buttons and delegated copy listener | HTML string plus delegated listener | remove delegated listener |

### 9.3 Final app.js responsibility

Final `web/app.js` should be an entry point of approximately 100-180 lines. It should:

- Import core modules and real page modules.
- Initialize auth/session and app shell.
- Register page modules with the router.
- Register global UI behavior: theme toggle, status/version refresh, logout, maybe topbar search.
- Start initial navigation.
- Handle unrecoverable top-level errors.
- Export only a temporary compatibility bridge while inline handlers remain.

It should not contain full page implementations, table builders, page-specific API calls, or page-specific state.

## 10. Page-module lifecycle contract

Recommended contract:

```js
export const databasesPage = {
  route: 'databases',
  nav: 'databases',
  title: 'Databases',
  async mount(ctx, params) {},
  unmount(ctx) {}
};
```

`ctx` should include:

| Field | Purpose |
|-------|---------|
| `root` | Current page DOM element, equivalent to `$('page')` |
| `api` | Shared API client |
| `navigate` | Router navigation callback |
| `toast` | Notification function |
| `modal` | Modal service |
| `confirm` | Confirmation service |
| `jobs` | Job polling service |
| `lifecycle` | Cleanup registration for this page instance |

The contract should not include separate `render()` and `load()` unless a page genuinely needs them. Most current pages perform load-and-render together. For this codebase, `mount(ctx, params)` plus explicit page-local functions is enough initially.

Cleanup rules:

| Resource | Required cleanup |
|----------|------------------|
| `setInterval` | Register `clearInterval` in lifecycle |
| `setTimeout` chains | Register timeout IDs or use cancellable job poller |
| Fetch requests | Use AbortController for long/progressive loads where useful; ignore stale generations as fallback |
| Modal/dialog | Close on route change unless explicitly application-global |
| Drawer | Close and remove Escape/backdrop listeners on route change |
| Delegated listeners | Register through lifecycle; remove on unmount |
| Page state | Store in module instance object and reset on mount/unmount |
| Stale DOM updates | Check lifecycle `active` flag before mutating DOM after await |

Compatibility while inline handlers remain:

```js
ctx.expose({
  openDatabaseDetail,
  closeDatabaseDrawer,
  refreshDatabases
});
```

`ctx.expose()` should attach temporary functions to `window.ContainerCP.actions` and optionally legacy `window.openDatabaseDetail` names. Each extracted page must remove or replace its exposed handlers on unmount, or the router must replace them on each mount.

## 11. Script loading/build strategy

### 11.1 Facts

| Question | Answer |
|----------|--------|
| Are scripts loaded as one classic script? | Three classic ordered scripts: `cache.js`, `utils.js`, `app.js` |
| Are ES modules already used? | No |
| Is there a bundler? | No |
| Is there transpilation? | No |
| Is browser compatibility constrained? | No explicit legacy-browser requirement found; Web UI targets modern admin browsers |
| Does backend embed static files? | No; it serves files from `source_root()/web` at runtime |
| Are frontend filenames referenced from C++? | Static requests are generic path-based; `index.html` references the filenames |
| Would many script tags affect CSP? | No CSP found, but many ordered classic scripts would increase load-order fragility |
| Would native ES modules work? | Yes, same-origin static `.js` is served as `application/javascript`; module imports should work in modern browsers |
| Is a bundler justified now? | No. It conflicts with zero-dep project conventions and adds deployment/install complexity |

### 11.2 Recommendation

Use native ES modules without a bundler.

Migration should be staged:

1. Keep the current classic script model while adding tests and documenting public globals.
2. Extract pure utilities into files that can be loaded by both classic compatibility and future modules if necessary.
3. Convert `app.js` to a module entry point only after core APIs and at least one page module prove the compatibility bridge works.
4. Update `index.html` to load a single module entry point:

```html
<script type="module" src="/app.js"></script>
```

5. Keep a temporary `window.ContainerCP` compatibility namespace for inline handlers during page-by-page extraction.

Deployment impact: no npm install, no build artifacts, no bundler output, no changes to `scripts/update.sh` or CMake required. The only backend requirement is continuing to serve `.js` as `application/javascript`, which already exists.

## 12. Shared component candidates

| Candidate | Pages duplicating pattern | Proposed API | HTML or DOM | Accessibility | Cleanup | Core/components |
|-----------|---------------------------|--------------|-------------|---------------|---------|-----------------|
| Status badges | Sites, Domains, SSL, Mail, DB, Proxy, Logs, Backups | `badge(label, className)`, `statusBadge(status, map)` | HTML string | Text labels must remain visible; color not sole signal | none | components |
| Table | Sites, Domains, SSL, Mail, Access, Backups, Profiles, Templates, Nodes, Logs | `renderTable({columns, rows, empty})` | HTML string initially | Proper `table`, `th`, empty state | none initially | components |
| Search toolbar | Sites, Domains, SSL, Proxy, Access, Profiles, etc. | `createSearchToolbar({title, value, onSearch})` or page-local state | DOM/event delegation preferred | Label/input relationship | remove listener | components |
| Empty/loading/error state | Nearly all pages | `emptyState(message, action?)`, `loadingState(message)`, `errorState(message, retry?)` | HTML string | `aria-live` where async | action listener cleanup | components |
| Summary cards | Dashboard and DB-2; future pages | `summaryCards(items)` | HTML string | Semantic labels and numbers | none | components |
| Right drawer | DB-2 now, future detail panels | `drawer.open({title, body, onClose})` | DOM controller | Focus, Escape, aria-modal, return focus | close/remove listeners | components |
| Confirmation dialog | DB rotation typed confirm, destructive native confirms | `confirmAction({title, message, severity, requireText})` | Promise modal | Focus, keyboard, typed text | close modal | components |
| Job timeline | Site creation, WordPress rotation, DB rotation, migration SQL | `renderJobTimeline(job)`, `renderJobFailure(job)` | HTML string | Status text visible | polling cleanup in core | components plus core jobs |
| Copy buttons | Domain DNS records, Mail DKIM, DB detail IDs | `copyButton({text, label, title})` plus delegated binder | HTML string plus listener | Button labels/titles | remove delegated listener | components |
| Filter bars | DB-2 filters; future Domains/Mail/SSL filters | `filterBar({search, filters, sort})` | HTML string plus listener | Labels for selects | remove listener | components |
| Action sections | DB detail, Proxy global actions, Settings actions | Keep page-specific initially | Page-owned | Button labels/severity | page cleanup | not yet shared |

Do not over-abstract one-off layouts. The first shared components should be security and cleanup boundaries, not visual redesign wrappers.

## 13. Databases page as reference

### 13.1 Generic pieces to extract later

| DB-2 piece | Generic candidate | Notes |
|------------|-------------------|-------|
| Summary cards | `components/status-summary.js` | Counts + label + status class are reusable |
| Health/status badges | `components/badges.js` | Generic badge renderer; DB-specific state mapping stays in page |
| Filter row | `components/filters.js` | Search, selects, sort, reset pattern reusable |
| Inventory table | `components/table.js` | Table rendering reusable; DB columns remain page-specific |
| Responsive list | Later design-system component | Requires CSS/design coordination; do not extract in modularization phase unless needed |
| Right-side drawer | `components/drawer.js` | Strong extraction candidate with cleanup/focus |
| Loading/error/empty states | `components/empty-state.js` | Already repeated across pages |
| Rotation confirmation | `components/confirmation-dialog.js` | Typed confirmation is reusable for destructive/credential actions |
| Job progress integration | `core/jobs.js` + `components/job-timeline.js` | Existing WordPress and DB rotation already share poller/rendering |
| Copyable ID values | `components/copy-button.js` | Reusable with safe data attributes |

### 13.2 Database-specific pieces that should stay in page module

| Piece | Reason |
|-------|--------|
| `computeDatabaseHealthState()` | Encodes DB-1/DB-2 product semantics |
| `dbRuntimeState`, `dbConnectionState`, `dbCredentialState`, `dbOwnershipState` | Database API field mapping |
| `databaseRotationCapability()` | Depends on WordPress credential target rules and DB state |
| DB detail sections and field list | Database-specific information architecture |
| DB sort ranking defaults | Page-specific operator workflow |

## 14. Detailed phased migration plan

### Phase 0: Baseline and safeguards

Files:

- `planning/frontend-modularization-architecture.md`
- Future test-only additions under `tests/` if approved

Functionality:

- Record current menu routes, globals, API usage, and inline handler names.
- Add static tests for script tags, route registry expectations, global handler inventory, and no secret surfaces across future module files.
- Add `node --check` validation for all `web/**/*.js` files.

Compatibility layer:

- None yet.

Tests required:

- Existing full doctest/CTest.
- `node --check web/app.js web/js/cache.js web/js/utils.js` or a script that checks all JS files.
- Static test that verifies all main menu route keys still exist.

Rollback strategy:

- Revert test/documentation-only commit.

Risks:

- Static tests can become brittle if they assert exact strings. Prefer route/function inventories over large substrings.

Commit boundary:

- One commit for documentation and baseline static safeguards.

Acceptance criteria:

- No runtime behavior changes.
- Validation remains green.
- Public/global handler list is documented.

### Phase 1: Core security utilities and compatibility namespace

Files:

- `web/core/utils.js`
- `web/core/notifications.js`
- `web/core/modals.js`
- `web/core/api.js`
- `web/core/clipboard.js`
- `web/core/compat.js`
- `web/app.js` minimal imports/bridges only if moving to modules in this phase is approved

Functionality moved:

- `esc`, `escAttr`, JS string serialization helper.
- `toast`.
- `showModal`/`hideModal`.
- `api`/`apiPost` with same behavior.
- `copyText`.

Compatibility layer required:

- Expose legacy names on `window` while inline handlers still exist: `api`, `apiPost`, `toast`, `esc`, `$`, `qs`, `qsa`, `showModal`, `hideModal`, `copyText`.

Tests required:

- Static tests that old global names exist in compatibility layer.
- API wrapper tests through existing C++ static pattern or future JS pure tests.
- `node --check` all JS.

Rollback strategy:

- Restore helpers to `app.js`; because no pages move yet, rollback is small.

Risks:

- ES module scope can break inline handlers if compatibility exports are missed.
- Utility extraction can change escaping behavior; keep exact output first.

Commit boundary:

- One commit for utility extraction only.

Acceptance criteria:

- UI behavior identical.
- Auth/session behavior identical.
- Existing static secret tests pass.

### Phase 2: Router and lifecycle infrastructure

Files:

- `web/core/router.js`
- `web/core/lifecycle.js`
- `web/core/state.js` if needed
- `web/app.js`

Functionality moved:

- `navigate()` dispatch into route registry.
- Active nav handling.
- Page mount/unmount cleanup contract.
- Status/version app-shell services remain app-level but register cleanup.

Compatibility layer required:

- `window.navigate` and `window.navigateTo` stay until inline links are removed.
- Router should support current in-memory `params` for detail pages.

Tests required:

- Static route registry test for every menu key and detail route.
- Repeated navigation smoke test plan: navigate Dashboard -> Databases -> Sites -> Databases with no duplicate listeners or stale drawers.
- Timer cleanup unit/static test if feasible.

Rollback strategy:

- Restore if/else `navigate()` chain.

Risks:

- Any missing compatibility handler breaks inline attributes.
- Premature URL/history changes would be risky; do not add history in this phase.

Commit boundary:

- One commit for router/lifecycle only, no page extraction.

Acceptance criteria:

- All existing route keys render the same pages.
- Navigation still starts at Dashboard after auth.
- No browser URL behavior changes.

### Phase 3: Extract Databases as first full page module

Files:

- `web/pages/databases.js`
- `web/components/drawer.js`
- `web/components/job-timeline.js`
- `web/components/confirmation-dialog.js` if not already created
- `web/components/status-summary.js` optionally, only if used immediately
- `web/app.js` removes DB section after compatibility bridge is proven

Functionality moved:

- `dbDashboardState` and all DB helper functions.
- DB detail drawer.
- DB rotation confirmation and job flow.

Compatibility layer required:

- Temporary global handlers: `refreshDatabases`, `openDatabaseDetail`, `closeDatabaseDrawer`, `toggleDatabaseSortDirection`, `resetDatabaseFilters`, `showDatabaseRotationConfirm`, `confirmDatabasePasswordRotation`, `computeDatabaseHealthState` if tests or console use it.

Tests required:

- Existing DB-2 static test updated to search `web/pages/databases.js` plus shared components.
- Secret-surface test expanded to all DB module files.
- Job polling cleanup test or manual checklist.
- Full validation: `node --check`, build, focused API/DB UI tests, full doctest/CTest.

Rollback strategy:

- Revert the DB extraction commit. Since DB page is self-contained, no other page should be affected.

Risks:

- Drawer Escape listener duplication.
- Rotation job poller updating closed drawer.
- Inline handler compatibility omissions.

Commit boundary:

- One commit for Databases extraction and tests only.

Acceptance criteria:

- DB-2 UI behavior remains visually and functionally identical.
- Password rotation still uses existing endpoints and job rendering.
- No DB lifecycle behavior is added.

### Phase 4: Extract low-risk read-only pages

Recommended order:

1. `webmail`
2. `nodes`
3. `profiles`
4. `templates`
5. `logs`
6. `dashboard`
7. `access`

Files:

- `web/pages/webmail.js`
- `web/pages/nodes.js`
- `web/pages/profiles.js`
- `web/pages/templates.js`
- `web/pages/logs.js`
- `web/pages/dashboard.js`
- `web/pages/access.js`

Functionality moved:

- Simple page render/load functions, using shared table/empty/card helpers where behavior-preserving.

Compatibility layer required:

- `loadLogs($('page'))` refresh handler should become either event delegation or a temporary global.
- `removeAccessUser` for Access if extracted in this phase.

Tests required:

- Static route render smoke for each page.
- API endpoint inventory tests.
- No behavior changes in table output.

Rollback strategy:

- Revert one page extraction commit at a time.

Risks:

- Shared `searchTerm/window.renderTable` migration can alter search behavior. Keep it unchanged until table component is ready.

Commit boundary:

- Prefer one page per commit or one small bundle of truly read-only pages.

Acceptance criteria:

- Each menu item renders and handles API failure state as before.

### Phase 5: Extract operational pages

Recommended order:

1. `ssl`
2. `proxy`
3. `backups`
4. `settings`
5. `mail` and `mail-domain`
6. `sites` and `site-detail`
7. `migration`
8. `domains` and `domain-detail`

Reasoning:

- SSL, Proxy, Backups, and Settings are operational but smaller than Mail/Sites/Domains.
- Mail has several existing delete/signature issues and should wait until API helper semantics are explicit.
- Sites couples to WordPress credentials, PHP mail, runtime, and many resource summaries.
- Migration has staged operations and polling, so it should wait until job cleanup infrastructure is proven.
- Domains is the largest and most coupled area; extract after all shared DNS/cache/copy/table/job patterns are stable.

Files:

- `web/pages/ssl.js`
- `web/pages/proxy.js`
- `web/pages/backups.js`
- `web/pages/settings.js`
- `web/pages/mail.js`
- `web/pages/sites.js`
- `web/pages/migration.js`
- `web/pages/domains.js` and later domain submodules

Functionality moved:

- Page-specific API actions, modals, confirmations, and job flows.

Compatibility layer required:

- Temporary globals for every inline action until event delegation replaces them.
- `apiPost` method support must be clarified before Mail deletion extraction.

Tests required:

- Static endpoint and handler tests per page.
- Operation confirm tests where possible.
- Manual smoke checklist for destructive actions against validation VM.
- No duplicate requests after repeated navigation.

Rollback strategy:

- One page per commit for operational pages.

Risks:

- Mutating action regressions.
- Existing latent bugs can be confused with extraction bugs; document baseline before moving each page.

Commit boundary:

- One operational page per commit, except very small pages if they share no mutation logic.

Acceptance criteria:

- Same API calls, same confirmation behavior, same success/error toasts, no new backend semantics.

### Phase 6: Remove legacy globals and inline handlers

Files:

- All extracted page modules
- `web/core/compat.js`
- `web/app.js`

Functionality moved:

- Replace inline `onclick`/`oninput`/`onchange` with delegated listeners or explicit component controllers.
- Remove `window.renderTable` and global `searchTerm`.
- Remove temporary `window.*` compatibility exports.

Compatibility layer required:

- None after completion.

Tests required:

- Static test forbids inline handlers except approved legacy exceptions.
- Repeated navigation and cleanup tests.
- Full menu smoke test.

Rollback strategy:

- Revert the cleanup commit; page modules should still work with compatibility bridge until final removal.

Risks:

- Inline handler removal can miss dynamically generated controls.

Commit boundary:

- Multiple commits by page/component area.

Acceptance criteria:

- `app.js` is only entry point/bootstrap.
- No page-specific functions attached globally.
- No stale timers/listeners after navigation.

## 15. Test and validation plan

### 15.1 Existing test coverage

Current frontend-related tests are C++ static checks in `tests/test_api.cpp`. They read `web/app.js` and assert endpoint usage and secret-surface constraints for WordPress credential UI and DB-2. The usual validation also runs `node --check web/app.js`. There is no browser automation, no JS DOM test harness, no route navigation test, and no listener/timer cleanup test.

### 15.2 Required modularization tests

| Area | Test approach |
|------|---------------|
| JS syntax | Run `node --check` on every `web/**/*.js` file, not just `web/app.js` |
| Script loading | Static test verifies `index.html` script model and module entry point when changed |
| Route registry | Static or JS test verifies all main menu route keys and detail routes are registered |
| App bootstrap | Static test verifies auth bootstrap and router initialization remain in app entry |
| Repeated navigation | Add a lightweight browser/manual checklist initially; optional future headless harness requires PO approval |
| Listener cleanup | Unit-test lifecycle registry if pure JS; static test for direct `addEventListener` outside lifecycle after Phase 2 |
| Timer/poll cleanup | Unit-test `core/jobs.js` cancellation and lifecycle cleanup |
| Page render | Static smoke: every page module exports route and mount; manual validation in browser |
| API errors | API wrapper tests for 401, 403, 404, JSON error, non-JSON error, network error |
| Auth expiration | Manual/API test that 401 clears token and returns to login, preserving current behavior |
| Modals | Modal close by button/backdrop/Escape; no duplicate overlays |
| Drawers | Drawer focus, Escape close, cleanup on navigation |
| Job polling | Completed/failed/timeout/cancelled states, no updates after unmount |
| Password rotation | DB and Site Details rotation still send only `site_id`, `database_id`, `confirmation` and display public-safe diagnostics |
| No duplicate requests | Manual instrumentation or future mock API test around repeated navigation |
| No stale state | Route unmount resets page-local states or creates fresh instances |
| No secret rendering | Static secret tests across all `web/**/*.js` modules |
| Responsive smoke | Manual browser checklist at desktop/tablet/mobile widths; no CSS redesign in modularization |
| Regression for every menu item | Manual checklist plus static route/menu matrix |

### 15.3 Validation commands per implementation commit

Minimum:

```bash
node --check web/app.js
git diff --check
cmake --build <build-dir> --target containercp_tests containercp containercpd -- -j1
./<build-dir>/tests/containercp_tests -tc="*API*"
ctest --test-dir <build-dir> --output-on-failure
```

After modules exist, replace single-file syntax check with all frontend JS files.

## 16. Rollback strategy

| Scope | Rollback action |
|-------|-----------------|
| Planning or tests only | Revert one commit |
| Core utility extraction | Revert helper extraction commit; app.js still contains or can regain original helpers |
| Router/lifecycle | Revert router commit to restore if/else `navigate()` |
| Single page extraction | Revert that page extraction commit only |
| Compatibility bridge removal | Revert final cleanup commit; bridge remains until all pages proven stable |
| Production deployment issue | Use normal git rollback and `scripts/update.sh`; no manual production build/install |

Each phase should be one logical commit and should be pushed with its commit according to project rules. Do not combine multiple operational page extractions in one commit.

## 17. Known risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| Inline handler globals break under ES modules | High | Temporary compatibility bridge; event delegation migration by page |
| Stale async operations mutate new pages | High | Lifecycle active flag, AbortController, cancellable pollers |
| Escaping regressions introduce XSS | High | Centralized escaping and static tests across all modules |
| Secret exposure through modularized diagnostics | High | Keep job/credential renderers centralized and tested |
| Existing latent bugs get blamed on modularization | Medium | Baseline known issues before moving each page |
| Too many small files complicate direct static serving | Low | Native module import tree avoids many script tags; no bundler needed |
| Browser compatibility of ES modules | Medium | Confirm supported browser baseline; modules require modern admin browsers |
| CSS class ownership remains global | Medium | Do not split CSS during modularization; design-system project handles later CSS architecture |
| Domain page extraction is large | High | Defer Domains until shared components and cleanup are proven |

## 18. Acceptance criteria

Architecture-level acceptance:

- `app.js` becomes a small entry point and does not contain full page implementations.
- Every main menu item has a page module with explicit route ownership.
- Shared behavior is imported from core modules, not copied between pages.
- Shared visual patterns are reusable components, without changing current appearance.
- Current routes and in-memory detail behavior remain compatible until a separate router/history task is approved.
- All API calls continue through a central API layer.
- All page timers, pollers, listeners, modals, and drawers are cleaned up on navigation.
- Existing DB-2 behavior remains unchanged.
- Existing WordPress credential rotation and job diagnostics remain public-safe.
- No new framework, npm dependency, bundler, CSS redesign, or backend behavior is introduced without product-owner approval.

Per-phase acceptance:

- Each phase is independently revertible.
- Each phase has tests appropriate to the moved responsibility.
- `node --check`, `git diff --check`, focused frontend/static tests, build, doctest, and CTest pass before commit.

## 19. Separate design-system follow-up outline

This is a separate project. Do not combine it with modularization commits.

### 19.1 Scope

The design-system migration should extract and standardize:

- Design tokens: color, typography, spacing, surfaces, borders, radii, shadows.
- Status semantics: success, warning, critical/error, info/unknown, disabled, admin/system.
- Cards: summary cards, detail cards, action cards, health cards.
- Tables: dense admin tables, responsive table-to-card behavior, empty/loading rows.
- Filters: search, select filters, sort, reset, count labels.
- Forms: labels, errors, help text, disabled/loading states.
- Drawers: right-side detail panels, Escape/backdrop/focus behavior.
- Dialogs: confirmation, typed confirmation, destructive action severity.
- Action bars: primary/secondary/destructive grouping.
- Responsive behavior: desktop/tablet/mobile breakpoints.
- Accessibility: focus management, labels, aria-live, keyboard actions, contrast.

### 19.2 Databases styles suitable as foundation

| DB-2 style/pattern | Foundation suitability |
|--------------------|------------------------|
| `.db-summary-grid` and `.db-summary-card` | Good basis for future status summary cards after generic naming |
| Health-first severity colors | Good foundation for status semantics |
| Filter row layout | Good basis for admin control bars |
| Responsive table/card switch | Good direction, needs generic component/CSS naming |
| Right-side drawer | Strong foundation for detail pages |
| Action section in detail drawer | Good pattern for safe operational actions |
| Loading/error/empty states | Good behavior pattern, needs component standardization |

### 19.3 Databases styles that are page-specific

| DB-2 style/pattern | Why page-specific |
|--------------------|-------------------|
| `.db-*` class names | Database namespace should not be reused directly by other pages |
| DB health semantics | Runtime/connection/credential ownership rules are DB-specific |
| Rotation action copy | WordPress database credential flow only |
| DB inventory columns | Database-specific information architecture |

## 20. Open questions requiring product-owner approval

1. Should ContainerCP officially require modern browsers that support native ES modules?
2. Is a temporary `window.ContainerCP` compatibility namespace acceptable during migration?
3. Should browser history/deep links be added later, or should modularization preserve the current no-history behavior indefinitely?
4. Should session tokens remain in `localStorage`, or should a later security task evaluate `sessionStorage` or another session transport?
5. Is adding optional browser automation acceptable later, even if it introduces npm or external tooling?
6. Should existing latent frontend bugs found in this audit be fixed before modularization, or tracked as separate cleanup tasks?
7. Should Domain Health Score remain frontend-owned during modularization, or should it move backend-side before Domains extraction?
8. Should native `confirm()` be preserved exactly during extraction, or may it be replaced by a modal confirmation component once behavior tests exist?
9. Should the final module tree optimize for one module per menu item, or allow submodules for complex pages such as Domains and Sites?
10. Should modularization update documentation/changelog in every phase, or should frontend architecture work have a dedicated rolling implementation log?
