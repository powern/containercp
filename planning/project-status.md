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

### v0.5.0 Release — pending 24h stability test

| Item | Status | Notes |
|------|--------|-------|
| 24-hour stability test (RC2) | ⬜ | Ready to start |
| v0.5.0 stable release | ⬜ | After stability pass |
| Final validation (148 items) | ⬜ | All pass expected |

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

## SSL/HTTPS Management Epic — ARCH-005

| Task | Status | Notes |
|------|--------|-------|
| ARCH-005: Architecture Proposal | ✅ | Approved |
| SSL-001: CertificateProvider abstraction | ✅ | |
| SSL-002: CertificateStore | ✅ | |
| SSL-003: REST API endpoints | ✅ | |
| SSL-004: AcmeClient + LetsEncryptProvider | ✅ | |
| SSL-005: Proxy integration | ✅ | |
| SSL-006: RenewalScheduler | ✅ | |
| SSL-007: Web UI SSL page | ✅ | Minimal |
| SSL-008: Real ACME HTTP-01 staging | ✅ | |
| SSL-009: Server validation + bug fixes | ✅ | |
| SSL-010: Comprehensive SSL/Proxy cleanup | ✅ | |
| SSL-011: Production-ready finalization | ✅ | |
| SSL-012: Admin Panel HTTPS | ✅ | |
| SSL-013: Bootstrap/Normal mode | ✅ | |

**Status:** Implemented

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
| Rspamd milter preparation | ✅ | |
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

### Stage 4c — Recovery and health integration (planned)

| Task | Status | Notes |
|------|--------|-------|
| MailHealthMonitor | ⬜ | |
| GET /api/mail/health | ⬜ | |
| Integration with RecoveryManager | ⬜ | |
| MX record validation | ⬜ | |

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
| Mail | In Progress | Stages 1a–2b implemented, Stage 3 active |
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
| Current Epic | Mail Module Stage 4c — Recovery and Health Integration |

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

*Last updated: 2025-07-10*
*Current Epic: Mail Module Stage 4b — Health and Recovery (after runtime sync)*
