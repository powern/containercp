# ContainerCP Project Status Tracker

> This file is the single source of truth for project completion status.
> It tracks every task across all sprints/versions and cross-references
> the actual codebase.
>
> Legend: ✅ Done  ⬜ Pending  🔄 In Progress  ❌ Blocked  ➖ Removed/Deprecated

---

## Version 0.1 — Core Foundation (Complete)

| Task | Status | Files |
|------|--------|-------|
| C++20 project with CMake/Ninja | ✅ | CMakeLists.txt |
| Core: Application, ServiceRegistry, Config, Resource | ✅ | `libs/core/` |
| Pipe-delimited Storage | ✅ | `libs/storage/` |
| Logger singleton | ✅ | `libs/logger/` |
| CLI thin client + daemon | ✅ | `libs/cli/`, `libs/daemon/` |
| UNIX socket communication | ✅ | `libs/daemon/UnixSocketClient.cpp` |

## Version 0.2 — Hosting Engine (Complete)

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

## Version 0.3 — Platform Services (Complete)

| Task | Status | Files |
|------|--------|-------|
| Daemon architecture (containercpd + containercp) | ✅ | `app/containercpd/main.cpp`, `app/containercp/main.cpp` |
| REST API server | ✅ | `libs/api/ApiServer.cpp` |
| Static file serving for Web UI | ✅ | `libs/api/WebServer.cpp` |
| Auth middleware (AllowAll) | ✅ | `libs/api/AuthMiddleware.cpp` |
| Router with GET/POST | ✅ | `libs/api/Router.cpp` |
| JSON formatter | ✅ | `libs/api/JsonFormatter.cpp` |

## Version 0.4 — Infrastructure Resources (Complete)

| Task | Status | Files |
|------|--------|-------|
| **CCP-1001**: User resource | ✅ | `libs/user/User.h/.cpp`, `UserManager.h/.cpp` |
| **CCP-1002**: Domain resource | ✅ | `libs/domain/Domain.h/.cpp`, `DomainManager.h/.cpp` |
| **CCP-1003**: PHP version abstraction | ✅ | `libs/php/PhpVersion.h/.cpp`, `PhpVersionManager` |
| **CCP-1004**: Database resource | ✅ | `libs/database/Database.h/.cpp`, `DatabaseManager.h/.cpp` |
| **CCP-1005**: Backup resource (initial) | ✅ | `libs/backup/Backup.h/.cpp`, `BackupManager.h/.cpp` |
| **CCP-1006**: SSL resource | ✅ | `libs/ssl/SslCertificate.h/.cpp`, `SslCertificateManager` |
| **CCP-1007**: Mail placeholder resource | ✅ | `libs/mail/MailDomain.h/.cpp`, `MailDomainManager.h/.cpp` |

## Version 0.5 — Web Administration (RC1 Complete)

### Sprint 4 — Production Ready (Complete)

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

### Sprint 5 — Tests and Reliability (Complete)

| Task | Status | Files |
|------|--------|-------|
| **CCP-3001**: Test framework (doctest) | ✅ | `libs/doctest/doctest.h`, `tests/CMakeLists.txt` |
| **CCP-3002**: Validator tests | ✅ | `tests/test_validator.cpp` (20 cases) |
| **CCP-3003**: Manager & Storage tests | ✅ | `tests/test_managers.cpp`, `tests/test_storage.cpp` |
| **CCP-3004**: Filesystem rollback fix | ✅ | `libs/provider/DockerComposeProvider.cpp` |
| **CCP-3005**: --force for site remove | ✅ | `libs/operations/SiteRemoveOperation.cpp` |
| **CCP-3006**: Detailed validation messages | ✅ | `libs/utils/Validator.cpp` |

### Sprint 6 — Developer Access Layer (Complete)

| Task | Status | Files |
|------|--------|-------|
| **CCP-4001**: AccessUser resource | ✅ | `libs/access/AccessUser.h/.cpp`, `AccessUserManager` |
| **CCP-4002**: AccessProvider abstraction | ✅ | `libs/access/AccessProvider.h` |
| **CCP-4003**: LocalSftpProvider placeholder | ✅ | `libs/access/LocalSftpProvider.h/.cpp` |
| **CCP-4004**: CLI access user commands | ✅ | `libs/daemon/DaemonApp.cpp` (handlers) |
| **CCP-4005**: Access directory mapping | ✅ | `libs/access/AccessGrant.cpp` |
| **CCP-4006**: SFTP documentation | ✅ | `docs/SFTP-PROVIDER.md` |

### Sprint 7 — Admin Panel Phase 2 (Complete)

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

### API Layer (Complete)

| Task | Status | Files |
|------|--------|-------|
| **API-001**: POST /api/sites/create | ✅ | `libs/api/ApiServer.cpp` |
| **API-002**: POST /api/sites/remove | ✅ | `libs/api/ApiServer.cpp` |
| **API-003**: POST /api/backups/create | ✅ | `libs/api/ApiServer.cpp` |
| **API-004/005**: GET /api/jobs | ✅ | `libs/api/ApiServer.cpp` |

### Job System (Complete)

| Task | Status | Files |
|------|--------|-------|
| **JOB-001**: In-memory JobManager | ✅ | `libs/jobs/JobManager.h/.cpp` |

### Backup Subsystem (Complete)

| Task | Status | Files |
|------|--------|-------|
| **BACKUP-001**: BackupProvider interface | ✅ | `libs/backup/BackupProvider.h` |
| **BACKUP-002**: TarBackupProvider | ✅ | `libs/backup/TarBackupProvider.h/.cpp` |
| **BACKUP-003**: Backup file management | ✅ | `/srv/containercp/backups/` |
| **BACKUP-004**: Backup resource update | ✅ | `struct Backup` (file_path, compression) |
| **BACKUP-005**: CLI commands | ✅ | backup create/restore/list/show/remove |
| **BACKUP-006**: REST API endpoint | ✅ | GET /api/backups |
| **BACKUP-007**: SiteRemove integration | ✅ (modified: preserves backups after BUG-013) | `libs/operations/SiteRemoveOperation.cpp` |
| **BACKUP-008**: Tests | ✅ | `tests/test_backup.cpp` |

### Architecture Changes (Complete)

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
| BUG-014: Multi-site port conflict | ✅ | ARCH-004 (Docker network routing) |

### RC2 — Stability & Production Foundation (Validated on real Debian 13)

| Item | Status | Notes |
|------|--------|-------|
| install.sh | ✅ | `scripts/install.sh` |
| update.sh | ✅ | `scripts/update.sh` |
| systemd service | ✅ | `packaging/containercp.service` |
| Single instance PID lock | ✅ | `main.cpp` |
| Startup recovery (dirs, network, proxy) | ✅ | `main.cpp` |
| Deployment cleanup on failure (rollback) | ✅ | `SiteCreateOperation.cpp` |
| Category-based logging [SYSTEM][DOCKER][PROXY] | ✅ | `Logger.h/.cpp` |
| Real-time deployment progress in GUI | ✅ | `web/app.js` + job polling |
| journald logging visibility | ✅ | `Logger.cpp` (std::endl flush fix) |
| Apache backend | ✅ | Default backend validated |
| Nginx backend | ✅ | Selectable via profiles |
| Multi-site Docker networking | ✅ | ARCH-004 validated |
| Central proxy recovery | ✅ | Survives daemon restart |
| Web UI operations | ✅ | Create, list, detail, backup |
| Updated docs (README, INSTALL, CHANGELOG) | ✅ | |
| Version bumped to 0.5.0-rc2 | ✅ | |

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
| SSL auto-renewal | 🔄 Being implemented in current Epic |
| No real auth for REST API | AuthMiddleware is AllowAll |

---

### SSL/HTTPS Management Epic (In Progress — ARCH-005)

| Task | Status | Notes |
|------|--------|-------|
| ARCH-005: Architecture Proposal | ✅ | Approved |
| SSL-001: CertificateProvider abstraction | ✅ | Step 1 complete |
| SSL-002: CertificateStore (provider-independent storage) | ✅ | Step 2 complete |
| SSL-003: REST API endpoints | ✅ | Step 3 complete |
| SSL-004: AcmeClient + LetsEncryptProvider (ACME engine) | ✅ | Step 4 complete |
| SSL-005: Proxy integration (attach certs, reload, redirect) | ✅ | Step 5 complete |
| SSL-006: RenewalScheduler | ✅ | Step 6 complete |
| SSL-007: Web UI SSL page | ✅ | Step 7 complete (minimal) |
| SSL-008: Real ACME HTTP-01 staging | ✅ | Step 8A complete |
| SSL-009: Real server validation + bug fixes | ✅ | Step 8B: ACME flow, CSR, finalize, HTTPS paths, renewal policy |
| SSL-010: Comprehensive SSL/Proxy cleanup | ✅ | Canonical upstream, transactional API, SSL mount, no fallbacks |

## Version 0.6 — DNS and Mail (Planned, after v0.5.0)

| Task | Status | Notes |
|------|--------|-------|
| DNS-001: DNS resource and manager | ⬜ | ProfileType::DNS exists as enum only |
| DNS-002: DNS provider interface | ⬜ | |
| DNS-003: DNS CLI and REST API | ⬜ | |
| DNS-004: DNS Web UI pages | ⬜ | |
| MAIL-001: Mail provider implementation | ⬜ | MailDomain resource exists, no provider |

## Version 0.7 — Monitoring (Planned, after v0.6)

| Task | Status | Notes |
|------|--------|-------|
| MON-001: System metrics collection | ⬜ | |
| MON-002: Resource usage dashboard | ⬜ | |
| MON-003: Log viewer enhancements | ⬜ | |
| MON-004: Health check configuration | ⬜ | |

## Version 0.8 — Multi-node (Planned, after v0.7)

| Task | Status | Notes |
|------|--------|-------|
| NODE-001: Remote node registration | ⬜ | |
| NODE-002: Distributed storage | ⬜ | |
| NODE-003: Cross-node site migration | ⬜ | |

---

## Current Maturity Assessment (SSL/HTTPS Epic)

| Subsystem | Completeness | Status |
|-----------|-------------|--------|
| Core | 100% | Stable |
| Config | 100% | Stable |
| Logger | 100% | Stable |
| Storage | 95% | Stable |
| CLI | 90% | Stable |
| Daemon | 98% | Stable |
| REST API | 92% | Active |
| Web UI | 85% | Active |
| Sites | 95% | Stable |
| Domains | 95% | Stable |
| Databases | 95% | Stable |
| Users | 95% | Stable |
| PHP Versions | 90% | Stable |
| Docker/Runtime | 92% | Stable |
| Reverse Proxy | 92% | Active |
| SSL/Certs | 10% | Epic in progress (ARCH-005) |
| Access/SFTP | 75% | Active (placeholder) |
| Backup | 85% | Active |
| Profiles | 90% | Stable |
| Templates | 85% | Stable |
| Jobs | 80% | Active |
| Mail | 10% | Placeholder only |
| DNS | 0% | Not started |
| Multi-node | 0% | Not started |
| Tests | 60% | Growing |

---

## Validation Status

| Metric | Value |
|--------|-------|
| Validation checklist items | 148 total |
| RC1 pass | 128/137 (9 stability deferred) |
| RC2 validation | ✅ All items verified on real Debian 13 |
| Bugs discovered during RC1 | 13 (all fixed) |
| Next milestone | SSL/HTTPS Management (ARCH-005) → v0.5.0 stable |

---

## Key Architecture Decisions (ADRs)

| ADR | Status | Summary |
|-----|--------|---------|
| ADR-0001: Control Plane / Node separation | ✅ | Resources have node_id, future multi-node ready |
| ADR-0002: HostingProvider / Runtime separation | ✅ | Provider orchestrates, Runtime executes docker |
| ADR-003: Let's Encrypt | ✅ | CertificateProvider abstraction, placeholder impl |
| ADR-004: REST API | ✅ | Built-in HTTP server, JSON envelope, no deps |
| ADR-005: Daemon Architecture | ✅ | containercpd + containercp thin client |
| ADR-006: Web Server Template Profiles | ✅ | Disk-based templates, nginx/Apache selectable |

## Architecture Proposals

| Proposal | Status | Summary |
|----------|--------|---------|
| ARCH-001: Complete Web UI v0.5 | ✅ Implemented | Multi-step wizard, job progress, CRUD |
| ARCH-002: First Production Validation | ✅ Implemented | 137-item checklist, clean VM testing |
| ARCH-003: Web UI Public Access | ✅ Implemented | Dual listener (8080 API, 8081 UI) |
| ARCH-004: Docker Network Multi-Site | ✅ Implemented | No host ports, shared public network, per-site private networks |
| ARCH-005: SSL/HTTPS Management | 🔄 In Progress | Real ACME HTTP-01, auto-renewal, REST API, Web UI |

---

*Last updated: 2025-07-08*
*Next planned action: SSL/HTTPS Management (ARCH-005) → v0.5.0 stable → v0.6 DNS*
*RC2 validated on real Debian 13 — all items complete*
