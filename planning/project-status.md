# ContainerCP Project Status Tracker

> This file is the single source of truth for project completion status.
> It tracks every task across all sprints/versions and cross-references
> the actual codebase.
>
> Legend: ✅ Implemented  🔄 In Progress  ⬜ Planned  ❌ Blocked  ➖ Removed/Deprecated

---

## Version 0.1 — Core Foundation

| Task | Status | Files |
|------|--------|-------|
| C++20 project with CMake/Ninja | ✅ | CMakeLists.txt |
| Core: Application, ServiceRegistry, Config, Resource | ✅ | `libs/core/` |
| Pipe-delimited Storage | ✅ | `libs/storage/` |
| Logger singleton | ✅ | `libs/logger/` |
| CLI thin client + daemon | ✅ | `libs/cli/`, `libs/daemon/` |
| UNIX socket communication | ✅ | `libs/daemon/UnixSocketClient.cpp` |

## Version 0.2 — Hosting Engine

| Task | Status | Files |
|------|--------|-------|
| Docker Compose generation | ✅ | `libs/docker/ComposeGenerator.cpp` |
| Env generation with secrets | ✅ | `libs/docker/EnvGenerator.cpp` |
| Runtime abstraction (DockerRuntime) | ✅ | `libs/runtime/DockerRuntime.cpp` |
| HostingProvider (DockerComposeProvider) | ✅ | `libs/provider/DockerComposeProvider.cpp` |
| Reverse proxy config (NginxProxyProvider) | ✅ | `libs/proxy/NginxProxyProvider.cpp` |
| SSL placeholder (LetsEncryptProvider) | ✅ | `libs/ssl/LetsEncryptProvider.cpp` |
| Site directory layout | ✅ | `libs/filesystem/SiteLayout.cpp` |
| Compose hardening (health, restart, limits) | ✅ | `libs/docker/ComposeGenerator.cpp` |

## Version 0.3 — Platform Services

| Task | Status | Files |
|------|--------|-------|
| Daemon architecture (containercpd + containercp) | ✅ | `app/containercpd/main.cpp`, `app/containercp/main.cpp` |
| REST API server | ✅ | `libs/api/ApiServer.cpp` |
| Static file serving for Web UI | ✅ | `libs/api/WebServer.cpp` |
| Auth middleware (AllowAll) | ✅ | `libs/api/AuthMiddleware.cpp` |
| Router with GET/POST | ✅ | `libs/api/Router.cpp` |
| JSON formatter | ✅ | `libs/api/JsonFormatter.cpp` |

## Version 0.4 — Infrastructure Resources

| Task | Status | Files |
|------|--------|-------|
| **CCP-1001**: User resource | ✅ | `libs/user/User.h/.cpp`, `UserManager.h/.cpp` |
| **CCP-1002**: Domain resource | ✅ | `libs/domain/Domain.h/.cpp`, `DomainManager.h/.cpp` |
| **CCP-1003**: PHP version abstraction | ✅ | `libs/php/PhpVersion.h/.cpp`, `PhpVersionManager` |
| **CCP-1004**: Database resource | ✅ | `libs/database/Database.h/.cpp`, `DatabaseManager.h/.cpp` |
| **CCP-1005**: Backup resource (initial) | ✅ | `libs/backup/Backup.h/.cpp`, `BackupManager.h/.cpp` |
| **CCP-1006**: SSL resource | ✅ | `libs/ssl/SslCertificate.h/.cpp`, `SslCertificateManager` |
| **CCP-1007**: Mail placeholder resource | ✅ | `libs/mail/MailDomain.h/.cpp`, `MailDomainManager.h/.cpp` |

## Version 0.5 — Web Administration

### Sprint 4 — Production Ready

| Task | Status | Files |
|------|--------|-------|
| **CCP-2001**: Shared utility module | ✅ | `libs/utils/PasswordGenerator.cpp`, `StringUtils.cpp` |
| **CCP-2002**: Domain validation | ✅ | `libs/utils/Validator.cpp` |
| **CCP-2003**: Username validation | ✅ | `libs/utils/Validator.cpp` |
| **CCP-2004**: Site remove command | ✅ | `libs/operations/SiteRemoveOperation.cpp` |
| **CCP-2005**: .env with DB credentials | ✅ | `libs/docker/EnvGenerator.cpp` |
| **CCP-2006**: Dry-run mode | ✅ | `libs/operations/SiteCreateOperation.cpp` |
| **CCP-2007**: Docker availability cache | ✅ | `libs/runtime/DockerRuntime.cpp` |
| **CCP-2008**: Recovery support (rollback) | ✅ | `libs/operations/SiteCreateOperation.cpp` |

### Sprint 5 — Tests and Reliability

| Task | Status | Files |
|------|--------|-------|
| **CCP-3001**: Test framework (doctest) | ✅ | `libs/doctest/doctest.h`, `tests/CMakeLists.txt` |
| **CCP-3002**: Validator tests | ✅ | `tests/test_validator.cpp` |
| **CCP-3003**: Manager & Storage tests | ✅ | `tests/test_managers.cpp`, `tests/test_storage.cpp` |
| **CCP-3004**: Filesystem rollback fix | ✅ | `libs/provider/DockerComposeProvider.cpp` |
| **CCP-3005**: --force for site remove | ✅ | `libs/operations/SiteRemoveOperation.cpp` |
| **CCP-3006**: Detailed validation messages | ✅ | `libs/utils/Validator.cpp` |

### Sprint 6 — Developer Access Layer

| Task | Status | Files |
|------|--------|-------|
| **CCP-4001**: AccessUser resource | ✅ | `libs/access/AccessUser.h/.cpp`, `AccessUserManager` |
| **CCP-4002**: AccessProvider abstraction | ✅ | `libs/access/AccessProvider.h` |
| **CCP-4003**: LocalSftpProvider placeholder | ✅ | `libs/access/LocalSftpProvider.h/.cpp` |
| **CCP-4004**: CLI access user commands | ✅ | `libs/daemon/DaemonApp.cpp` |
| **CCP-4005**: Access directory mapping | ✅ | `libs/access/AccessGrant.cpp` |
| **CCP-4006**: SFTP documentation | ✅ | `docs/SFTP-PROVIDER.md` |

### Sprint 7 — Admin Panel Phase 2

| Task | Status | Files |
|------|--------|-------|
| **CCP-5001**: GET /api/profiles | ✅ | `libs/api/ApiServer.cpp` |
| **CCP-5002**: GET /api/nodes | ✅ | `libs/api/ApiServer.cpp` |
| **CCP-5003**: GET /api/logs | ✅ | `libs/api/ApiServer.cpp` |
| **CCP-5005**: Site detail page | ✅ | `web/app.js` |
| **CCP-5006**: Create Site modal | ✅ | `web/app.js` |
| **CCP-5007**: Toast notification system | ✅ | `web/app.js` |
| **CCP-5008**: Improved tables + search | ✅ | `web/app.js` |
| **CCP-5009**: Global error handling | ✅ | `web/app.js` |

### API Layer

| Task | Status | Files |
|------|--------|-------|
| **API-001**: POST /api/sites/create | ✅ | `libs/api/ApiServer.cpp` |
| **API-002**: POST /api/sites/remove | ✅ | `libs/api/ApiServer.cpp` |
| **API-003**: POST /api/backups/create | ✅ | `libs/api/ApiServer.cpp` |
| **API-004/005**: GET /api/jobs | ✅ | `libs/api/ApiServer.cpp` |

### Job System

| Task | Status | Files |
|------|--------|-------|
| **JOB-001**: In-memory JobManager | ✅ | `libs/jobs/JobManager.h/.cpp` |

### Backup Subsystem

| Task | Status | Files |
|------|--------|-------|
| **BACKUP-001**: BackupProvider interface | ✅ | `libs/backup/BackupProvider.h` |
| **BACKUP-002**: TarBackupProvider | ✅ | `libs/backup/TarBackupProvider.h/.cpp` |
| **BACKUP-003**: Backup file management | ✅ | `/srv/containercp/backups/` |
| **BACKUP-004**: Backup resource update | ✅ | `struct Backup` |
| **BACKUP-005**: CLI commands | ✅ | backup create/restore/list/show/remove |
| **BACKUP-006**: REST API endpoint | ✅ | GET /api/backups |
| **BACKUP-007**: SiteRemove integration | ✅ | `libs/operations/SiteRemoveOperation.cpp` |
| **BACKUP-008**: Tests | ✅ | `tests/test_backup.cpp` |

### Architecture Changes

| Task | Status | Files |
|------|--------|-------|
| **ARCH-003**: Web UI Public Access (dual listener) | ✅ | `libs/api/WebServer.cpp` |
| **ARCH-004**: Docker Network Multi-Site | ✅ | Multiple files (see CHANGELOG) |
| Default backend → Apache2 | ✅ | `libs/core/ServiceRegistry.cpp` |
| Web UI backend selector (Apache2/Nginx) | ✅ | `web/app.js` |
| **ARCH-007**: DNS Diagnostic Center | ✅ | See summary below |

### Bugs Fixed

| Bug | Status | File |
|-----|--------|------|
| BUG-003: /api/version returns 404 | ✅ | Fixed |
| BUG-006: Site create rollback incomplete | ✅ | `SiteCreateOperation.cpp` |
| BUG-007: site remove --force broken | ✅ | `CommandDispatcher.cpp`, `DaemonApp.cpp` |
| BUG-008: docker-compose missing detection | ✅ | `DockerRuntime.cpp` |
| BUG-009: Rollback storage persistence | ✅ | `SiteCreateOperation.cpp` |
| BUG-010: docker-compose fallback | ✅ | `DockerRuntime.cpp` |
| BUG-011: Login 401 shows daemon error | ✅ | `WebServer.cpp`, `AuthService.cpp` |
| BUG-012: Missing nginx config on create | ✅ | `DockerComposeProvider.cpp` |
| BUG-013: Backup restore no feedback, site remove deletes backups | ✅ | `web/app.js`, `SiteRemoveOperation.cpp` |
| BUG-014: Multi-site port conflict | ✅ | ARCH-004 |

### RC2 — Stability & Production Foundation (validated on real Debian 13)

| Item | Status | Notes |
|------|--------|-------|
| install.sh | ✅ | `scripts/install.sh` |
| update.sh | ✅ | `scripts/update.sh` |
| systemd service | ✅ | `packaging/containercp.service` |
| Single instance PID lock | ✅ | `main.cpp` |
| Startup recovery (dirs, network, proxy) | ✅ | `main.cpp` |
| Deployment cleanup on failure (rollback) | ✅ | `SiteCreateOperation.cpp` |
| Category-based logging | ✅ | `Logger.h/.cpp` |
| Real-time deployment progress in GUI | ✅ | `web/app.js` + job polling |
| journald logging visibility | ✅ | `Logger.cpp` |
| Apache backend | ✅ | Default backend validated |
| Nginx backend | ✅ | Selectable via profiles |
| Multi-site Docker networking | ✅ | ARCH-004 validated |
| Central proxy recovery | ✅ | Survives daemon restart |
| Web UI operations | ✅ | Create, list, detail, backup |
| Updated docs | ✅ | README, INSTALL, CHANGELOG |
| Version bumped to v0.5.0-rc2 | ✅ | |

### v0.5.0 Release — superseded by v0.6.0-rc1

| Item | Status | Notes |
|------|--------|-------|
| 24-hour stability test (RC2) | ⏭️ | Deferred — superseded by v0.6 scope |
| v0.5.0 stable release | ⏭️ | Not released — v0.6.0-rc1 is the current release |
| Final validation (148 items) | ⏭️ | Merged into v0.6.0-rc1 validation scope |

---

### Known Technical Debt

| Item | Notes |
|------|-------|
| Logs endpoint returns mock data | `GET /api/logs` |
| Jobs are in-memory only | Not persisted |
| No DELETE/PUT for individual resources | Only POST-based remove |
| No pagination for large datasets | |
| No persistent theme preference | localStorage not used |
| Backup scheduling not implemented | No cron/systemd timer |
| Backup rotation not implemented | Old backups fill disk |
| PortManager deprecated but not removed | `libs/runtime/PortManager.cpp` |
| No real auth for REST API | AuthMiddleware is AllowAll |

---

## DNS Diagnostic Center — ARCH-007

**Status:** COMPLETED (2026-07-16)

**Summary:** Read-only DNS diagnostic center for the ContainerCP admin panel.
Provides live DNS resolution, Configured vs Published comparison, health
scoring, and security recommendations. Does NOT manage DNS zones.

**Components:**

| Component | Description |
|-----------|-------------|
| **DnsCheckService** | Backend DNS resolution using c-ares library (`libs/dns/DnsCheckService.h/.cpp`). Supports A, AAAA, MX, TXT, CNAME, NS, SOA, CAA record types with 60s in-memory cache |
| **SpfAnalyzer** | RFC 7208 SPF analysis (`libs/dns/SpfAnalyzer.h/.cpp`). Evaluates ip4, ip6, a, mx, include, redirect, all mechanisms using c-ares |
| **DNS Check API** | `GET /api/domains/<domain>/dns-check` with type filtering and cache bypass |
| **Domain List** | Progressive DNS/Health/Runtime columns with loading → badge → status |
| **Domain Detail** | 5-tab layout (Overview, DNS Records, Mail, Security, Health) |
| **Configured vs Published** | DNS record comparison for A, MX, SPF, DKIM, DMARC, MTA-STS, Autodiscover |
| **Evidence/Why** | Per-record evidence panel showing expected vs published, reason, fix steps |
| **Mail Tab** | Conditional MailDomain display with MX/SPF/DKIM/DMARC checks |
| **Security Tab** | DMARC Wizard (Monitor/Quarantine/Reject), CAA, MTA-STS, TLS-RPT, Autodiscover recommendations |
| **Health Engine** | Context-aware weighted scoring with grade boundaries. Handles mail/no-mail, site_id=0, SSL states |
| **Admin Panel (site_id=0)** | Synthetic system-domain representation in Domains and Sites. Virtual system Site with capability fields. Protected from deletion |
| **SitesViewService** | Extracted handler for enriched sites JSON including virtual admin-panel site |

---

## Mail Module

### Stage 1a — MailDomain resource

| Task | Status | Notes |
|------|--------|-------|
| MailDomain data model (domain modes) | ✅ | Disabled, LocalPrimary, ExternalRelay, SplitM365 |
| MailDomainManager CRUD | ✅ | |
| Persistence via Storage | ✅ | |
| REST API: GET/POST/DELETE /api/mail/domains | ✅ | |
| Tests | ✅ | |

### Stage 1b — Mailbox resource

| Task | Status | Notes |
|------|--------|-------|
| Mailbox data model | ✅ | |
| MailboxManager CRUD | ✅ | |
| Password hashing (SHA-512-CRYPT) | ✅ | |
| REST API: mailbox endpoints | ✅ | |
| Tests | ✅ | |

### Stage 1c — Mail module lifecycle

| Task | Status | Notes |
|------|--------|-------|
| MailModuleState (inactive / active) | ✅ | Module inactive by default |
| GET /api/mail/status | ✅ | |
| POST /api/mail/activate / deactivate | ✅ | |
| State persisted in Storage | ✅ | |

### Stage 1d — Docker mail stack

| Task | Status | Notes |
|------|--------|-------|
| MailProvider interface | ✅ | write_configs / prepare_environment / start / reload / stop |
| DockerMailProvider implementation | ✅ | Postfix + Dovecot + Redis |
| CommandExecutor for all external commands | ✅ | No std::system |
| Docker Compose generation | ✅ | |
| Config generation separated from runtime | ✅ | |
| Custom config directory structure | ✅ | generated/ + custom/ |
| Module lifecycle integration | ✅ | activate → write + start |
| Tests | ✅ | |

### Stage 2a — Aliases

| Task | Status | Notes |
|------|--------|-------|
| MailAlias data model | ✅ | |
| MailAliasManager CRUD | ✅ | |
| REST API: alias endpoints | ✅ | |
| Alias routing before domain routes | ✅ | |
| virtual_alias_maps generation | ✅ | Fixed: was (void)aliases; |
| Port publishing (25/465/587/143/993) | ✅ | Fixed: was network_mode: service:redis |
| Router prefix chaining | ✅ | Fixed: alias/DKIM endpoints were unreachable |
| ForwarderManager | ⬜ | Deferred |
| Tests | ✅ | |

### Stage 2b — TLS, DKIM, security

| Task | Status | Notes |
|------|--------|-------|
| DkimManager service (Logger + CommandExecutor) | ✅ | |
| DKIM key generation via OpenSSL | ✅ | |
| DKIM DNS record stored and returned via API | ✅ | |
| Postfix TLS configuration | ✅ | |
| Dovecot TLS configuration | ✅ | |
| Postfix transport_maps skeleton | ✅ | LocalPrimary, ExternalRelay, SplitM365 |
| Rspamd DKIM signing | ✅ | milter proxy on port 11332, dkim_signing.conf |
| TLS cert paths from CertificateStore | ✅ | |
| Tests | ✅ | |

### Stage 3 — External modes (implemented)

| Task | Status | Notes |
|------|--------|-------|
| External-relay mode: Postfix relayhost config | ✅ | Per-domain transport maps, no global relayhost |
| Split-M365 mode: catch-all relay transport | ✅ | Per-user LMTP entries + domain SMTP catch-all |
| relay_host validation for ExternalRelay/SplitM365 | ✅ | |
| relay_host on domain creation (API) | ✅ | |
| Transport maps generation tests | ✅ | |
| ADR-007: M365 split delivery routing | ✅ | |
| Routing design document | ✅ | docs/mail-routing-design.md |
| Verified Postfix transport_maps behavior | ✅ | transport_maps overrides virtual_transport |

### Stage 4a — Runtime synchronization (implemented)

| Task | Status | Notes |
|------|--------|-------|
| RuntimeSynchronizer generic abstraction | ✅ | Callback registry in libs/runtime/ |
| Mail sync registration in ServiceRegistry | ✅ | write_configs + reload when active |
| Sync calls in all 11 mail CRUD handlers | ✅ | Every mutation triggers write_configs + reload |
| Tests | ✅ | 5 RuntimeSynchronizer tests |

### Stage 4b — Health reporting (implemented)

| Task | Status | Notes |
|------|--------|-------|
| HealthRegistry generic abstraction | ✅ | Callback registry in libs/runtime/ |
| Mail health check in ServiceRegistry | ✅ | Per-service Postfix/Dovecot/Redis status |
| Health response designed for evolution | ✅ | services[], details{} extensible |
| /api/health extended with module reports | ✅ | Backward-compatible with legacy status field |
| Tests | ✅ | 5 HealthRegistry tests |

### Stage 4c — Mail health API (implemented)

| Task | Status | Notes |
|------|--------|-------|
| GET /api/mail/health endpoint | ✅ | Serializes generic HealthReport model |
| Health report enriched with module details | ✅ | domain_count, mailbox_count, alias_count |
| Response designed for evolution | ✅ | services[], details{} sections |

### Runtime validation (completed)

| Task | Status | Notes |
|------|--------|-------|
| Activate Mail module | ✅ | POST /api/mail/activate succeeds |
| Docker containers start | ✅ | Postfix, Dovecot, Redis all Up |
| /api/mail/health reports ok | ✅ | All 3 services healthy |
| Domain + mailbox creation | ✅ | Runtime sync fires after mutations |
| Config files generated | ✅ | transport_maps, postfix-main.cf, passwd |
| Validation documented | ✅ | docs/testing.md — Mail Runtime Validation |

### Stage 4d — Recovery, SMTP, smarthost, DKIM (implemented)

| Task | Status | Notes |
|------|--------|-------|
| POST /api/mail/reload | ✅ | Reload Postfix config without full restart |
| POST /api/mail/recover | ✅ | Full stop + regenerate + start cycle |
| SMTP server fixes | ✅ | bookworm base image, stale socket cleanup, maillog_file |
| DNS MX resolution | ✅ | resolv.conf + chroot fix + smtp_host_lookup |
| Smarthost API | ✅ | GET/POST /api/mail/smarthost with TLS+SASL |
| Thread-safe crypt_r | ✅ | Replaced crypt() with crypt_r() |
| relay_host validation | ✅ | Format check on POST/PATCH |
| Rspamd DKIM signing | ✅ | milter proxy on port 11332, dkim_signing.conf |
| Dovecot LMTP | ✅ | inet listener, passwd mount, mail dir permissions |
| Tests | ✅ | 146 unit tests, 678 assertions |

### Stage 5 — Webmail and advanced features (planned)

### Stage 5 — Webmail and advanced features

| Task | Status | Notes |
|------|--------|-------|
| Webmail container | ⬜ | |
| Spam filtering UI | ⬜ | |
| Antivirus (ClamAV) | ⬜ | |
| Mail backup/restore | ⬜ | |
| Migration tools | ⬜ | |
| Web UI pages | ⬜ | |

---

## Version 0.6 — DNS and Mail (planned, after v0.5.0)

| Task | Status | Notes |
|------|--------|-------|
| DNS-001: DNS resource and manager | ⬜ | ProfileType::DNS exists as enum only |
| DNS-002: DNS provider interface | ⬜ | |
| DNS-003: DNS CLI and REST API | ⬜ | |
| DNS-004: DNS Web UI pages | ⬜ | |

## Version 0.7 — Monitoring (planned, after v0.6)

| Task | Status | Notes |
|------|--------|-------|
| MON-001: System metrics collection | ⬜ | |
| MON-002: Resource usage dashboard | ⬜ | |
| MON-003: Log viewer enhancements | ⬜ | |
| MON-004: Health check configuration | ⬜ | |

## Version 0.8 — Multi-node (planned, after v0.7)

| Task | Status | Notes |
|------|--------|-------|
| NODE-001: Remote node registration | ⬜ | |
| NODE-002: Distributed storage | ⬜ | |
| NODE-003: Cross-node site migration | ⬜ | |

---

## Current Subsystem Maturity

| Subsystem | Status | Notes |
|-----------|--------|-------|
| Core | Stable | |
| Config | Stable | |
| Logger | Stable | |
| Storage | Stable | Pipe-delimited, SQLite candidate |
| CLI | Stable | Thin client over UNIX socket |
| Daemon | Stable | Dual-mode bootstrap/normal |
| REST API | Active | |
| Web UI | Active | |
| Sites | Stable | |
| Domains | Stable | |
| Databases | Deferred | Postponed — see `planning/database-module-architecture.md` |
| Users | Stable | |
| PHP Versions | Stable | |
| Docker/Runtime | Stable | |
| Reverse Proxy | Active | |
| SSL/Certs | Implemented | ACME HTTP-01, auto-renewal, full GUI |
| Access/SFTP | Experimental | Placeholder provider |
| Backup | Active | |
| Profiles | Stable | |
| Templates | Stable | |
| Jobs | Active | In-memory only |
| Mail | Active | Stages 1a–4d implemented, DKIM via Rspamd ✅ |
| DNS | Planned | |
| Multi-node | Planned | |
| Tests | Growing | |

---

## Validation Status

| Metric | Value |
|--------|-------|
| Validation checklist items | 148 total |
| RC1 pass | 128/137 (9 stability deferred) |
| RC2 validation | All items verified on real Debian 13 |
| Bugs discovered during RC1 | 13 (all fixed) |
| Current Epic | Mail Module Stage 4d — Recovery Integration |

---

## Architecture Decisions (ADRs)

| ADR | Status | Summary |
|-----|--------|---------|
| ADR-0001: Control Plane / Node separation | ✅ | Resources have node_id, future multi-node ready |
| ADR-0002: HostingProvider / Runtime separation | ✅ | Provider orchestrates, Runtime executes docker |
| ADR-003: Let's Encrypt | ✅ | CertificateProvider abstraction |
| ADR-004: REST API | ✅ | Built-in HTTP server, JSON envelope |
| ADR-005: Daemon Architecture | ✅ | containercpd + containercp thin client |
| ADR-006: Web Server Template Profiles | ✅ | Disk-based templates, nginx/Apache selectable |
| ADR-007: M365 Split Delivery Routing | ✅ | See `docs/ADR/ADR-007-m365-split-delivery.md` |

---

## v0.6.0 — DNS and Mail Stable Release

| Item | Status | Notes |
|------|--------|-------|
| Mail module (ARCH-006) | ✅ Implemented | MailDomain, Mailbox, Aliases, DKIM, Docker stack |
| DNS Diagnostics (ARCH-007) | ✅ Implemented | DnsCheckService, Health Score, Admin Panel support |
| SSL/HTTPS (ARCH-005) | ✅ Implemented | ACME HTTP-01, auto-renewal, Web UI |
| Build: clean Release | ✅ | Zero warnings |
| Deterministic tests | ✅ | 242 passed |
| Full test suite | ✅ | 257 passed |
| Manual UI acceptance | ✅ | Browser verification complete |
| 24-hour stability test | ⏭️ | Deferred to future release |
| Production deployment | ⛔ | Not yet deployed |

**Release notes:** `docs/release-notes-v0.6.0.md`

**Completed epics:** ARCH-003, ARCH-004, ARCH-005, ARCH-006, ARCH-007

**Known deferred features:**
- Authoritative DNS zone management
- Backup scheduling and rotation
- PortManager cleanup
- Pagination for large datasets
- Real authentication
- Persistent theme preference

---

## Phase 11 — SQLite Activation

| Task | Status | Notes |
|------|--------|-------|
| P11-01: Current runtime storage analysis | ✅ | Documented in `docs/development/phase11-sqlite-activation-checklist.md` |
| P11-02: Backend selection contract | ✅ | `storage.backend` legacy/sqlite, unknown value fails |
| P11-03: Explicit migration command | ✅ | `containercp storage migrate-to-sqlite` |
| P11-04: Migration orchestrator | ✅ | 13-stage fail-closed pipeline |
| P11-05: Phase 9 verification integration | ✅ | Orchestrator requires verification success |
| P11-06: Phase 10 archive integration | ✅ | Immutable legacy archive integrated |
| P11-07: Activation state | ✅ | `storage-state.json` written after migration |
| P11-08: SQLite startup path | ✅ | Activation state, DB file, PRAGMA, integrity/FK checks |
| P11-09: No silent fallback | ✅ | Commit `23bfe33`; daemon exits before HTTP/UI listeners when SQLite validation fails |
| P11-10: Runtime repository wiring | ✅ | Commit `7a616a5`; all 17 resources verified through SQLite |
| P11-11: Write-path validation | ✅ | Commit `f3dd14e`; replacement commits, no TXT fallback, child-write rollback verified |
| P11-12: Read-path validation | ✅ | Commit `e954568`; checked empty reads and TXT-ignore behavior verified |
| P11-13: Restart persistence | ✅ | Commit `40f703e`; validated reopen preserves all runtime resources |
| P11-14: Failure handling | ✅ | Commit `e855ff6`; symlinked SQLite database path rejected before open |
| P11-15: Observability | ✅ | Commit `526e410`; SQLite startup success/failure logs added |
| P11-16: Operator workflow | ✅ | Commit `615e8b3`; migration diagnostics include activation next steps |
| P11-17: Security | ✅ | Commit `d8fd466`; symlinked activation state rejected before read |
| P11-18: site_id=0 | ✅ | Commit `d824ec2`; approved sentinel records survive validated restart |
| P11-19: Integration tests | ✅ | Commit `173db12`; migrated DB opens through production startup gate |
| P11-20: Production runbook | ✅ | Commit `046e400`; production migration/activation/rollback runbook added |
| P11-21: Clean build and final validation | ✅ | Commit `545a4ce`; clean rebuild, CTest, full doctest passed; warning debt documented |

---

## v0.7.0 — SQLite Storage Stable Release

| Item | Status | Notes |
|------|--------|-------|
| SQLite backend | ✅ Active | Core runtime storage can run on `containercp.db` after explicit activation |
| Manual migration | ✅ Implemented | `containercp storage migrate-to-sqlite` migrates legacy TXT into SQLite |
| Activation gate | ✅ Implemented | `storage-state.json` plus `storage_backend=sqlite` required |
| Startup validation | ✅ Implemented | Daemon validates activation state, SQLite schema, integrity, FK state, and filesystem safety |
| Fail-closed behavior | ✅ Implemented | Invalid SQLite state stops startup; no silent TXT fallback |
| Production validation | ✅ Completed | Production-like migration, activation, restart, and SQLite-only operation validated |

**Release notes:** `docs/releases/v0.7.0.md`

**Completed epic:** ARCH-008 / Phase 11 SQLite Activation

---

*Last updated: 2026-07-19*
*Current Version: v0.7.0*
