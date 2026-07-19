# Changelog

All notable changes to ContainerCP are documented here.

Format: date | commit | summary

---

## 2026-07-19 | `PENDING` | Update — Install SQLite build dependency during updates

**Summary:** Updated `scripts/update.sh` so git-based updates install required build dependencies, including `libsqlite3-dev`, before running CMake. This prevents older installations from failing configuration after SQLite support is pulled.

**Files changed:** `scripts/update.sh`, `CHANGELOG.md`

**User-visible behavior:** Running `./scripts/update.sh` on an existing Debian installation now installs the SQLite development package required by CMake before building ContainerCP.

**Validation:** `bash -n scripts/update.sh` passed.

**Known risks:** Requires update execution with sufficient privileges to run `apt-get`; non-APT systems still need manual dependency installation.

---

## 2026-07-18 | `c9c09b3` | Phase 11 — Final SQLite production validation report

**Summary:** Completed the final Phase 11 production validation report for SQLite activation review fixes. The report records commit hashes, focused-test evidence, full-suite evidence, clean rebuild evidence, git status evidence, CI status, and production readiness conclusions for P11-R1 through P11-R7.

**Files changed:** `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This closes the production review evidence package for Phase 11 SQLite activation hardening.

**Validation:** Documentation-only update based on validated HEAD `3e65609`. Latest Build and Test CI run `29660270878` passed for `3e65609`; `git status --short` produced no output before editing the final report.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `e2b9e90` | P11-R7 — Add end-to-end SQLite production upgrade test

**Summary:** Added a focused production upgrade integration test covering TXT fixture storage, manual SQLite migration, verification, archive creation, activation-state creation, startup validation, runtime read, runtime write, restart, post-restart validation, and no TXT fallback files in SQLite runtime storage.

**Files changed:** `tests/test_migrate_sqlite.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This adds coverage proving the approved upgrade path can migrate legacy TXT data to SQLite, activate through the production startup gate, accept runtime writes, persist them across restart, and avoid silently writing TXT fallback files.

**Validation:** Focused P11-R7 test passed (`1` case, `92` assertions). Full suite passed (`666` cases, `4491` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` completed with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`; `containercpd` compilation was continued after the tool timeout with `cmake --build build2 --target containercpd -- -j1` and completed successfully.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `e18a805` | P11-R6 — Complete SQLite filesystem security validation

**Summary:** Added SQLite activation startup security checks for storage directories, database files, activation-state files, and activation archive paths. Startup now rejects unsafe ownership, group/world-writable permissions, symlinked archive paths, non-regular activation-state paths, and unexpected filesystem objects before opening SQLite.

**Files changed:** `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** SQLite activation fails closed when startup inputs are exposed through unsafe filesystem permissions, wrong ownership, symlinks, or unsupported object types. Startup does not fall back to TXT storage on these security failures.

**Validation:** Focused P11-R6 tests passed (`6` cases, `144` assertions). Affected P11-R3 missing-archive regression test passed (`1` case, `7` assertions). Full suite passed (`665` cases, `4399` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings. Ownership mismatch behavior is implemented but not directly exercised in local tests because changing test-file ownership is not safe or portable in the normal developer workflow.

---

## 2026-07-18 | `51429f0` | P11-R5 — Complete SQLite production failure handling tests

**Summary:** Added focused production failure handling tests for SQLite startup validation. The new tests cover missing activation state, corrupted activation state, archive validation failure, migration state mismatch, unsupported schema version, and SQLite open failure while asserting fail-closed startup, no TXT fallback files, and unchanged SQLite marker state or unchanged corrupt database file.

**Files changed:** `tests/test_sqlite_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This adds validation coverage proving SQLite activation failures stop startup without silently falling back to TXT storage or mutating existing SQLite state.

**Validation:** Focused P11-R5 tests passed (`6` cases, `176` assertions). Full suite passed (`659` cases, `4255` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `7164301` | CI — Stabilize SQLite reopen verification test

**Summary:** Updated the importer verification test that checks wrong SQLite reopen behavior to use an explicit empty SQLite backend under the test fixture directory instead of `/nonexistent_storage_dir`. The test now deterministically verifies that reopen comparison fails on mismatched SQLite data across both CI and root-local environments.

**Files changed:** `tests/test_importer.cpp`, `CHANGELOG.md`

**User-visible behavior:** No product behavior change. This stabilizes CI coverage for SQLite migration verification after the fail-closed startup validation changes.

**Validation:** Focused `Storage failure detected on corrupt db reopen` test passed (`1` case, `12` assertions). Full suite passed (`653` cases, `4079` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` completed with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`; `containercpd` compilation was continued after the tool timeout with `cmake --build build2 --target containercpd -- -j1` and completed successfully.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `815e1cc` | P11-R4 — Propagate SQLite write failures

**Summary:** Replaced ignored SQLite `try_save_*` results with controlled exceptions from public SQLite-backed save methods, so failed writes are visible to callers instead of being silently ignored. Added focused parent, child, and mail configuration write-failure coverage and updated existing foreign-key rollback tests to assert caller-visible failures.

**Files changed:** `libs/storage/SQLiteStorage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** SQLite-backed writes now fail closed with `SQLite save failed for <resource>` when the database rejects or cannot complete a write. SQLite mode no longer silently drops failed writes or falls back to TXT files for those write paths.

**Validation:** Focused P11-R4 tests passed (`3` cases, `22` assertions). Affected FK and rollback reruns passed. Full suite passed (`653` cases, `4073` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `c727106` | P11-R3 — Validate SQLite activation-state consistency

**Summary:** Extended SQLite startup validation so activation state must reference a real completed migration archive. Startup now validates the migration ID, state schema version, database path, normalized archive path, archive integrity, and archive manifest fields before opening SQLite.

**Files changed:** `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** If `storage.backend=sqlite` is configured and `storage-state.json` points to a missing, invalid, relocated, or inconsistent migration archive, daemon startup fails closed instead of accepting the activation state.

**Validation:** Focused P11-R3 tests passed (`7` cases, `46` assertions). Full suite passed (`650` cases, `4043` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `8e18db1` | P11-R2 — Strict SQLite activation-state parsing

**Summary:** Replaced SQLite activation-state substring extraction with a strict typed parser. Startup now rejects malformed JSON, duplicate keys, missing required keys, unknown keys, wrong value types, invalid strings, and invalid enum values before continuing SQLite activation.

**Files changed:** `libs/storage/Storage.cpp`, `libs/storage/Storage.h`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** If `storage.backend=sqlite` is configured and `storage-state.json` does not match the approved activation-state schema, daemon startup fails closed with an activation-state validation error instead of accepting ambiguous or malformed content.

**Validation:** Focused P11-R2 tests passed (`8` cases, `44` assertions). Full suite passed (`643` cases, `3969` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully with `cmake --build build2 --clean-first --target containercp_tests containercpd -- -j1`.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, sign-compare warnings, and legacy misleading indentation warnings. Archive existence and migration/schema consistency checks remain scoped to P11-R3.

---

## 2026-07-18 | `c557601` | P11-R1 — Remove automatic SQLite schema migration from startup

**Summary:** Removed automatic schema migration from `Storage` SQLite startup. Startup now opens the configured database and validates existing schema metadata/version instead of running `MigrationEngine::migrate()`.

**Files changed:** `libs/storage/Storage.cpp`, `libs/storage/Storage.h`, `tests/test_sqlite_storage.cpp`, `tests/test_storage.cpp`, `docs/development/phase11-production-review-fixes.md`, `CHANGELOG.md`

**User-visible behavior:** `containercpd` startup no longer creates or updates SQLite schema. If SQLite is configured with missing or unsupported schema metadata, startup fails closed instead of migrating automatically.

**Validation:** Focused P11-R1 tests passed (`2` cases, `19` assertions). Full suite passed (`635` cases, `3925` assertions). CTest passed (`1/1`). Clean rebuild of `containercp_tests` and `containercpd` completed successfully.

**Known risks:** Existing clean-build warning debt remains, including OpenSSL/c-ares deprecations, unused variables/parameters, member reorder warnings, and legacy misleading indentation warnings.

---

## 2026-07-18 | `545a4ce` | P11-21 — Phase 11 final validation

**Summary:** Completed final Phase 11 validation for SQLite activation. Clean rebuild, CTest, full doctest, and worktree status checks were run against validated code HEAD `f9036aa`.

**Files changed:** `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No functional behavior change; this closes Phase 11 validation records.

**Validation:** Clean rebuild of `containercp_tests` and `containercpd` succeeded. CTest passed (`1/1`). Full doctest suite passed (`633` cases, `3822` assertions). Worktree was clean after validation.

**Known risks:** Clean rebuild still emits existing compiler warning debt, including OpenSSL/c-ares deprecations, unused variables/parameters, `ServiceRegistry` member reorder warnings, and misleading indentation warnings in legacy/archive/test code.

---

## 2026-07-18 | `046e400` | P11-20 — SQLite activation production runbook

**Summary:** Added a production runbook for SQLite activation covering prerequisites, migration, activation, validation, failure handling, rollback, and operator safety warnings.

**Files changed:** `docs/sqlite-activation-runbook.md`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** Operators now have a documented procedure for safely migrating to SQLite and rolling back to legacy TXT if activation fails.

**Validation:** Documentation-only change; full suite from P11-19 passed (`633` cases, `3822` assertions), and `containercpd` target built successfully before this runbook update.

**Known risks:** Runbook assumes legacy TXT files remain present for configuration-only rollback.

---

## 2026-07-18 | `173db12` | P11-19 — SQLite startup integration test

**Summary:** Added end-to-end integration coverage proving a migrated SQLite database opens through the production startup validation gate and exposes all checked runtime snapshots.

**Files changed:** `tests/test_migrate_sqlite.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No functional behavior change; this validates that successful migration output can be activated by the daemon startup path.

**Validation:** Focused P11-19 test passed (`1` case, `19` assertions). Full suite passed (`633` cases, `3822` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-19. SQLite startup observability logs make migration/storage tests more verbose.

---

## 2026-07-18 | `d824ec2` | P11-18 — SQLite site_id=0 sentinel validation

**Summary:** Added runtime validation that approved `site_id=0` sentinel records survive SQLite writes and validated restart.

**Files changed:** `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No behavior change; this is validation coverage proving approved orphan/admin/external sentinel records remain valid after SQLite activation.

**Validation:** Focused P11-18 test passed (`1` case, `24` assertions). Full suite passed (`632` cases, `3803` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-18. SQLite startup observability logs make storage tests more verbose.

---

## 2026-07-18 | `d8fd466` | P11-17 — SQLite activation-state security

**Summary:** Hardened SQLite startup validation by rejecting symlinked or non-regular `storage-state.json` files before reading activation state JSON.

**Files changed:** `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** If `storage.backend=sqlite` is configured and `storage-state.json` is a symlink or non-regular file, startup now fails with a descriptive error instead of following the path.

**Validation:** Focused P11-17 test passed (`1` case, `4` assertions). Full suite passed (`631` cases, `3779` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-17. SQLite startup observability logs make storage tests more verbose.

---

## 2026-07-18 | `615e8b3` | P11-16 — SQLite migration operator workflow

**Summary:** Added explicit operator next steps to successful SQLite migration diagnostics, including the required config change, daemon restart validation, and legacy archive retention guidance.

**Files changed:** `libs/storage/MigrationOrchestrator.cpp`, `tests/test_migrate_sqlite.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** `containercp storage migrate-to-sqlite` success output now tells operators how to activate SQLite safely after migration.

**Validation:** Focused P11-16 test passed (`1` case, `6` assertions). Full suite passed (`630` cases, `3775` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-16. SQLite startup observability logs make migration tests more verbose.

---

## 2026-07-18 | `526e410` | P11-15 — SQLite startup observability

**Summary:** Added `STORAGE` category logs for SQLite backend startup. Startup now logs backend selection, validation success, readiness, and fail-closed exception reasons.

**Files changed:** `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** Operators now see clear SQLite startup logs, including descriptive failure reasons when activation state or validation checks fail.

**Validation:** Focused P11-15 tests passed (`2` cases, `8` assertions). Full suite passed (`629` cases, `3769` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-15. Test output is more verbose because SQLite startup paths now emit observability logs.

---

## 2026-07-18 | `e855ff6` | P11-14 — SQLite failure handling

**Summary:** Added fail-closed startup handling for symlinked SQLite database paths. Startup validation now inspects the configured database path with `lstat()` and rejects symlinks before opening SQLite.

**Files changed:** `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** If `storage.backend=sqlite` points to a symlinked `containercp.db`, daemon startup now fails with a descriptive error instead of following the link.

**Validation:** Focused P11-14 test passed (`1` case, `4` assertions). Full suite passed (`627` cases, `3761` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-14.

---

## 2026-07-18 | `40f703e` | P11-13 — SQLite restart persistence

**Summary:** Added restart persistence validation for SQLite activation. The test writes all runtime resource categories, closes storage, writes activation state, reopens with startup validation enabled, and verifies all checked snapshots still load.

**Files changed:** `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No functional behavior change; this is validation coverage proving SQLite-backed data survives restart and production startup validation.

**Validation:** Focused P11-13 test passed (`1` case, `37` assertions). Full suite passed (`626` cases, `3757` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-13.

---

## 2026-07-18 | `e954568` | P11-12 — SQLite read-path validation

**Summary:** Added focused validation that SQLite-mode runtime reads use SQLite only and ignore legacy TXT files, including poisoned TXT content left beside `containercp.db`.

**Files changed:** `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No functional behavior change; this is validation coverage for SQLite backend reads. It confirms legacy TXT files cannot affect runtime reads after SQLite activation.

**Validation:** Focused P11-12 tests passed (`2` cases, `51` assertions). Full suite passed (`625` cases, `3720` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-12.

---

## 2026-07-18 | `f3dd14e` | P11-11 — SQLite write-path validation

**Summary:** Added focused validation for SQLite-mode write behavior after runtime repository wiring. The tests prove replacement writes commit to SQLite, omitted records are removed, legacy TXT files are not created, and failed child-table writes roll back without losing the last committed state.

**Files changed:** `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** No functional behavior change; this is validation coverage for SQLite backend writes. It reduces risk that SQLite-mode runtime mutations silently fall back to TXT or partially apply failed child writes.

**Validation:** Focused P11-11 tests passed (`2` cases, `29` assertions). Full suite passed (`623` cases, `3669` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-11.

---

## 2026-07-18 | `7a616a5` | P11-10 — Runtime repository SQLite wiring

**Summary:** Completed SQLite runtime routing for the two remaining TXT-only resources, `backups` and `auth_users`, and verified the full 17-resource runtime set through SQLite snapshots.

**Files changed:** `libs/storage/SQLiteStorage.h`, `libs/storage/SQLiteStorage.cpp`, `libs/storage/Storage.h`, `libs/storage/Storage.cpp`, `tests/test_sqlite_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** When SQLite is the configured backend, backup metadata and auth users now persist to `containercp.db` instead of legacy TXT files. The runtime storage abstraction no longer leaves `backups` or `auth_users` on the legacy path in SQLite mode.

**Validation:** Focused P11-10 tests passed (`2` cases, `62` assertions). Full suite passed (`621` cases, `3640` assertions). `containercpd` target builds successfully.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to P11-10.

---

## 2026-07-18 | `23bfe33` | P11-09 — No silent SQLite fallback

**Summary:** SQLite backend selection is now loaded during normal daemon startup before `ServiceRegistry` constructs `Storage`, so `storage.backend=sqlite` cannot be ignored and silently fall back to legacy TXT storage.

**Files changed:** `app/containercpd/main.cpp`, `tests/test_storage.cpp`, `docs/development/phase11-sqlite-activation-checklist.md`, `planning/project-status.md`, `planning/backlog.md`, `CHANGELOG.md`

**User-visible behavior:** If SQLite is configured and startup validation fails, `containercpd` exits non-zero before starting REST API or Web UI listeners. Operators see the storage validation error instead of a daemon that starts on legacy storage.

**Validation:** Focused P11 tests passed (`7` cases, `14` assertions). Full suite passed (`620` cases, `3577` assertions). Daemon namespace validation with missing activation state exited `134`, was not alive after 2 seconds, and had no listeners on the selected API/UI ports.

**Known risks:** Build output still contains pre-existing compiler warnings unrelated to this change; no new warning was introduced by P11-09.

---

## v0.6.0 — 2026-07-16

**Stable release.** See `docs/release-notes-v0.6.0.md` for full release notes.

**Validation:** All 94 items across 5 stages passed. 242 deterministic tests.
257 full suite. Zero compiler warnings. Browser verification complete.

**RC1:** `v0.6.0-rc1` (2026-07-16) — validated and promoted to stable.

---

## v0.6.0-rc1 — 2026-07-16

**Release scope:** DNS and Mail release. First release candidate for v0.6.

### Mail (ARCH-006)
- MailDomain resource with 4 modes (Disabled, LocalPrimary, ExternalRelay, SplitM365)
- Mailbox CRUD with SHA-512-CRYPT password hashing
- Mail alias support with domain-level routing and virtual_alias_maps
- Docker mail stack: Postfix, Dovecot, Redis, Rspamd (stock images)
- DKIM key generation via OpenSSL, stored in MailDomain, Rspamd milter signing
- TLS configuration for Postfix + Dovecot with CertificateStore paths
- External relay mode: per-domain transport maps
- Split-M365 mode: local mailboxes + catch-all relay with LMTP routing
- Runtime synchronization: 11 mail CRUD handlers trigger config regeneration
- Mail health reporting (Postfix/Dovecot/Redis status via HealthRegistry)
- Module lifecycle (activate/deactivate/status via MailModuleState)
- Mail reload (`POST /api/mail/reload`) and recover (`POST /api/mail/recover`)
- SMTP server fixes: bookworm base image, chroot, socket cleanup, DNS resolution
- Smarthost API (`GET/POST /api/mail/smarthost`) with TLS + SASL support
- DKIM signing fix for PHP Mail: `allow_username_mismatch=true`
- PHP Mail enable/disable per site

### DNS Diagnostics (ARCH-007)
- DnsCheckService using c-ares library (A, AAAA, MX, TXT, CNAME, NS, SOA, CAA)
- 60s in-memory cache with refresh=1 bypass and concurrent access protection
- `GET /api/domains/<domain>/dns-check` with type filtering and error semantics
- Domain List with progressive DNS/Runtime/Health column loading
- Domain Detail with 5 tabs: Overview, DNS Records, Mail, Security, Health
- Configured vs Published comparison for A, AAAA, MX, SPF, DKIM, DMARC, CAA, MTA-STS, TLS-RPT
- SPF analysis (RFC 7208) with SpfAnalyzer — ip4, ip6, a, mx, include, redirect, all
- DMARC Wizard with 3 policies (Monitor=none, Quarantine, Reject)
- Evidence/Why panels with expected/actual/reason/fix for all record types
- Context-aware Health Score (9 weighted checks, grade boundaries, mail/no-mail context)
- Admin-panel virtual system Site and Domain (site_id=0) with capability fields
- 16 commits, 257 tests, zero compiler warnings

### SSL and Security
- ACME HTTP-01 with Let's Encrypt (staging + production environments)
- CertificateStore with versioned metadata and auto-renewal
- SSL REST API (issue, renew, enable, disable, HTTP→HTTPS redirect)
- SSL Web UI page with status overview
- Admin-panel SSL (site_id=0) with certificate management

### Tests and Reliability
- Deterministic test suite: 242 tests
- Live-DNS integration suite: 15 tests (tagged [integration])
- API handler tests through Router dispatch with FakeDnsCheckService
- JSON syntax validation in regression tests
- CTest suite separation (deterministic vs integration)
- Thread-safe concurrent cache access tests

### Important Fixes
- Site ID 0 foreign-key collision in MailDomain lookup (frontend)
- JSON generation bug: missing closing quote in DomainViewService
- Stale loader race condition in HealthCache
- ID-0 collision in mail association (domain_id=0 matching)
- Admin-panel site_id=0 cannot be removed via generic endpoints
- SPF nesting bug in Health scoring (DKIM/DMARC outside rootDns block)
- DMARC parser: `indexOf('p=none')` matched `sp=none`

---

## ARCH-007 — DNS Diagnostic Center (2026-07-16)

**Epic status:** COMPLETED

**Commits:** `8f7a249..72a2333` (16 commits)

**Summary:** Read-only DNS diagnostic center providing live DNS resolution,
Configured vs Published comparison, health scoring, and security recommendations.

**Major milestones:**

| Phase | Description |
|-------|-------------|
| Phase 0 | Existing code verification, production inspection |
| Phase 1 | DnsCheckService with c-ares backend, record parsing, caching |
| Phase 2 | REST API endpoint, DomainViewService mail fields |
| Phase 3 | Domain list with DNS/Mail/Runtime/Health columns |
| Phase 4 | Domain detail with 5-tab layout (Overview, DNS Records, Mail, Security, Health) |
| Phase 5 | Configured vs Published comparison + copy buttons |
| Phase 6 | Mail tab with conditional MailDomain display |
| Phase 7 | Security tab, DMARC Wizard, evidence/why panels |
| Phase 8 | Health scoring engine (9 check types, weighted, context-aware) |
| Phase 9 | Unit tests, API handler tests, frontend verification |

**Key components:**
- `libs/dns/DnsCheckService.h/.cpp` — c-ares DNS resolution
- `libs/dns/SpfAnalyzer.h/.cpp` — RFC 7208 SPF analysis
- `libs/dns/DnsCheckHandler.h/.cpp` — production API handler
- `libs/api/SitesViewService.h/.cpp` — enriched sites with admin panel
- `libs/domain/DomainViewService.h/.cpp` — enriched domains with admin panel
- `libs/network/NetworkService.h/.cpp` — public IP detection
- `web/app.js` — full GUI (domain list, detail, tabs, evidence panels)
- `web/js/utils.js` — health scoring, formatters, comparison helpers
- `web/js/cache.js` — DNS/Health caching with TTL

**Admin panel (site_id=0):** Virtual system domain and site synthesized at the
view layer. Protected from deletion. Runtime N/A. SSL applicable.

---

## 2026-07-15 | `db07c1a` | Fix DKIM signing for PHP Mail — username mismatch

**Root cause:** Rspamd's `dkim_signing` module defaults to `allow_username_mismatch=false`.
When PHP Mail sends via msmtp, the SASL username (`site-11@php.containercp.internal`)
differs from the `From:` header (`wordpress@unity.softico.ua`), causing the module
to skip signing. SnappyMail worked because its SASL username matches the `From:` address.

**Fix:**
- Added `allow_username_mismatch = true` to generated `dkim_signing.conf`
- Removed all experimental patches (Docker image patch, Lua wrappers, settings.conf overrides)
- Stock Rspamd image restored — no modifications to `dkim_signing.lua`

**update.sh improved:**
- Mail stack Docker images (Rspamd, Dovecot, Postfix, SnappyMail) now rebuilt on each update
- Prevents stale images after Dockerfile changes

**Changed files:** `libs/mail/providers/DockerMailProvider.cpp`, `docker/mail/Dockerfile.rspamd`, `scripts/update.sh`

---

## 2025-07-09 | Phase 1–5: Runtime management

- Runtime subsystem with `RuntimeActionExecutor`, `ServiceRole`, `CommandExecutor`
- Site Details page redesigned as Website Management Center with Runtime card
- Runtime card shows Frontend/PHP/Database/Redis status + restart actions
- Runtime architecture refactoring: `ServiceRole` abstraction, `ContainerStatus` moved to executor
- Phase 3 fix: restart-all semantics, restart-db/redis actions added
- Phase 5 cleanup: moved container status inspection into `RuntimeActionExecutor`
- Phase 4: Sites UI restart actions (⚡ dropdown), Phase 5: moved to Site Details

See `docs/changelog/runtime-phases.md` for detailed entries with commit hashes,
file changes, validation results, and known risks.

---

## 2025-07-08 | SSL & HTTPS subsystem

- ACME HTTP-01 challenge via Web UI (staging + production)
- Bootstrap simplified (removed SSL step)
- Admin Panel HTTPS on port 443
- Let's Encrypt integration with auto-renewal
- `CertificateStore`, `RenewalScheduler`, `PemCertificateProvider`
- HTTPS status display in Sites Runtime card (consumes `CertificateStore`)

See `docs/changelog/ssl-subsystem.md` for detailed per-commit entries.

---

## 2025-07-08 | RC2 — Stability & Production Foundation

- Daemon architecture, REST API hardening
- Deployment scripts, update mechanism
- Port management refactoring
- Bug fixes for login, site removal, and rollback

See `docs/changelog/rc2-stability.md` for detailed entries.

---

## 2025-07-08 and earlier | Earlier development

- Multi-site Docker networking (ARCH-004)
- Web UI v0.5, PHP hosting, profiles
- CLI tooling, template engine
- Sprint reviews and infrastructure setup

See `docs/changelog/early-development.md` for detailed entries.

---

### Risks (current)

- Existing sites with host-port allocation retain old compose template
- Fresh site creation uses new template (always overwritten on disk)
- Deprecated PortManager not yet removed — cleanup planned
- `RuntimeActionExecutor` requires Docker Compose v2+
- Async jobs cancelled/marked failed if daemon shuts down during execution

---

## 2026-07-11 | Mail module hardening

- Network isolation: LMTP port 24 removed from host, ports bound to 127.0.0.1
- LMTP via Docker DNS (`containercp-mail-dovecot:24`) instead of `127.0.0.1:24`
- Router consolidation: 6 prefix handlers → 2 dispatchers, no 404 fallthrough
- Transactional `apply_config()`: generate → `postfix check` → reload → rollback
- Self-signed TLS cert auto-generated on fresh install (`ensure_certificate()`)
- Certificate status reported in health endpoint (valid/self-signed/expired/missing)
- Alias self-loop detection, `postmap -q` validation before apply
- Process-level health checks: `postfix status`, `doveadm who`, `redis-cli ping`
- Health status model: ok / degraded / error
- E2E test script: `scripts/test-mail-routing.sh`
- All aliases now written to Postfix `virtual_alias_maps` (was `(void)aliases;`)
- Port publishing fixed: Postfix 25/465/587, Dovecot 143/993 exposed on host

---

## 2026-07-11 | SMTP + DNS + Smarthost

- SMTP server fixes: Postfix master starts reliably (base image → debian:bookworm,
  stale socket cleanup in entrypoint, virtual_mailboxes mount, empty map files,
   Rspamd milter temporarily disabled, later re-enabled for DKIM signing)
- Postfix config: compatibility_level, mynetworks, smtpd_relay_restrictions,
  smtp_host_lookup, maillog_file (direct file logging, no syslog dependency)
- Docker DNS fix: resolv.conf with Google DNS, chroot jail copy, `dns: 8.8.8.8`
- Smarthost API: `GET /api/mail/smarthost`, `POST /api/mail/smarthost`
  ```json
  {"enabled":true,"host":"smtp.gmail.com","port":587,
   "username":"user@gmail.com","password":"app-password"}
  ```
- DKIM DNS record format (add TXT to your DNS provider):
  ```
  Type:  TXT
  Name:  dkim._domainkey.<your-domain>
  Value: v=DKIM1; k=rsa; p=<your-public-key>
  ```
- Direct MX delivery verified: admin@maillab.softi.co → powern76@gmail.com
  (SPF: PASS, DMARC: PASS, TLS: AES_256_GCM_SHA384)

---

## 2026-07-13 | Existing site upgrade path — trusted proxy + mod_rewrite

- `VestaSiteImporter::upgrade_site()` — upgrades existing WordPress sites:
  - Checks/fixes Apache mod_rewrite in 00-load-modules.conf
  - Adds trusted proxy block to wp-config.php (BEGIN CONTAINERCP TRUSTED PROXY)
  - PHP syntax check with backup/restore on failure
- CLI: `--upgrade` flag — runs without --backup
- DaemonApp: handles --upgrade mode
- All existing sites upgraded on production (test-gui-*, testssl, unity)

## 2026-07-13 | Trusted proxy HTTPS detection + Apache mod_rewrite

- Trusted proxy block added to wp-config.php during SQL import:
  - Reads `X-Forwarded-Proto` header, sets `$_SERVER['HTTPS'] = 'on'`
  - Only applies to requests through central nginx proxy
  - Idempotent: `// BEGIN CONTAINERCP TRUSTED PROXY` / `// END CONTAINERCP TRUSTED PROXY`
- Apache mod_rewrite: `LoadModule rewrite_module` in generated 00-load-modules.conf
- Nginx HTTPS vhost sends: `X-Forwarded-Proto https`, `X-Forwarded-Port 443`, `X-Forwarded-Ssl on`
- Fixes: ERR_TOO_MANY_REDIRECTS, 404 on pretty permalinks

## 2026-07-13 | Declarative proxy sync — orphan cleanup, HTTPS generation, validation

- `NginxProxyProvider::sync_all_proxies()` — full declarative sync:
  - Removes orphan .conf files (no proxy entry → file deleted)
  - Generates correct HTTP/HTTPS config based on CertificateStore
  - Validates each config with nginx -t
  - Checks upstream container existence
- `sync_all_https_configs()` now delegates to `sync_all_proxies()`
- Fix: unity.softico.ua now has HTTPS (301) after sync
- Fix: orphan proxy configs auto-removed on startup

## 2026-07-13 | ContainerCP PHP runtime with MySQL extensions

- New Docker image: `ghcr.io/powern/containercp-php:8.4` (php:8.4-fpm + mysqli + pdo_mysql)
- Parameterized Dockerfile: `docker/php/Dockerfile` with `ARG PHP_VERSION`
- ServiceRegistry: legacy PHP images auto-migrated to ContainerCP image on restart
- PHP container preflight: `import_sql()` checks mysqli/pdo_mysql before destructive ops
- Error if missing: "Target PHP runtime missing required MySQL extensions"
- Example site (site-11) upgraded: `docker compose stop → rm → compose up -d php`
- `scripts/update.sh` now builds PHP image during update
- update.sh builds `ghcr.io/powern/containercp-php:8.4` if Dockerfile present

## 2026-07-12 | VestaSiteImporter — Stage 2: web file import

- `import_files()` — extract, safety-copy, rsync, ownership fix, container restart
- Safety-copy rollback: restores original public/ on failure
- Path traversal protection via `realpath` prefix check
- Hidden files preserved (`.htaccess`, `.user.ini`, `.well-known`)
- Web UI: Import files button after Stage 1
- API: `POST /api/migration/vesta/import-files`
- CLI: `--import-files` flag

## 2026-07-12 | VestaSiteImporter — MyVestaCP migration tool (Phase 1-2)

- New CLI: `migrate-vesta-site --backup --domain --owner [--dry-run]`
- `VestaSiteImporter` — read-only inspection of MyVestaCP backup archives
- Secure tar listing, domain extraction, web root detection (public_html/public/htdocs)
- WordPress detection via wp-config.php parsing (regex, single+double quotes)
- SQL dump discovery in archive
- Dry-run mode: shows manifest without system changes
- 7 unit tests for inspect/parsing/path validation

## 2026-07-12 | SnappyMail webmail integration

- New container: `containercp-mail-snappymail` (alpine + nginx + php84)
- Image: `ghcr.io/containercp/mail-snappymail:latest`
- Accessible at `https://<server-hostname>/webmail/` via nginx proxy
- Connects to Dovecot IMAP and Postfix SMTP (STARTTLS auth)
- New `webmail_upstream` param in `ProxyConfigBuilder::Params`
- Web UI sidebar: Webmail link added between Mail and Proxy
- Health check: snappymail container status reported in /api/health

---

## 2026-07-12 | Rspamd DKIM signing (replaced OpenDKIM)

- OpenDKIM milter was not signing — replaced with Rspamd milter proxy
- `POST /api/mail/domains/<id>/dkim/generate` generates 2048-bit RSA key
- DKIM DNS record stored in `MailDomain::dkim_public_key_dns`, returned via API
- Rspamd `dkim_signing` module signs outbound mail via milter on port 11332
- Postfix milter: `smtpd_milters = inet:containercp-mail-rspamd:11332`
- Config: `worker-proxy.inc`, `worker-normal.inc`, `dkim_signing.conf`, `logging.inc`
- Key permissions: 644 (Rspamd runs as `_rspamd` user in container)
- Bug fixes: `use_esld=false` (eSLD mismatch broke domain key lookup),
  `worker-normal.inc` not generated, `worker-proxy.inc` not mounted in compose
- Docker images: `ghcr.io/containercp/mail-rspamd:latest` (debian:trixie + rspamd)

## 2026-07-18 | P11-03 — Explicit migration command

- New CLI: `containercp storage migrate-to-sqlite` with `--source`, `--database`, `--archive-root`, `--source-version`, `--target-version`, `--confirm`
- Daemon handler validates flags, checks paths, generates UUID v4 migration ID
- Command does NOT start HTTP service (handler-only, no API dependency)
- Requires `--confirm` flag, exits non-zero on failure
- 2 new unit tests (607 total, 3541 assertions)
- Files: `libs/cli/CommandDispatcher.cpp`, `libs/daemon/DaemonApp.cpp`, `tests/test_daemon.cpp`

## 2026-07-18 | P11-02 — Backend selection contract

- `Config` gains `storage_backend()` getter, `set_storage_backend()`, `load_storage_backend()`
- Backend source: `CONTAINERCP_STORAGE_BACKEND` env var → `/srv/containercp/database/storage_backend` file → `"legacy"` default
- `ServiceRegistry` accepts `StorageOptions` from config, validates at startup
- Unknown backend value → startup failure (no silent Txt fallback)
- 5 new unit tests (605 total, 3526 assertions)
- Files: `libs/config/Config.h/cpp`, `libs/core/ServiceRegistry.h/cpp`, `tests/test_storage.cpp`
