# Frontend Modularization and ContainerCP UI 2.0 Checklist

Authoritative implementation tracker for the Frontend Modularization + ContainerCP UI 2.0 project.

Architectural source of truth: `planning/frontend-modularization-architecture.md`.

## Tracker Rules

States:

- `[ ]` Not Started
- `[-]` In Progress
- `[x]` Completed

Rules:

- Every implementation session must begin by reviewing this checklist.
- Every implementation session must end by updating this checklist.
- Do not implement work that is not represented by this checklist.
- Before starting any checklist item, mark it as `[-]` In Progress.
- After successful validation, mark completed items as `[x]` Completed.
- Never delete completed work from this document.
- If implementation reveals additional work, add checklist items immediately.
- If architecture changes, update this checklist in the same planning change.
- If implementation order changes, update this checklist before implementation continues.
- Production code changes must follow `planning/frontend-modularization-architecture.md`.
- The Databases DB-2 page is the visual and interaction reference for future ContainerCP pages.

## Current Planning State

- [x] Approve `planning/frontend-modularization-architecture.md` as architectural source of truth.
- [x] Create this master implementation checklist.
- [x] Re-review the architecture document for missing implementation tasks.
- [x] Represent every main menu item in the checklist.
- [x] Represent every shared component candidate in the checklist.
- [x] Represent every security boundary in the checklist.
- [x] Represent every migration phase in the checklist.
- [x] Represent validation and documentation activities in the checklist.
- [x] Start first production implementation session.

## Phase 0: Preparation and Baseline Safeguards

Objective:

Create the safety net before moving production frontend code. This phase documents the current app shape, freezes baseline expectations, and expands validation so later modularization changes can be reviewed safely.

Checklist items:

- [x] Review this checklist at the start of the implementation session.
- [x] Mark selected Phase 0 tasks as `[-]` before editing.
- [x] Record current `web/app.js`, `web/js/cache.js`, `web/js/utils.js`, `web/style.css`, and `web/index.html` sizes.
- [x] Record current main menu routes and detail routes.
- [x] Create `planning/frontend-modularization-baseline.md` as the Phase 0 implementation baseline.
- [x] Document current script loading model and script order.
- [x] Document current page render entry points.
- [x] Create route/menu regression matrix for every main menu route.
- [x] Create route/menu regression matrix entries for detail and subroutes.
- [x] Record current inline handler inventory.
- [x] Record current global compatibility names used by inline handlers.
- [x] Record current `window.*` assignments and cross-script global assumptions.
- [x] Record current API endpoint inventory per page.
- [x] Record current polling, timer, and listener inventory.
- [x] Record current modal and drawer ownership.
- [x] Record current API helper ownership.
- [x] Record current security-sensitive helper ownership.
- [x] Record current known latent frontend bugs without fixing them in this phase.
- [x] Create Phase 0 security boundary baseline.
- [x] Create explicit list of helpers that must not be duplicated during modularization.
- [x] Create behavioral snapshot for route-to-render mapping, headings, buttons, empty/error text, drawer behavior, modal behavior, and job-progress behavior.
- [x] Create Phase 0 known-risk register with status, target phase, blocking classification, and acceptance condition.
- [x] Review Phase 0 checklist quality for duplicates, missing acceptance criteria, backend/build requirements, and scope creep.
- [x] Reflect frontend-only validation rules in Phase 0 checklist and validation notes.
- [x] Add static tests for main menu route coverage.
- [x] Add static tests for detail route coverage: `site-detail`, `domain-detail`, `mail-domain`, and `mail-health`.
- [x] Add static tests for existing classic script loading order.
- [x] Add static tests for public/global handler inventory.
- [x] Add static tests for frontend secret-surface restrictions across current files.
- [x] Add a validation helper or documented command to run `node --check` on every currently loaded JavaScript file.
- [x] Run `node --check` on all currently loaded frontend JS files.
- [x] Run `git diff --check`.
- [x] Run relevant existing frontend tests if available.
- [x] Record browser/manual smoke-check results for affected routes, or mark Not Testable with reasons.
- [x] Record browser console error review, or mark Not Testable with reasons.
- [x] Confirm no full CMake configure/build, containercp/containercpd build, full CTest, GitHub Actions, or new `build-*` directory is used for frontend-only Phase 0.
- [x] Update this checklist with validation results before ending the session.
- [x] Update planning documentation according to the Phase 0 scope; defer changelog until a commit is explicitly requested unless instructed otherwise.

Phase 0 validation results, 2026-07-21:

- [x] `node --check web/js/cache.js` passed.
- [x] `node --check web/js/utils.js` passed.
- [x] `node --check web/app.js` passed.
- [x] `node --check scripts/check-frontend-baseline.js` passed.
- [x] `node scripts/check-frontend-baseline.js` passed.
- [x] `git diff --check` passed.
- [x] Browser/manual smoke checks recorded as Not Testable because no browser session is available in this environment.
- [x] Browser console review recorded as Not Testable because no browser session is available in this environment.
- [x] Full CMake configure/build, `containercp` build, `containercpd` build, full CTest, GitHub Actions, and new `build-*` directories were not used.

Expected files:

- `planning/frontend-modularization-checklist.md`
- `planning/frontend-modularization-architecture.md`
- `planning/frontend-modularization-baseline.md`
- Optional frontend-only test helper under `scripts/` only if needed and approved
- `tests/test_api.cpp` only if frontend static test changes are explicitly approved for this phase

Expected risks:

- Static tests can become brittle if they assert large raw strings.
- Existing latent bugs can be mistaken for regressions later if not documented now.
- Overly broad baseline tests can block legitimate modularization refactors.

Acceptance criteria:

- No runtime behavior changes.
- No frontend production code moved.
- Menu route inventory is documented and ready for future static test coverage.
- Global handler inventory is documented and ready for future compatibility test coverage.
- Secret-surface boundaries are documented and ready for future module-wide test coverage.
- Frontend-only validation is green or explicitly marked Not Testable with reasons.

Rollback notes:

- Revert the Phase 0 test/documentation commit.
- No production behavior rollback should be required.

## Phase 1: Core Security Utilities and Compatibility Namespace

Objective:

Centralize security-sensitive helpers and create a temporary compatibility bridge while preserving the current classic-script behavior and inline-handler reachability.

Checklist items:

- [x] Review this checklist and Phase 1 architecture notes before implementation.
- [x] Mark selected Phase 1 tasks as `[-]` before editing.
- [x] Create `web/core/utils.js` only when moving real utility code.
- [x] Move `esc` into the core utility boundary without changing output semantics.
- [x] Move attribute escaping into the core utility boundary.
- [x] Add a centralized JavaScript string serialization helper for compatibility handlers.
- [x] Preserve current date and null-display formatting behavior when moving shared helpers.
- [x] Create `web/core/api.js` only when moving real API code.
- [x] Move `api` behavior into the core API boundary without endpoint-specific logic.
- [x] Move `apiPost` behavior into the core API boundary without changing current callers.
- [x] Add explicit API error normalization boundaries without exposing raw secrets.
- [x] Preserve current `X-Session-Token` request behavior.
- [x] Preserve current 401/auth failure behavior.
- [x] Create `web/core/notifications.js` only when moving toast code.
- [x] Move `toast` into the notification boundary with current visual behavior preserved.
- [x] Track toast timeout cleanup behavior for later lifecycle integration.
- [x] Create `web/core/modals.js` only when moving modal code.
- [x] Move `showModal` and `hideModal` into the modal boundary without redesigning dialogs.
- [x] Document modal body HTML escaping responsibilities.
- [x] Create `web/core/clipboard.js` only when moving clipboard code.
- [x] Move `copyText` into the clipboard boundary with current feedback behavior preserved.
- [x] Create `web/core/context.js` only when exposing moved functions for legacy callers.
- [x] Expose `api` through the compatibility layer while inline handlers require it.
- [x] Expose `apiPost` through the compatibility layer while inline handlers require it.
- [x] Expose `toast` through the compatibility layer while inline handlers require it.
- [x] Expose `esc` through the compatibility layer while legacy utility files require it.
- [x] Expose `$`, `qs`, and `qsa` through the compatibility layer while legacy code requires them.
- [x] Expose `showModal` and `hideModal` through the compatibility layer while page code requires them.
- [x] Expose `copyText` through the compatibility layer while inline handlers require it.
- [x] Add static tests for required compatibility exports.
- [x] Add static tests for centralized escaping imports or compatibility references.
- [x] Run `node --check` on all frontend JS files.
- [x] Run `git diff --check`.
- [x] Run focused frontend/API tests.
- [x] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [x] Update this checklist with validation results before ending the session.

Expected files:

- `web/core/utils.js`
- `web/core/api.js`
- `web/core/notifications.js`
- `web/core/modals.js`
- `web/core/clipboard.js`
- `web/core/compat.js`
- `web/app.js`
- `tests/test_api.cpp`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- ES module scope can hide functions that inline handlers still call.
- Escaping output changes can create visual or security regressions.
- API helper behavior changes can affect every page.
- Moving modal code can increase raw HTML insertion risk if contracts are unclear.

Acceptance criteria:

- UI behavior remains identical.
- Auth and session behavior remain identical.
- All current inline handlers still work.
- Centralized security helpers are represented by tests.
- No page implementation is extracted in this phase.

Rollback notes:

- Revert the Phase 1 commit and restore helpers in `web/app.js`.
- Because pages are not moved, rollback should be limited to helper and compatibility files.

## Phase 2: Router and Lifecycle Infrastructure

Objective:

Replace the if/else navigation chain with a route registry and add page cleanup infrastructure while preserving current route keys, in-memory detail params, and no-history behavior.

Checklist items:

- [x] Review this checklist and Phase 2 architecture notes before implementation.
- [x] Mark selected Phase 2 tasks as `[-]` before editing.
- [x] Create `web/core/router.js` only when moving real router code.
- [x] Move route registration into the router boundary.
- [x] Preserve `navigate(page, params)` behavior.
- [x] Preserve `navigateTo(page)` behavior.
- [x] Preserve active nav mapping for `site-detail` to `sites`.
- [x] Preserve active nav mapping for `domain-detail` to `domains`.
- [x] Preserve non-menu routes `mail-domain` and `mail-health`.
- [x] Preserve auth bootstrap navigation to Dashboard.
- [x] Do not add browser history or deep links in this phase.
- [x] Create `web/core/lifecycle.js` only when adding cleanup infrastructure.
- [x] Add cleanup registration for timers.
- [x] Add cleanup registration for timeout chains.
- [x] Add cleanup registration for delegated listeners.
- [x] Add cleanup registration for modal or drawer teardown.
- [x] Add an active-generation or active-flag pattern for stale async updates.
- [x] Add optional AbortController support where it does not change behavior.
- [x] Create `web/core/state.js` only if shared app/session state is moved; not needed for this lifecycle phase.
- [x] Keep page-local state out of `core/state.js`.
- [x] Expose `window.navigate` through compatibility while inline links remain.
- [x] Expose `window.navigateTo` through compatibility while legacy listeners remain.
- [x] Add static route registry tests for every main menu item.
- [x] Add static route registry tests for every detail route.
- [x] Add a repeated-navigation validation checklist.
- [x] Add lifecycle cleanup tests or static checks where feasible.
- [x] Run `node --check` on all frontend JS files.
- [x] Run `git diff --check`.
- [x] Run focused frontend/API tests.
- [x] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [x] Update this checklist with validation results before ending the session.

Expected files:

- `web/core/router.js`
- `web/core/lifecycle.js`
- `web/core/state.js` if needed
- `web/core/compat.js`
- `web/app.js`
- `tests/test_api.cpp`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Missing route registration can blank a page.
- Missing compatibility names can break inline attributes.
- Cleanup can remove application-global behavior too early if scopes are unclear.
- Premature history support would change user-visible behavior.

Acceptance criteria:

- All existing route keys render the same page content.
- Detail pages still receive in-memory params.
- Browser URL behavior is unchanged.
- Page cleanup infrastructure exists but does not alter visible behavior.
- No page implementation is extracted in this phase.

Rollback notes:

- Revert the Phase 2 commit to restore the existing if/else `navigate()` chain.
- Keep Phase 1 helpers if they are already stable and independent.

Phase 2 lifecycle implementation results, 2026-07-21:

- [x] Router now creates one lifecycle context per route transition.
- [x] Router calls the previous page `unmount()` and lifecycle cleanup before mounting the next page.
- [x] Router accepts explicit page objects with `mount` and optional `unmount` hooks.
- [x] Every current page module exports an explicit page object used by `web/app.js` route registration.
- [x] Lifecycle context supports cleanup registration for intervals, timeouts, delegated listeners, AbortController, modal teardown, and route-local `window.renderTable` compatibility.
- [x] Sites route owns runtime row stale guards, site creation job polling, delayed navigation timeout, WordPress rotation polling, and runtime refresh timeout through the active lifecycle context.
- [x] Domains route owns list table rendering and progressive DNS/runtime/health async updates through the active lifecycle context.
- [x] Databases route owns Escape key drawer listener, drawer teardown, drawer focus timeout, detail async stale guards, and rotation polling through the active lifecycle context.
- [x] Migration route owns SQL import polling interval through the active lifecycle context.
- [x] Mail route owns table rendering and DKIM copy listeners through the active lifecycle context.
- [x] SSL, Proxy, Access, and Backups routes own route-local table refresh state through the active lifecycle context.
- [x] App shell status interval now has one owner and is cleared before reinitialization or logout.
- [x] Low-risk unused page loader globals were removed for Dashboard, Profiles, Templates, Nodes, Webmail, Settings, Access, and Backups.
- [x] Frontend baseline validation now checks lifecycle primitives, route cleanup ownership, and explicit page-object route registration.
- [x] No browser history or deep-link behavior was added.
- [x] No backend, C++, CMake, API route, CSS, visual redesign, or UI 2.0 work was included.

Repeated-navigation validation checklist:

- [x] Navigating away from a page calls previous page cleanup before mounting the next page.
- [x] Navigating to the same route again still cleans the previous route instance first.
- [x] Route-local `window.renderTable` is cleared only if it still belongs to the leaving route.
- [x] Databases Escape listener is registered per page lifecycle and removed on navigation.
- [x] Databases drawer/backdrop DOM is removed on navigation.
- [x] Migration SQL polling interval is cancelled on navigation.
- [x] Sites job polling and delayed refresh/navigation timers are cancelled on navigation.
- [x] WordPress/database rotation polling timeout chains are cancelled on navigation.
- [x] Domains progressive async row updates check the active lifecycle before mutating the DOM.

Remaining temporary compatibility exports after Phase 2:

- [x] `web/core/context.js`: `$`, `qs`, `qsa`, `api`, `apiPost`, `esc`, `escAttr`, `jsString`, `dbJsArg`, `toast`, `showModal`, `hideModal`, `destroyModal`, `copyText`, `navigate`, `navigateTo`, `pollJobProgress`, `pollRotationJob`, `renderWordPressRotationDiagnostics`, `renderRotationJobTimeline`; required by legacy helper files, modal teardown, and inline handlers until delegated-handler migration is complete.
- [x] `web/core/shell.js`: `renderLogin`, `doLogin`, `renderChangePassword`, `doChangePassword`, `doLogout`, `initApp`, `checkAuth`, `toggleTheme`, `loadVersion`, `updateStatus`; required by auth/shell inline handlers and bootstrap compatibility.
- [x] `web/js/cache.js`: `DnsCache`, `RuntimeCache`, `HealthCache`; required by Domains and existing cache helpers until cache modules are split.
- [x] `web/js/utils.js`: DNS, runtime, health, copy, and Domain Health helper globals; required by Domains and cache helpers until delegated utility imports replace them.
- [x] `web/pages/sites.js`: Site action/card/runtime globals remain for existing inline handlers.
- [x] `web/pages/domains.js`: Domain detail, tab, DNS, mail, security, health, proxy, SSL, and remove globals remain for existing inline handlers and legacy cross-helper references.
- [x] `web/pages/databases.js`: Database inventory, filter, drawer, rotation, and badge globals remain for existing inline handlers in the DB-2 page.
- [x] `web/pages/mail.js`: Mail module, mail-domain, mailbox, alias, DKIM, and mail-health globals remain for existing inline handlers.
- [x] `web/pages/ssl.js`: SSL action/status/link globals remain for existing inline handlers.
- [x] `web/pages/proxy.js`: Proxy action/remove globals remain for existing inline handlers; internal helper exports remain temporary while proxy inline handlers are migrated.
- [x] `web/pages/backups.js`: Backup modal/create/restore/remove globals remain for existing inline handlers.
- [x] `web/pages/access.js`: `removeAccessUser` remains for existing inline handlers.
- [x] `web/pages/logs.js`: `loadLogs` remains for the existing Refresh inline handler.
- [x] `web/pages/migration.js`: Migration analyze/import globals remain for existing inline handlers.
- [x] `web/pages/settings.js`: Settings action globals remain for existing inline handlers.

Phase 2 validation results, 2026-07-21:

- [x] `node --check` passed for every `web/**/*.js`, `web/*.js`, and `scripts/check-frontend-baseline.js` file.
- [x] `node scripts/check-frontend-baseline.js` passed.
- [x] Module import smoke check passed with a minimal browser API stub.
- [x] `git diff --check` passed.
- [x] Browser production smoke was already completed by deployment feedback before this phase; browser execution is otherwise not directly available in this environment.
- [x] Full CMake configure/build, `containercp` build, `containercpd` build, full CTest, GitHub Actions, backend changes, C++ changes, CMake changes, and API route changes were not used.

## Phase 3: Application State Boundaries

Objective:

Define explicit ownership for app/session/global state and isolate page-local state so later page modules can mount and unmount without leaking stale data.

Checklist items:

- [x] Review this checklist and global state inventory before implementation.
- [x] Mark selected Phase 3 tasks as `[-]` before editing.
- [ ] Move `API_BASE` ownership to the API boundary.
- [ ] Move `sessionToken` ownership to the auth boundary.
- [ ] Move `currentUser` ownership to the auth or app-state boundary.
- [ ] Move `currentPage` ownership to the router boundary.
- [ ] Preserve `localStorage` session token behavior unless a separate approved task changes it.
- [ ] Document the future `sessionStorage` decision as deferred.
- [ ] Keep `searchTerm` behavior unchanged until table/search extraction is ready.
- [ ] Keep `window.renderTable` compatibility until page tables are extracted.
- [ ] Identify all page-local state that must move with each page module.
- [ ] Identify status interval ownership and cleanup behavior.
- [ ] Identify job poller ownership before page extraction continues.
- [ ] Identify cache ownership for `DnsCache`, `RuntimeCache`, and `HealthCache`.
- [ ] Do not move Domain Health Score ownership until the PO decision is recorded.
- [ ] Add state ownership documentation to this checklist or architecture appendix if needed.
- [ ] Add tests or static checks for forbidden new page state in `web/app.js` after extraction starts.
- [ ] Run `node --check` on all frontend JS files if any JS changes occur.
- [ ] Run `git diff --check`.
- [ ] Run focused frontend/API tests if any JS changes occur.
- [ ] Run frontend-only validation if any production JS changes occur; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/core/state.js` if needed
- `web/core/auth.js` if auth state moves here
- `web/core/api.js`
- `web/core/router.js`
- `web/app.js`
- `tests/test_api.cpp`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Moving session state can unintentionally log users out.
- Moving global search too early can regress list filtering.
- Cache ownership changes can create import cycles.
- Domain health ownership remains an unresolved product decision.

Acceptance criteria:

- Session behavior is unchanged.
- Page-local state ownership is documented before page extraction.
- No heavy state framework is introduced.
- Future page state has a clear owner and cleanup path.

Rollback notes:

- Revert the state boundary commit.
- Preserve existing `localStorage` token semantics during rollback.

## Phase 4: Shared Component Foundations

Objective:

Extract reusable components only where they reduce security or cleanup risk and are immediately used. Do not redesign pages or split CSS in this phase.

Checklist items:

- [x] Review this checklist and shared component candidates before implementation.
- [x] Mark selected Phase 4 tasks as `[-]` before editing.
- [x] Create `web/components/badges.js` only when replacing real duplicated badge code.
- [x] Track Badges component extraction independently.
- [x] Create `web/components/cards.js` only when replacing real duplicated card helpers.
- [x] Track Summary Cards component extraction independently.
- [x] Create `web/components/table.js` only when replacing real table rendering code.
- [x] Track Tables component extraction independently.
- [x] Create `web/components/filters.js` only when replacing real search/filter controls.
- [x] Track Filters component extraction independently.
- [x] Create `web/components/drawer.js` only when replacing DB drawer or future detail drawer code.
- [x] Track Drawers component extraction independently.
- [x] Create `web/components/confirmation-dialog.js` only when replacing typed/native confirm flows.
- [x] Track Dialogs component extraction independently.
- [x] Ensure native `confirm()` behavior is preserved until replacement is explicitly approved.
- [x] Create `web/components/job-timeline.js` only when replacing existing job timeline rendering.
- [x] Track Job Timeline component extraction independently.
- [x] Create `web/components/empty-state.js` only when replacing repeated loading/empty/error markup.
- [x] Track Loading States component extraction independently.
- [x] Track Error States component extraction independently.
- [x] Track Empty States component extraction independently.
- [x] Create `web/components/copy-button.js` only when replacing data-copy or copy helpers.
- [x] Track Copy Button behavior with security tests.
- [x] Track Search component extraction independently.
- [x] Track Toolbar component extraction independently.
- [x] Preserve existing CSS classes unless a separate UI 2.0 design-system task approves changes.
- [x] Preserve existing HTML output where static tests depend on it.
- [x] Add component syntax tests.
- [x] Add static checks that component helpers escape untrusted labels and values.
- [x] Run `node --check` on all frontend JS files.
- [x] Run `git diff --check`.
- [x] Run focused frontend/API tests.
- [x] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [x] Update this checklist with validation results before ending the session.

Expected files:

- `web/components/badges.js`
- `web/components/cards.js`
- `web/components/table.js`
- `web/components/filters.js`
- `web/components/drawer.js`
- `web/components/confirmation-dialog.js`
- `web/components/job-timeline.js`
- `web/components/empty-state.js`
- `web/components/copy-button.js`
- `web/app.js`
- `tests/test_api.cpp`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Over-abstracting one-off layouts can obscure page intent.
- Changing component HTML can alter screenshots and user workflows.
- Dialog replacement can change destructive-action semantics.
- Drawer focus or Escape handling can duplicate listeners.

Acceptance criteria:

- Components are introduced only with immediate production usage.
- Existing visual behavior is preserved.
- Security-sensitive components have explicit escaping behavior.
- Cleanup-sensitive components register teardown through lifecycle.

Rollback notes:

- Revert the component extraction commit.
- If components are used by a page extraction, revert that page extraction first or in the same logical rollback.

## Component Extraction Tracker

Objective:

Track each shared component independently across all phases.

Checklist items:

- [x] Badges: identify current status badge variants across Sites, Domains, SSL, Mail, Databases, Proxy, Logs, and Backups.
- [x] Badges: define generic `badge` and `statusBadge` behavior without page-specific semantics.
- [x] Badges: migrate first real caller.
- [x] Badges: add escaping/static tests.
- [x] Summary Cards: identify Dashboard and DB-2 card patterns.
- [x] Summary Cards: extract count/label/status rendering without DB-specific health logic.
- [x] Summary Cards: migrate first real caller.
- [x] Summary Cards: add responsive smoke validation.
- [x] Tables: identify current table builder and table call sites.
- [x] Tables: define column renderer contract.
- [x] Tables: preserve existing table HTML output where required.
- [x] Tables: keep route-local `searchTerm` compatibility until inline handler cleanup completes.
- [x] Filters: identify DB-2 filters and list-page search patterns.
- [x] Filters: define search/select/sort/reset contract.
- [x] Filters: migrate DB-2 or another immediate caller.
- [x] Filters: register event cleanup through lifecycle.
- [x] Drawers: extract DB-2 right drawer behavior into generic CSS/component helpers.
- [x] Drawers: implement Escape/backdrop/focus cleanup through lifecycle.
- [x] Drawers: validate close on navigation.
- [x] Drawers: DB-specific classes remain as compatibility aliases while generic UI 2.0 drawer classes exist.
- [x] Dialogs: inventory native `confirm()` and typed confirmation usages.
- [x] Dialogs: preserve native confirm behavior until modal replacement is approved.
- [x] Dialogs: extract typed confirmation helper without changing existing destructive flows.
- [x] Dialogs: track destructive action severity through helper naming.
- [x] Notifications: preserve current toast behavior.
- [x] Notifications: track timeout cleanup.
- [x] Notifications: avoid rendering raw API error HTML.
- [x] Notifications: add validation for text-only messages.
- [x] Job Timeline: inventory site creation, WordPress rotation, DB rotation, and migration SQL job renderers.
- [x] Job Timeline: separate job polling from timeline rendering.
- [x] Job Timeline: preserve public-safe diagnostics.
- [x] Job Timeline: validate completed, failed, timeout, and cancelled states.
- [x] Loading States: inventory current loading markup.
- [x] Loading States: extract reusable loading state only when replacing a real caller.
- [x] Loading States: add `aria-live` where async status is visible.
- [x] Loading States: validate UI 2.0 CSS migration.
- [x] Error States: inventory current API error rendering.
- [x] Error States: centralize escaped error display.
- [x] Error States: avoid raw command/config/path output unless explicitly safe.
- [x] Error States: add static security checks.
- [x] Empty States: inventory table and page empty messages.
- [x] Empty States: extract reusable empty state helper.
- [x] Empty States: preserve action buttons where pages currently have them.
- [x] Empty States: validate desktop and mobile rendering.
- [x] Search: inventory all pages using global search.
- [x] Search: define page-local search state contract.
- [x] Search: keep global search bridge as documented compatibility until inline handlers are removed.
- [x] Search: validate search reset on navigation through lifecycle-owned render hooks.
- [x] Toolbar: inventory page headers and action bars.
- [x] Toolbar: extract only common admin toolbar behavior.
- [x] Toolbar: keep page-specific actions page-owned.
- [x] Toolbar: preserve current button order and labels.

Expected files:

- `web/components/*.js`
- `web/core/lifecycle.js`
- `web/core/utils.js`
- Page modules that immediately consume the component
- `tests/test_api.cpp`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Component extraction can become a redesign project.
- Generic components can accidentally encode DB-specific behavior.
- Security-sensitive rendering can regress if escaping is inconsistent.

Acceptance criteria:

- Every listed component has a tracked owner and validation path.
- Components are not created as empty placeholders.
- Component extraction remains behavior-preserving.

Rollback notes:

- Revert the component-specific commit.
- Keep this section updated if a component is deferred or split.

## Security Checklist

Objective:

Protect the Web UI while modularizing. Security boundaries must be centralized before large page extraction.

Checklist items:

- [ ] HTML escaping: centralize `esc` and preserve current output before improving behavior.
- [ ] HTML escaping: add static tests for untrusted API fields interpolated into HTML.
- [ ] Attribute escaping: centralize `escAttr` or equivalent quote-safe escaping.
- [ ] Attribute escaping: replace ad-hoc attribute escaping with the central helper.
- [ ] JavaScript string serialization: add a central helper for temporary inline handlers.
- [ ] JavaScript string serialization: remove ad-hoc single-quote interpolation as pages migrate.
- [ ] textContent usage: prefer textContent for untrusted plain messages.
- [ ] textContent usage: document where HTML strings are still required.
- [ ] innerHTML review: inventory all direct `innerHTML` assignments.
- [ ] innerHTML review: require escaped inputs or trusted component builders.
- [ ] Secret rendering review: expand static tests across all `web/**/*.js` files.
- [ ] Secret rendering review: block database passwords, generated passwords, root credentials, option-file contents, SQL password literals, stack traces, and config paths.
- [ ] Console logging review: inventory current `console.error` calls.
- [ ] Console logging review: redact or suppress raw sensitive objects for credential, mail, migration, and job endpoints.
- [ ] Storage review: keep `localStorage` behavior unchanged during modularization.
- [ ] Storage review: track later product decision about `sessionStorage` or alternate session transport.
- [ ] Password handling: preserve password field behavior in auth and settings.
- [ ] Password handling: never render password values returned from APIs.
- [ ] Rotation UI: preserve DB and WordPress rotation request payload limits.
- [ ] Rotation UI: verify rotation sends only `site_id`, `database_id`, and `confirmation` where applicable.
- [ ] Confirmation dialogs: preserve existing native confirm behavior until replacement is approved.
- [ ] Confirmation dialogs: track destructive action severity for future modal confirmation.
- [ ] Job diagnostics: centralize public-safe job diagnostics rendering.
- [ ] Job diagnostics: test that raw secrets and config paths are not displayed.
- [ ] API error rendering: normalize errors before showing them to users.
- [ ] API error rendering: avoid raw stack traces and command output.
- [ ] Copy buttons: prevent long DNS/DKIM values from being embedded unsafely in inline handlers.
- [ ] Copy buttons: prefer delegated copy handlers with safe data attributes.
- [ ] CSRF/session behavior: keep `X-Session-Token` header transport through central API client.
- [ ] CSRF/session behavior: prevent page modules from bypassing `core/api.js`.
- [ ] Stale async operations: cancel or ignore route-stale updates.
- [ ] Stale async operations: test job pollers and progressive DNS/runtime loads.

Expected files:

- `web/core/utils.js`
- `web/core/api.js`
- `web/core/auth.js`
- `web/core/jobs.js`
- `web/core/modals.js`
- `web/components/confirmation-dialog.js`
- `web/components/job-timeline.js`
- `web/components/copy-button.js`
- `tests/test_api.cpp`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Security improvements can unintentionally change visible behavior if combined with extraction.
- Static secret tests can miss dynamically assembled strings.
- Legacy inline handlers remain a risk until event delegation replaces them.

Acceptance criteria:

- Every security-sensitive boundary has an owner module or tracked legacy owner.
- Static tests scan all frontend module files.
- Page modules do not bypass centralized API and escaping boundaries.
- Rotation diagnostics remain public-safe.

Rollback notes:

- Revert the security-boundary commit if behavior changes unexpectedly.
- Do not roll back secret-surface tests unless they are demonstrably incorrect.

## Phase 5: Databases Module Extraction

Objective:

Extract Databases first because DB-2 is recently reviewed, coherent, security-sensitive, and the reference implementation for future ContainerCP pages.

Checklist items:

- [x] Review this checklist and the Databases reference section before implementation.
- [x] Mark selected Databases tasks as `[-]` before editing.
- [ ] Create `web/pages/databases.js`.
- [ ] Register the `databases` route through the router.
- [ ] Move Databases page rendering from `web/app.js`.
- [ ] Move Databases page state into a page-local state object.
- [ ] Move Databases filter state into the page module.
- [ ] Move Databases sort state into the page module.
- [ ] Move Databases event handlers into the page module or compatibility bridge.
- [ ] Move `dbDashboardState` out of global `app.js` scope.
- [ ] Move `computeDatabaseHealthState` with tests or compatibility exposure as needed.
- [ ] Keep database-specific health semantics inside the Databases page module.
- [ ] Preserve DB-2 summary card behavior.
- [ ] Preserve DB-2 search behavior.
- [ ] Preserve DB-2 filter behavior.
- [ ] Preserve DB-2 sort behavior.
- [ ] Preserve DB-2 desktop table behavior.
- [ ] Preserve DB-2 mobile card behavior.
- [ ] Preserve DB-2 detail drawer behavior.
- [ ] Preserve DB-2 WordPress credential status behavior.
- [ ] Preserve DB-2 password rotation confirmation behavior.
- [ ] Preserve DB-2 rotation job polling and timeline behavior.
- [ ] Preserve DB-2 copy-to-clipboard behavior.
- [ ] Do not add database create/drop/import/export/backup/Adminer UI behavior.
- [ ] Do not add backend database lifecycle behavior.
- [ ] Expose temporary `refreshDatabases` compatibility handler if inline handlers still require it.
- [ ] Expose temporary `openDatabaseDetail` compatibility handler if inline handlers still require it.
- [ ] Expose temporary `closeDatabaseDrawer` compatibility handler if inline handlers still require it.
- [ ] Expose temporary `toggleDatabaseSortDirection` compatibility handler if inline handlers still require it.
- [ ] Expose temporary `resetDatabaseFilters` compatibility handler if inline handlers still require it.
- [ ] Expose temporary `showDatabaseRotationConfirm` compatibility handler if inline handlers still require it.
- [ ] Expose temporary `confirmDatabasePasswordRotation` compatibility handler if inline handlers still require it.
- [ ] Remove duplicated code from `web/app.js` after Databases module is registered.
- [ ] Update DB-2 static tests to scan `web/pages/databases.js` and shared components.
- [ ] Expand secret-surface tests to Databases module files.
- [ ] Validate drawer Escape cleanup on repeated navigation.
- [ ] Validate rotation job poller cleanup after drawer close and route change.
- [ ] Run `node --check` on all frontend JS files.
- [ ] Run `git diff --check`.
- [ ] Run focused Database dashboard UI tests.
- [ ] Run focused API tests.
- [ ] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update docs for the Databases module extraction.
- [ ] Update changelog for the Databases extraction commit.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/pages/databases.js`
- `web/components/drawer.js`
- `web/components/job-timeline.js`
- `web/components/confirmation-dialog.js`
- `web/components/status-summary.js` if immediately used
- `web/core/jobs.js`
- `web/core/compat.js`
- `web/app.js`
- `web/index.html` only if module entry conversion is part of the approved commit
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Inline handler compatibility omissions can break controls.
- Drawer Escape listener can duplicate after remount.
- Rotation poller can update a closed drawer or stale route.
- Secret diagnostics can accidentally broaden during extraction.

Acceptance criteria:

- Databases UI is visually and functionally identical to DB-2.
- Existing DB inventory and WordPress rotation endpoints are unchanged.
- No new database lifecycle actions are introduced.
- DB-specific state is no longer global.
- Databases extraction is independently revertible.

Rollback notes:

- Revert the Databases extraction commit.
- Since Databases should be self-contained, other pages should remain unaffected.

## Phase 6: Low-Risk Page Module Extraction

Objective:

Extract low-risk read-only or simple pages after core/router/lifecycle and Databases prove the module pattern.

Checklist items:

- [x] Review this checklist and Phase 6 order before implementation.
- [x] Mark selected low-risk page tasks as `[-]` before editing.
- [ ] Extract Webmail page after confirming no hidden API dependencies.
- [ ] Webmail: create page module.
- [ ] Webmail: move rendering.
- [ ] Webmail: move events.
- [ ] Webmail: regression validate external link behavior.
- [ ] Webmail: update documentation.
- [ ] Extract Nodes page.
- [ ] Nodes: create page module.
- [ ] Nodes: move rendering.
- [ ] Nodes: move API call.
- [ ] Nodes: move table usage.
- [ ] Nodes: regression validate empty/error state.
- [ ] Nodes: update documentation.
- [ ] Extract Profiles page.
- [ ] Profiles: create page module.
- [ ] Profiles: move rendering.
- [ ] Profiles: move API call.
- [ ] Profiles: move table usage.
- [ ] Profiles: regression validate profile rows.
- [ ] Profiles: update documentation.
- [ ] Extract Templates page.
- [ ] Templates: create page module.
- [ ] Templates: move rendering.
- [ ] Templates: move API call.
- [ ] Templates: move table usage.
- [ ] Templates: regression validate template rows.
- [ ] Templates: update documentation.
- [ ] Extract Logs page.
- [ ] Logs: create page module.
- [ ] Logs: move rendering.
- [ ] Logs: move API call.
- [ ] Logs: replace or expose refresh handler.
- [ ] Logs: regression validate escaped log messages.
- [ ] Logs: update documentation.
- [ ] Extract Dashboard page.
- [ ] Dashboard: create page module.
- [ ] Dashboard: move rendering.
- [ ] Dashboard: move summary API calls.
- [ ] Dashboard: add stale async update guard.
- [ ] Dashboard: regression validate health grid and recent jobs.
- [ ] Dashboard: update documentation.
- [ ] Extract Access page.
- [ ] Access: create page module.
- [ ] Access: move rendering.
- [ ] Access: move API call.
- [ ] Access: move remove action or expose temporary compatibility handler.
- [ ] Access: preserve native confirm behavior.
- [ ] Access: regression validate remove action against validation VM if approved.
- [ ] Access: update documentation.
- [ ] Keep one page per commit unless pages are truly read-only and independent.
- [ ] Update route/menu tests after each page extraction.
- [ ] Run `node --check` on all frontend JS files after each extraction.
- [ ] Run `git diff --check` after each extraction.
- [ ] Run focused frontend/API tests after each extraction.
- [ ] Run frontend-only validation after each extraction commit; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update changelog for each extraction commit.
- [ ] Update this checklist with validation results before ending each session.

Expected files:

- `web/pages/webmail.js`
- `web/pages/nodes.js`
- `web/pages/profiles.js`
- `web/pages/templates.js`
- `web/pages/logs.js`
- `web/pages/dashboard.js`
- `web/pages/access.js`
- `web/app.js`
- `web/core/router.js`
- Shared components as immediately consumed
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Shared `searchTerm` and `window.renderTable` migration can change list filtering.
- Logs can expose raw messages if escaping changes.
- Dashboard async follow-up calls can update after navigation.

Acceptance criteria:

- Every extracted low-risk menu item renders as before.
- API failure behavior remains unchanged or is explicitly documented if centralized.
- No duplicate listeners or stale async updates appear after repeated navigation.
- Each page extraction is independently revertible.

Rollback notes:

- Revert one page extraction commit at a time.
- Keep shared core/router changes if they are already validated and independent.

## Phase 7: SSL Module Extraction

Objective:

Extract the SSL page after lower-risk pages, preserving existing certificate actions and explicitly tracking the current undefined `loadSite()` issue.

Checklist items:

- [x] Review this checklist and SSL architecture notes before implementation.
- [x] Mark selected SSL tasks as `[-]` before editing.
- [ ] Baseline the existing `loadSite()` link issue before extraction.
- [ ] Decide whether the `loadSite()` issue is fixed before extraction or tracked separately.
- [ ] Create `web/pages/ssl.js`.
- [ ] Register the `ssl` route.
- [ ] Move SSL rendering.
- [ ] Move SSL page API calls.
- [ ] Move SSL action handlers.
- [ ] Preserve issue/renew/enable/disable/redirect action behavior.
- [ ] Preserve current toast behavior.
- [ ] Preserve current refresh-after-action behavior.
- [ ] Move SSL table/search behavior without changing global search semantics prematurely.
- [ ] Replace or expose temporary inline handlers.
- [ ] Add endpoint inventory tests for SSL endpoints.
- [ ] Add route/menu tests for SSL module.
- [ ] Regression validate SSL page render.
- [ ] Regression validate SSL actions against validation VM if approved.
- [ ] Run `node --check` on all frontend JS files.
- [ ] Run `git diff --check`.
- [ ] Run focused frontend/API tests.
- [ ] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update docs and changelog.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/pages/ssl.js`
- `web/app.js`
- `web/core/router.js`
- `web/core/compat.js`
- Shared table/badge components if immediately used
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- SSL actions mutate production-facing certificate/proxy state.
- Existing undefined `loadSite()` bug can be confused with extraction regression.
- Missing compatibility handlers can break action buttons.

Acceptance criteria:

- SSL page renders and performs existing actions as before.
- Existing latent bug handling is documented.
- No new SSL backend semantics are introduced.

Rollback notes:

- Revert the SSL extraction commit only.

## Phase 8: Proxy Module Extraction

Objective:

Extract Proxy after lifecycle cleanup is proven because it has global actions, removal confirmation, and page-local operation state.

Checklist items:

- [x] Review this checklist and Proxy architecture notes before implementation.
- [x] Mark selected Proxy tasks as `[-]` before editing.
- [ ] Baseline `_proxyActionPending` and `p._loading` behavior.
- [ ] Create `web/pages/proxy.js`.
- [ ] Register the `proxy` route.
- [ ] Move Proxy rendering.
- [ ] Move Proxy health API calls.
- [ ] Move Proxy entry API calls.
- [ ] Move Proxy global action handlers.
- [ ] Move Proxy remove handler.
- [ ] Move Proxy operation state into page-local state.
- [ ] Preserve native confirm behavior for remove.
- [ ] Preserve action pending behavior.
- [ ] Preserve reload/refresh behavior after proxy actions.
- [ ] Replace or expose temporary inline handlers.
- [ ] Add endpoint inventory tests for Proxy endpoints.
- [ ] Add no-duplicate-request validation after repeated navigation.
- [ ] Regression validate proxy health display.
- [ ] Regression validate proxy global actions against validation VM if approved.
- [ ] Run `node --check` on all frontend JS files.
- [ ] Run `git diff --check`.
- [ ] Run focused frontend/API tests.
- [ ] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update docs and changelog.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/pages/proxy.js`
- `web/app.js`
- `web/core/router.js`
- `web/core/compat.js`
- Shared table/badge/card components if immediately used
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Proxy actions affect live routing.
- DOM-attached state can be lost if moved incorrectly.
- Repeated navigation can duplicate action handlers.

Acceptance criteria:

- Proxy UI behavior is unchanged.
- Page-local action state is not stored on DOM elements.
- No duplicate proxy actions occur after repeated navigation.

Rollback notes:

- Revert the Proxy extraction commit only.

## Phase 9: Backups Module Extraction

Objective:

Extract Backups after modal and confirmation boundaries are stable, preserving create/restore/remove behavior and operation flags.

Checklist items:

- [x] Review this checklist and Backups architecture notes before implementation.
- [x] Mark selected Backups tasks as `[-]` before editing.
- [ ] Baseline `creatingBackup` and `restoringBackup` behavior.
- [ ] Create `web/pages/backups.js`.
- [ ] Register the `backups` route.
- [ ] Move Backups rendering.
- [ ] Move Backups API calls.
- [ ] Move create backup modal logic.
- [ ] Move restore backup action.
- [ ] Move remove backup action.
- [ ] Move operation flags into page-local state.
- [ ] Preserve native confirm behavior for restore/remove.
- [ ] Preserve current toast behavior.
- [ ] Preserve current lack of job progress behavior unless separately approved.
- [ ] Replace or expose temporary inline handlers.
- [ ] Add endpoint inventory tests for Backups endpoints.
- [ ] Regression validate backup list render.
- [ ] Regression validate create/restore/remove against validation VM if approved.
- [ ] Run `node --check` on all frontend JS files.
- [ ] Run `git diff --check`.
- [ ] Run focused frontend/API tests.
- [ ] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update docs and changelog.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/pages/backups.js`
- `web/app.js`
- `web/core/router.js`
- `web/core/compat.js`
- Shared modal/table/badge components if immediately used
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Backup actions mutate stored backup files.
- Operation flags can get stuck if state cleanup changes.
- Adding job progress would be scope creep.

Acceptance criteria:

- Backups page behavior is unchanged.
- Operation flags are page-local.
- No new backup semantics are introduced.

Rollback notes:

- Revert the Backups extraction commit only.

## Phase 10: Settings Module Extraction

Objective:

Extract Settings with extra security review because it includes password changes and admin SSL operations.

Checklist items:

- [x] Review this checklist and Settings architecture notes before implementation.
- [x] Mark selected Settings tasks as `[-]` before editing.
- [ ] Baseline password-change form behavior.
- [ ] Baseline admin SSL action behavior.
- [ ] Create `web/pages/settings.js`.
- [ ] Register the `settings` route.
- [ ] Move Settings rendering.
- [ ] Move settings load/save API calls.
- [ ] Move password-change API call.
- [ ] Move admin SSL issue/renew actions.
- [ ] Preserve password field handling.
- [ ] Preserve current toast behavior.
- [ ] Ensure passwords are never logged or rendered.
- [ ] Replace or expose temporary inline handlers.
- [ ] Add endpoint inventory tests for Settings endpoints.
- [ ] Add secret-surface tests for password fields and API errors.
- [ ] Regression validate settings render.
- [ ] Regression validate password change only in a safe validation environment if approved.
- [ ] Run `node --check` on all frontend JS files.
- [ ] Run `git diff --check`.
- [ ] Run focused frontend/API tests.
- [ ] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update docs and changelog.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/pages/settings.js`
- `web/app.js`
- `web/core/router.js`
- `web/core/auth.js` if password behavior is centralized
- `web/core/compat.js`
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Password handling regressions are high impact.
- Admin SSL operations can mutate live certificate state.
- Error rendering can expose sensitive details.

Acceptance criteria:

- Settings page behavior is unchanged.
- Password values are never rendered or logged.
- Admin SSL actions preserve current behavior.

Rollback notes:

- Revert the Settings extraction commit only.

## Phase 11: Mail and Webmail Module Extraction

Objective:

Extract Mail only after API helper semantics are explicit because existing delete calls appear to pass unsupported DELETE intent through `apiPost`.

Checklist items:

- [x] Review this checklist and Mail architecture notes before implementation.
- [x] Mark selected Mail tasks as `[-]` before editing.
- [ ] Confirm whether Webmail was already extracted in Phase 6.
- [ ] Baseline Mail delete behavior before extraction.
- [ ] Decide whether `apiPost` method support is fixed before Mail extraction or tracked separately.
- [ ] Baseline `currentParams` undefined issue before extraction.
- [ ] Create `web/pages/mail.js`.
- [ ] Register the `mail` route.
- [ ] Register the `mail-domain` detail route.
- [ ] Register the `mail-health` route.
- [ ] Move Mail dashboard rendering.
- [ ] Move Mail status API calls.
- [ ] Move Mail domain list rendering.
- [ ] Move Mail domain create modal.
- [ ] Move Mail domain remove action.
- [ ] Move Mail activation/deactivation actions.
- [ ] Move Mail Domain detail rendering.
- [ ] Move mailbox create action.
- [ ] Move mailbox remove action.
- [ ] Move alias create action.
- [ ] Move alias remove action.
- [ ] Move DKIM display and copy behavior.
- [ ] Move DKIM regenerate action.
- [ ] Move Mail Health rendering.
- [ ] Move `window._mailDomains` into page-local or modal-local state.
- [ ] Move `window._dkimData` into page-local state or delegated copy data.
- [ ] Preserve native confirm behavior.
- [ ] Preserve current toast behavior.
- [ ] Replace or expose temporary inline handlers.
- [ ] Add endpoint inventory tests for Mail endpoints.
- [ ] Add secret-surface tests for mail passwords and DKIM values.
- [ ] Regression validate Mail page render.
- [ ] Regression validate mailbox/alias actions against validation VM if approved.
- [ ] Regression validate DKIM copy behavior.
- [ ] Run `node --check` on all frontend JS files.
- [ ] Run `git diff --check`.
- [ ] Run focused frontend/API tests.
- [ ] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update docs and changelog.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/pages/mail.js`
- `web/pages/webmail.js` if not already extracted
- `web/app.js`
- `web/core/router.js`
- `web/core/api.js`
- `web/core/compat.js`
- Shared modal/table/badge/copy components if immediately used
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Existing delete behavior may be broken before extraction.
- Undefined `currentParams` can be mistaken for a new regression.
- Mail and Domain Detail duplicate DNS/mail concepts.
- DKIM and credential surfaces require careful escaping/copy handling.

Acceptance criteria:

- Mail, Mail Domain, Mail Health, and Webmail behavior are preserved.
- Existing latent bugs are either fixed in a dedicated task or documented as baseline.
- Mail-specific global state is removed.
- No secrets are rendered or logged.

Rollback notes:

- Revert the Mail extraction commit only.
- Keep any separately committed API helper bug fix only if validated independently.

## Phase 12: Sites Module Extraction

Objective:

Extract Sites and Site Detail after Databases, jobs, modals, runtime cards, and WordPress credential patterns are proven.

Checklist items:

- [x] Review this checklist and Sites architecture notes before implementation.
- [x] Mark selected Sites tasks as `[-]` before editing.
- [ ] Baseline Sites list behavior.
- [ ] Baseline Site Detail behavior.
- [ ] Baseline site create wizard and job overlay behavior.
- [ ] Baseline WordPress credential card behavior.
- [ ] Baseline PHP mail card behavior.
- [ ] Baseline runtime card behavior.
- [ ] Create `web/pages/sites.js` or `web/pages/sites/index.js` when splitting is approved.
- [ ] Register the `sites` route.
- [ ] Register the `site-detail` route.
- [ ] Move Sites list rendering.
- [ ] Move Sites search/table behavior.
- [ ] Move site create modal or dialog.
- [ ] Move site creation job polling.
- [ ] Move site remove action.
- [ ] Move Site Detail rendering.
- [ ] Move Site Detail API summary calls.
- [ ] Move runtime card behavior.
- [ ] Move WordPress credentials card behavior.
- [ ] Move PHP mail card behavior.
- [ ] Move Site Detail page-local state.
- [ ] Preserve native confirm behavior.
- [ ] Preserve job progress overlay behavior.
- [ ] Preserve WordPress rotation behavior and diagnostics.
- [ ] Preserve runtime action behavior.
- [ ] Replace or expose temporary inline handlers.
- [ ] Add endpoint inventory tests for Sites and Site Detail endpoints.
- [ ] Add job cleanup tests for site creation and WordPress rotation.
- [ ] Add secret-surface tests for WordPress credentials.
- [ ] Regression validate Sites list.
- [ ] Regression validate site create/remove against validation VM if approved.
- [ ] Regression validate Site Detail cards and actions.
- [ ] Run `node --check` on all frontend JS files.
- [ ] Run `git diff --check`.
- [ ] Run focused frontend/API tests.
- [ ] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update docs and changelog.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/pages/sites.js` or `web/pages/sites/index.js`
- Optional `web/pages/sites/detail.js`
- Optional `web/pages/sites/create-site-dialog.js`
- Optional `web/pages/sites/runtime-card.js`
- Optional `web/pages/sites/wordpress-credentials-card.js`
- Optional `web/pages/sites/php-mail-card.js`
- `web/app.js`
- `web/core/router.js`
- `web/core/jobs.js`
- `web/core/compat.js`
- Shared modal/table/badge/job components if immediately used
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Sites couples to many resource summaries.
- Job polling can update stale overlays.
- Runtime actions can affect live containers.
- WordPress credential diagnostics must remain public-safe.

Acceptance criteria:

- Sites and Site Detail behavior are unchanged.
- Site creation/removal flows remain API-backed.
- Site-local state is not global.
- Job pollers clean up on route changes.

Rollback notes:

- Revert the Sites extraction commit only.
- If submodules were introduced, revert the whole Sites extraction as one logical unit.

## Phase 13: Migration Module Extraction

Objective:

Extract Migration after core job cleanup is proven because staged imports and SQL polling have route-cleanup risk.

Checklist items:

- [x] Review this checklist and Migration architecture notes before implementation.
- [x] Mark selected Migration tasks as `[-]` before editing.
- [ ] Baseline myVesta backup list behavior.
- [ ] Baseline inspect/create-site/import-files/import-sql behavior.
- [ ] Baseline SQL import polling behavior.
- [ ] Create `web/pages/migration.js`.
- [ ] Register the `migration` route.
- [ ] Move Migration rendering.
- [ ] Move Migration API calls.
- [ ] Move staged form DOM state into page-local state where feasible.
- [ ] Move SQL import job polling into cancellable job infrastructure.
- [ ] Preserve backend path/message escaping behavior.
- [ ] Preserve current toast behavior.
- [ ] Replace or expose temporary inline handlers.
- [ ] Add endpoint inventory tests for Migration endpoints.
- [ ] Add job cleanup tests for SQL import polling.
- [ ] Add secret-surface tests for migration database data and paths.
- [ ] Regression validate migration page render.
- [ ] Regression validate staged migration against validation VM if approved.
- [ ] Run `node --check` on all frontend JS files.
- [ ] Run `git diff --check`.
- [ ] Run focused frontend/API tests.
- [ ] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update docs and changelog.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/pages/migration.js`
- `web/app.js`
- `web/core/router.js`
- `web/core/jobs.js`
- `web/core/compat.js`
- Shared job/loading/error components if immediately used
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Migration surfaces backend paths and database information.
- SQL job polling can update stale DOM after navigation.
- Staged operations can be difficult to validate without a real backup artifact.

Acceptance criteria:

- Migration behavior is unchanged.
- SQL polling is cancellable or stale-safe.
- Sensitive migration data remains escaped and public-safe.

Rollback notes:

- Revert the Migration extraction commit only.

## Phase 14: Domains Module Extraction

Objective:

Extract Domains last because it is the largest coupling area and depends on DNS cache, runtime cache, health cache, mail, proxy, SSL, copy, tabs, and progressive async updates.

Checklist items:

- [x] Review this checklist and Domains architecture notes before implementation.
- [x] Mark selected Domains tasks as `[-]` before editing.
- [ ] Resolve or explicitly defer the Domain Health Score ownership decision.
- [ ] Baseline Domains list behavior.
- [ ] Baseline Domain Detail overview tab behavior.
- [ ] Baseline Domain Detail DNS records tab behavior.
- [ ] Baseline Domain Detail mail tab behavior.
- [ ] Baseline Domain Detail security tab behavior.
- [ ] Baseline Domain Detail health tab behavior.
- [ ] Baseline proxy/SSL system actions from Domain Detail.
- [ ] Baseline duplicate DNS helper behavior.
- [ ] Create `web/pages/domains.js` or `web/pages/domains/index.js` when splitting is approved.
- [ ] Register the `domains` route.
- [ ] Register the `domain-detail` route.
- [ ] Move Domains list rendering.
- [ ] Move Domains progressive DNS loading.
- [ ] Move Domains progressive runtime loading.
- [ ] Move Domains table/search behavior.
- [ ] Move Domain Detail shell rendering.
- [ ] Move Domain Detail tab state out of globals.
- [ ] Move overview tab rendering.
- [ ] Move DNS records tab rendering.
- [ ] Move mail tab rendering.
- [ ] Move security tab rendering.
- [ ] Move health tab rendering.
- [ ] Move DMARC selection state out of globals.
- [ ] Move evidence panel state out of globals.
- [ ] Move `window._domainDetailData` into page-local state.
- [ ] Move `_currentDomain` into page-local state.
- [ ] Move `_domainIdForTab` into page-local state.
- [ ] Consolidate duplicate DNS helpers after behavior baseline is protected.
- [ ] Consolidate duplicate `getEvidenceSteps()` after behavior baseline is protected.
- [ ] Preserve DNS cache behavior.
- [ ] Preserve Runtime cache behavior.
- [ ] Preserve Health cache behavior unless separately changed.
- [ ] Preserve copy behavior for DNS and evidence values.
- [ ] Preserve native confirm behavior for system actions.
- [ ] Replace or expose temporary inline handlers.
- [ ] Add endpoint inventory tests for Domains and Domain Detail endpoints.
- [ ] Add stale async update tests or manual validation for progressive DNS/runtime loads.
- [ ] Add secret-surface tests for DNS, mail, security, health, job, and system-action messages.
- [ ] Regression validate Domains list.
- [ ] Regression validate every Domain Detail tab.
- [ ] Regression validate proxy/SSL actions against validation VM if approved.
- [ ] Run `node --check` on all frontend JS files.
- [ ] Run `git diff --check`.
- [ ] Run focused frontend/API tests.
- [ ] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update docs and changelog.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/pages/domains.js` or `web/pages/domains/index.js`
- Optional `web/pages/domains/detail.js`
- Optional `web/pages/domains/dns-records-tab.js`
- Optional `web/pages/domains/mail-tab.js`
- Optional `web/pages/domains/security-tab.js`
- Optional `web/pages/domains/health-tab.js`
- Optional `web/pages/domains/health-score.js`
- `web/js/cache.js` or future cache modules if cache ownership moves
- `web/js/utils.js` or future utility modules if DNS helpers move
- `web/app.js`
- `web/core/router.js`
- `web/core/compat.js`
- Shared table/badge/copy/loading/error components if immediately used
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Domains is the largest frontend coupling point.
- Progressive async updates can mutate stale rows.
- Domain Detail stores state on `window` today.
- Mail, proxy, SSL, DNS, runtime, and health behavior overlap.
- Health score ownership remains a product decision until resolved.

Acceptance criteria:

- Domains and Domain Detail behavior are unchanged.
- Every Domain Detail tab works after repeated navigation.
- No Domain state remains global except approved temporary compatibility exports.
- Duplicate DNS/security helpers are consolidated only after tests protect behavior.
- Stale async updates are cancelled or ignored.

Rollback notes:

- Revert the Domains extraction commit only.
- Prefer splitting Domains into smaller submodule commits only after the main route extraction strategy is validated.

## Phase 15: Legacy Cleanup

Objective:

Remove compatibility globals, inline handlers, and legacy shared mutable state after all page modules are extracted and validated.

Checklist items:

- [x] Review this checklist and legacy cleanup architecture notes before implementation.
- [x] Mark selected legacy cleanup tasks as `[-]` before editing.
- [ ] Inventory all remaining inline `onclick` handlers.
- [ ] Inventory all remaining inline `oninput` handlers.
- [ ] Inventory all remaining inline `onchange` handlers.
- [ ] Inventory all remaining inline `onkeydown` handlers.
- [ ] Replace inline handlers with delegated listeners or component controllers.
- [ ] Remove `window.renderTable` after all table owners are page-local.
- [ ] Remove global `searchTerm` after all search owners are page-local.
- [ ] Remove temporary `window.ContainerCP.actions` exports that are no longer needed.
- [ ] Remove legacy direct `window.*` handler exports that are no longer needed.
- [ ] Remove dead duplicate helpers from `web/app.js`.
- [ ] Remove page-specific API calls from `web/app.js`.
- [ ] Remove page-specific state from `web/app.js`.
- [ ] Confirm final `web/app.js` is only entry point/bootstrap/router registration/global UI setup.
- [ ] Confirm final `web/app.js` target size is approximately 100-180 lines or document why not.
- [ ] Convert `web/index.html` to a single module entry point only after compatibility bridge is proven.
- [ ] Keep no npm, no bundler, no transpilation.
- [ ] Add static test forbidding new inline handlers except approved exceptions.
- [ ] Add static test forbidding page implementation blocks in `web/app.js`.
- [ ] Add full menu smoke validation.
- [ ] Add repeated-navigation cleanup validation.
- [ ] Run `node --check` on all frontend JS files.
- [ ] Run `git diff --check`.
- [ ] Run focused frontend/API tests.
- [ ] Run frontend-only validation; do not run full CMake build or CTest unless compiled/backend files changed with explicit approval.
- [ ] Update docs and changelog.
- [ ] Update this checklist with validation results before ending the session.

Expected files:

- `web/index.html`
- `web/app.js`
- `web/core/compat.js`
- All extracted page modules
- All used shared components
- `tests/test_api.cpp`
- `docs/WEB-UI.md`
- `CHANGELOG.md`
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Removing inline handlers can miss dynamically generated controls.
- Removing globals can break console/debug workflows or tests if dependencies remain.
- Module entry conversion can expose browser compatibility assumptions.

Acceptance criteria:

- No page-specific functions are attached globally.
- No legacy inline handlers remain except documented approved exceptions.
- No stale timers/listeners after navigation.
- `app.js` is a small entry point.
- Native ES modules are used without bundler or npm dependency.

Rollback notes:

- Revert cleanup commits by page/component area.
- Keep the compatibility bridge available until all pages are proven stable.

## Phase 16: Validation Hardening

Objective:

Maintain reliable validation for every modularization commit and close the current test gaps without prematurely adding external tooling.

Checklist items:

- [ ] Review this checklist and validation plan before every implementation session.
- [ ] Maintain `node --check` coverage for every `web/**/*.js` file.
- [ ] Maintain static script loading tests.
- [ ] Maintain route registry tests for every main menu route.
- [ ] Maintain route registry tests for every detail route.
- [ ] Maintain app bootstrap tests.
- [ ] Maintain page module export tests.
- [ ] Maintain API endpoint inventory tests per page.
- [ ] Maintain API error normalization tests.
- [ ] Maintain auth expiration behavior tests or manual checklist.
- [ ] Maintain modal close validation.
- [ ] Maintain drawer close and cleanup validation.
- [ ] Maintain job polling cancellation tests.
- [ ] Maintain password rotation diagnostics tests.
- [ ] Maintain no-duplicate-request validation.
- [ ] Maintain no-stale-state validation.
- [ ] Maintain no-secret-rendering static tests across all frontend modules.
- [ ] Maintain responsive smoke checklist for desktop, tablet, and mobile widths.
- [ ] Maintain regression validation for every menu item.
- [ ] Evaluate optional browser automation only after product-owner approval.
- [ ] Keep validation commands documented for every implementation commit.
- [ ] Record validation results in this checklist after each completed phase.

Expected files:

- `tests/test_api.cpp`
- Optional future JS/browser test files only after approval
- Optional validation scripts only after approval
- `planning/frontend-modularization-checklist.md`
- `docs/WEB-UI.md`

Expected risks:

- C++ static tests alone cannot prove browser runtime behavior.
- Browser automation can introduce npm or external tooling, which requires approval.
- Manual smoke checklists can drift if not updated with route changes.

Acceptance criteria:

- Each completed phase records validation results.
- Syntax, static, modular baseline, and browser/manual frontend validation pass before commits; full build/CTest are reserved for approved compiled/backend changes.
- Testing gaps are either closed or explicitly documented.

Rollback notes:

- Revert validation hardening changes only if they are incorrect.
- Do not remove useful failing tests to make implementation pass; fix the implementation or update the architecture/checklist.

## Phase 17: Documentation and Project Tracking

Objective:

Keep planning, user documentation, API references, changelog, and project status aligned with every modularization phase.

Checklist items:

- [ ] Review this checklist at the end of every implementation session.
- [ ] Update this checklist with completed, in-progress, and newly discovered work.
- [ ] Update `docs/WEB-UI.md` when user-visible frontend structure or behavior changes.
- [ ] Update `docs/api/API_REFERENCE.md` only if API behavior or documented endpoint use changes.
- [ ] Update `planning/project-status.md` when phase status changes.
- [ ] Update `CHANGELOG.md` for every committed task or bug fix according to project rules.
- [ ] Record validation results in changelog entries.
- [ ] Record known risks in changelog entries.
- [ ] Keep `planning/frontend-modularization-architecture.md` unchanged unless architecture is intentionally revised.
- [ ] If architecture is revised, update this checklist in the same planning change.
- [ ] If implementation order changes, update this checklist before code changes continue.
- [ ] Preserve completed checklist items.
- [ ] Keep open product-owner decisions visible until resolved.
- [ ] Record decisions on native ES module browser support.
- [ ] Record decisions on temporary `window.ContainerCP` compatibility namespace.
- [ ] Record decisions on future browser history/deep links.
- [ ] Record decisions on session storage strategy.
- [ ] Record decisions on optional browser automation/tooling.
- [ ] Record decisions on known latent bug handling.
- [ ] Record decisions on Domain Health Score ownership.
- [ ] Record decisions on native confirm replacement.
- [ ] Record decisions on one-module-per-menu versus submodules for complex pages.

Expected files:

- `planning/frontend-modularization-checklist.md`
- `planning/frontend-modularization-architecture.md`
- `planning/project-status.md`
- `docs/WEB-UI.md`
- `docs/api/API_REFERENCE.md`
- `CHANGELOG.md`

Expected risks:

- Checklist drift can allow work outside the approved architecture.
- Changelog omissions can break project process.
- Unresolved product decisions can block later phases if not tracked.

Acceptance criteria:

- Checklist reflects reality at the end of each session.
- Documentation matches shipped behavior.
- Each implementation commit has matching validation and changelog records.

Rollback notes:

- Documentation-only changes can be reverted independently.
- Do not delete completed checklist history during rollback.

## Current Modularization Implementation Results

Objective:

Record the actual repository state after the first complete frontend-only modularization implementation pass.

Checklist items:

- [x] Native ES module entry point implemented in `web/app.js`.
- [x] `web/index.html` loads `/app.js` as `type="module"`.
- [x] `web/app.js` is reduced to route registration and imports only.
- [x] Core API/session owner implemented in `web/core/api.js`.
- [x] Core DOM helpers implemented in `web/core/dom.js`.
- [x] Core escaping helpers implemented in `web/core/utils.js`.
- [x] Core notification owner implemented in `web/core/notifications.js`.
- [x] Core modal owner implemented in `web/core/modals.js`.
- [x] Core clipboard owner implemented in `web/core/clipboard.js`.
- [x] Core router owner implemented in `web/core/router.js`.
- [x] Core shell/auth/status/theme owner implemented in `web/core/shell.js`.
- [x] Core job helper owner implemented in `web/core/jobs.js`.
- [x] Compatibility exports implemented through `web/core/context.js` and page-level `Object.assign(window, ...)` shims.
- [x] Shared component helper files created under `web/components/`.
- [x] Dashboard page module created.
- [x] Sites and Site Detail page module created.
- [x] Domains and Domain Detail page module created.
- [x] Databases page module created.
- [x] SSL page module created.
- [x] Mail, Mail Domain, and Mail Health page module created.
- [x] Webmail page module created.
- [x] Proxy page module created.
- [x] Access page module created.
- [x] Backups page module created.
- [x] Migration page module created.
- [x] Profiles page module created.
- [x] Templates page module created.
- [x] Nodes page module created.
- [x] Logs page module created.
- [x] Settings page module created.
- [x] Existing `cache.js` and `utils.js` adjusted for module import compatibility without changing their public cache/helper names.
- [x] Mail delete handlers now keep the current mail-domain id instead of referencing undefined `currentParams`.
- [x] `apiPost` now honors the existing optional method argument used by Mail `DELETE` calls while defaulting to `POST`.
- [x] SSL domain links now resolve through a frontend `loadSite(domain)` helper instead of calling an undefined function.
- [x] Duplicate Domain Security `getEvidenceSteps()` implementation removed.
- [x] Modular baseline validation script updated for native module entry and page-module ownership.
- [x] Documentation updated for the frontend module layout.
- [x] Changelog updated for this frontend-only implementation.
- [x] Project status updated for the Web UI module layout.
- [x] Frontend-only validation completed for the implementation pass.
- [x] Production API proxy path regression fixed after `f23d738` by preserving `/api` when building `/ui-api/api/...` URLs in `web/core/api.js`.
- [x] Frontend baseline validation now verifies `api('/api/sites')` resolves to `/ui-api/api/sites`.
- [x] Frontend baseline validation now verifies `api('/api/databases')` resolves to `/ui-api/api/databases`.
- [x] Frontend baseline validation now verifies `api('/auth/me')` resolves to `/ui-api/auth/me`.
- [x] Frontend API-call prefix scan confirmed every frontend API helper call uses `/api...` except `/auth...` endpoints.
- [ ] Browser/manual route smoke checks remain Not Testable until a browser session is available.
- [ ] Browser console review remains Not Testable until a browser session is available.
- [ ] Legacy inline event handlers remain as documented temporary compatibility until browser-validated delegated handler migration is performed.
- [ ] Temporary global compatibility shims remain until inline event handlers are removed.
- [ ] Route-level cancellable cleanup for all async loaders/pollers remains follow-up work; high-risk shared job helpers are centralized first.

Expected files:

- `web/app.js`
- `web/index.html`
- `web/core/*.js`
- `web/components/*.js`
- `web/pages/*.js`
- `web/js/cache.js`
- `web/js/utils.js`
- `scripts/check-frontend-baseline.js`
- `docs/WEB-UI.md`
- `planning/frontend-modularization-checklist.md`
- `planning/frontend-modularization-baseline.md`
- `planning/project-status.md`
- `CHANGELOG.md`

Expected risks:

- Browser runtime behavior is not fully validated until a browser session is available.
- Temporary compatibility globals and inline handlers remain the largest remaining cleanup surface.
- Some lifecycle cleanup remains page-specific until delegated handler migration is browser-validated.

Acceptance criteria:

- Native module entry loads without module-linkage errors in a stubbed import smoke check.
- All frontend JavaScript files pass `node --check`.
- Modular baseline static checks pass.
- `git diff --check` passes.
- No backend, C++, CMake, or API contract files are modified.

Rollback notes:

- Revert the modularization commit to restore the previous monolithic `web/app.js` and classic script tags.

## UI 2.0 Design System Tracker: Active Migration

Objective:

Track future design-system work without implementing it during frontend modularization. DB-2 is the reference implementation, but `db-*` class names and DB-specific semantics must not be reused as generic UI primitives.

Checklist items:

- [x] Design Tokens: define colors after modularization stabilizes.
- [x] Design Tokens: define typography after modularization stabilizes.
- [x] Design Tokens: define spacing after modularization stabilizes.
- [x] Design Tokens: define surfaces, borders, radii, and shadows after modularization stabilizes.
- [x] Colors: map success, warning, critical/error, info/unknown, disabled, and admin/system semantics.
- [x] Typography: define admin density and hierarchy rules.
- [x] Spacing: define compact operational spacing rules.
- [x] Cards: generalize summary cards, detail cards, action cards, and health cards.
- [x] Tables: generalize dense admin tables.
- [x] Tables: generalize responsive table-to-card behavior.
- [x] Forms: generalize labels, errors, help text, disabled states, and loading states.
- [x] Drawers: generalize right-side detail panels with focus and Escape behavior.
- [x] Badges: generalize visible status semantics without color-only meaning.
- [x] Dialogs: generalize confirmation and typed confirmation patterns.
- [x] Notifications: define toast/alert hierarchy.
- [x] Job Timeline: define public-safe operational timeline presentation.
- [x] Loading States: define consistent async loading patterns.
- [x] Error States: define consistent escaped error display.
- [x] Empty States: define consistent empty states and optional actions.
- [x] Search: define search input and reset behavior.
- [x] Toolbar: define page action grouping.
- [x] Responsive Layout: define desktop, tablet, and mobile breakpoints.
- [x] Accessibility: define focus management rules.
- [x] Accessibility: define labels, aria-live, keyboard actions, and contrast requirements.
- [x] Databases reference: identify DB-2 styles suitable for generic names.
- [x] Databases reference: identify DB-2 styles that must stay page-specific.
- [x] Implement UI 2.0 CSS redesign only in this approved UI 2.0 migration phase.

Expected files:

- Future design-system planning document if approved
- `web/style.css` only during approved UI 2.0 implementation
- Future component CSS or token files only after approval
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Design-system work can expand modularization scope.
- Reusing `db-*` classes generically can bake in database-specific semantics.
- CSS redesign can obscure behavior-preserving extraction review.

Acceptance criteria:

- UI 2.0 remains tracked but unimplemented until approved.
- Databases remains the reference visual direction.
- Modularization commits do not redesign pages.

Rollback notes:

- Future UI 2.0 commits must be independently revertible from modularization commits.

UI 2.0 implementation results, 2026-07-21:

- [x] `web/style.css` now imports the formal design-system layer from `web/styles/`.
- [x] Design-system CSS files created with real ownership: `tokens.css`, `base.css`, `layout.css`, `components.css`, `cards.css`, `tables.css`, `forms.css`, `badges.css`, `drawer.css`, `dialogs.css`, `states.css`, and `responsive.css`.
- [x] Dark-theme tokens remain the default and light-theme token overrides remain available.
- [x] Status semantics standardized for Healthy, Warning, Critical, Unknown, Running, Stopped, Connected, Failed, Available, Missing, Invalid, Managed, Imported, Valid, Expiring, Expired, Enabled, and Disabled.
- [x] Shared component helpers completed for PageHeader, SummaryCards, StatusBadge, FilterBar/SearchBox, InventoryTable, ResponsiveInventoryCards, DetailDrawer/DrawerSection/StatusRow, ConfirmationDialog helpers, JobTimeline, LoadingState, EmptyState, ErrorState, CopyButton, Toolbar-compatible page actions, and Tabs-compatible CSS.
- [x] Dashboard migrated to UI 2.0 overview with summary cards, service health, recent jobs, and quick navigation.
- [x] Sites migrated with health-focused summary cards while preserving create, runtime, WordPress credential, PHP mail, and detail-page behavior.
- [x] SSL migrated with certificate summary cards and existing searchable inventory/actions preserved.
- [x] Proxy migrated with runtime/config/recovery summary cards and existing operational actions preserved.
- [x] Backups migrated with backup summary cards and create/restore/remove actions preserved.
- [x] Access migrated with access-user summary cards and remove behavior preserved.
- [x] Profiles, Templates, Nodes, Logs, Settings, Webmail migrated to shared headers and summary/status patterns without forcing unnecessary dashboards.
- [x] Mail and Mail Health migrated to shared headers and summary cards while preserving current mail domain, mailbox, alias, DKIM, activate/deactivate, and health behavior.
- [x] Domains migrated with summary cards for domain count, linked sites, usable HTTPS, and active mail while preserving DNS/Mail/Security/Health detail functionality.
- [x] Migration migrated with shared header and staged workflow summary while preserving existing import actions and SQL job polling.
- [x] Databases retains the approved DB-2 header, summary grid, filter toolbar, inventory, drawer, and rotation reference behavior instead of using generic PageHeader conversion.
- [x] Generic responsive table behavior added so non-DB inventory tables stack on mobile; DB-2 keeps its approved responsive card inventory.
- [x] Browser smoke and console review remain dependent on an available browser session in this environment; production browser validation should use the checklist below.
- [x] Blocking Databases visual regression after UI 2.0 CSS extraction fixed by restoring missing DB-2 CSS owners for `.db-summary-grid`, `.db-summary-card`, `.db-summary-value`, `.db-inventory`, and `.db-inventory-title`.
- [x] Databases generic header conversion reverted to the approved `page-header db-page-header` structure.
- [x] Baseline validation now protects approved DB-2 header and layout CSS rules so future modular CSS changes cannot silently drop them.
- [x] Root cause identified: CSS modularization grouped DB-specific selectors with generic UI selectors and omitted standalone DB-2 grid/inventory rules, causing summary cards and inventory/filter layout regressions.
- [x] Real browser validation attempted but not runnable in this environment because no `chromium`, `chromium-browser`, `google-chrome`, `google-chrome-stable`, or `firefox` executable is available.

## Known Latent Frontend Bug Tracker

Objective:

Keep audit-discovered bugs visible so they are not confused with modularization regressions.

Checklist items:

- [x] `apiPost(path, body)` method argument issue: fixed in frontend API helper by honoring the existing optional method argument while preserving default POST behavior.
- [x] `currentParams` undefined in mailbox/alias delete refresh handlers: fixed by passing the current mail-domain id to delete handlers.
- [x] SSL undefined `loadSite(...)` link: fixed with a frontend helper that resolves the matching site and navigates to Site Detail.
- [ ] Duplicate DNS helpers: consolidate only after tests protect current behavior.
- [x] Duplicate `getEvidenceSteps()`: consolidated during Domains module extraction.
- [ ] `loadMail()` table mutation pattern: baseline before Mail extraction.
- [ ] Existing `console.error` usage: review for sensitive payloads before affected page extraction.
- [ ] Inline event handlers with dynamic strings: replace through compatibility and event delegation phases.
- [ ] Multiple job pollers/timers without route cleanup: fix through lifecycle and job phases.

Expected files:

- `planning/frontend-modularization-checklist.md`
- Future production files only in dedicated bug-fix commits
- `CHANGELOG.md` for each bug fix
- `tests/test_api.cpp` or future test files

Expected risks:

- Fixing latent bugs inside extraction commits can make reviews harder.
- Leaving latent bugs undocumented can create false regression reports.

Acceptance criteria:

- Each latent bug has a decision before its affected page is extracted.
- Bug fixes are separate logical commits unless required for safe extraction.

Rollback notes:

- Revert bug-fix commits independently from page extraction commits when possible.

## Final Architecture Acceptance Tracker

Objective:

Track completion of the full modularization architecture.

Checklist items:

- [x] `web/app.js` is a small entry point and no longer contains full page implementations.
- [x] Every main menu item has explicit page module ownership.
- [x] Dashboard has explicit page module ownership.
- [x] Sites has explicit page module ownership.
- [x] Site Detail has explicit route/module ownership.
- [x] Domains has explicit page module ownership.
- [x] Domain Detail has explicit route/module ownership.
- [x] Databases has explicit page module ownership.
- [x] SSL has explicit page module ownership.
- [x] Mail has explicit page module ownership.
- [x] Mail Domain has explicit route/module ownership.
- [x] Mail Health has explicit route/module ownership.
- [x] Webmail has explicit page module ownership.
- [x] Proxy has explicit page module ownership.
- [x] Access has explicit page module ownership.
- [x] Backups has explicit page module ownership.
- [x] Migration has explicit page module ownership.
- [x] Profiles has explicit page module ownership.
- [x] Templates has explicit page module ownership.
- [x] Nodes has explicit page module ownership.
- [x] Logs has explicit page module ownership.
- [x] Settings has explicit page module ownership.
- [x] Shared behavior is imported from core modules, not copied between pages for extracted core API/session/router/modal/toast/clipboard/job helpers.
- [x] Shared visual patterns are reusable components without changing current appearance.
- [x] Current routes and in-memory detail behavior remain compatible until a separate router/history task is approved.
- [x] All API calls go through the central API layer.
- [ ] All page timers, pollers, listeners, modals, and drawers clean up on navigation.
- [x] DB-2 behavior remains unchanged by intent; static and module smoke validation passed.
- [x] WordPress credential rotation and job diagnostics remain public-safe by preserving the existing shared renderers.
- [x] No new framework, npm dependency, bundler, CSS redesign, or backend behavior is introduced without product-owner approval.
- [x] Every implemented phase is independently revertible as one frontend-only modularization commit.
- [x] Every implemented phase has appropriate frontend-only syntax/static/module validation for moved responsibility.
- [x] `node --check`, `git diff --check`, focused frontend tests, modular baseline checks, and browser/manual smoke checks pass or are recorded Not Testable before each frontend-only implementation commit.

Expected files:

- All finalized frontend modules
- `web/app.js`
- `web/index.html`
- `web/style.css`
- Tests and documentation updated by phase
- `planning/frontend-modularization-checklist.md`

Expected risks:

- Final cleanup can reveal missed global dependencies.
- Acceptance can drift if checklist is not maintained after every session.

Acceptance criteria:

- All final acceptance checklist items are completed.
- The architecture document and implementation state match.
- The UI remains behavior-compatible unless separately approved changes were made.

Rollback notes:

- Roll back by last completed phase or page extraction commit.
- Never delete completed checklist history during rollback.
