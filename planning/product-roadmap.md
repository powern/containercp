# ContainerCP Product Roadmap

This roadmap is guided by the Product Vision in `planning/PRODUCT_VISION.md`.
Every version milestone supports the vision of a modern, container-native
hosting control panel.

Before implementing any milestone, an Architecture Proposal must be
written and approved (see `planning/proposals/README.md`).

## Current maturity assessment

```
Subsystem          Status        Completeness
────────────────────────────────────────────────
Core               Stable        100%
Config             Stable        100%
Logger             Stable        100%
Storage            Stable         95%
CLI                Stable         90%
Daemon             Stable         95%
REST API           Active         95%
Web UI             Active         90%
Sites              Stable         95%
Domains            Stable         95%
Databases          Planned        40%
Users              Stable         95%
PHP Versions       Stable         90%
Docker/Runtime     Stable         90%
Reverse Proxy      Active         95%
SSL/Certs          Active         90%
Access/SFTP        Active         75%
Backup             Active         85%
Profiles           Stable         90%
Templates          Stable         85%
Jobs               Active         75%
Mail               Active         85%
DNS (Diagnostics)  Active         90%
DNS (Zone Mgmt)    Not started     0%
Multi-node         Not started     0%
Tests              Active         75%
```

## Version 0.1 — Core Foundation (Complete)

**Features:**
- C++20 project with CMake/Ninja
- Core modules: Application, ServiceRegistry, Config, Resource
- Pipe-delimited Storage for 12 resource types
- Logger singleton
- CLI with 40+ commands via thin client + daemon
- UNIX socket communication

**Remaining:** Storage could use SQLite migration for atomicity.

## Version 0.2 — Hosting Engine (Complete)

**Features:**
- Docker Compose generation with PHP, nginx, mariadb, redis
- Runtime abstraction (DockerRuntime)
- HostingProvider interface (DockerComposeProvider)
- Reverse proxy config generation (NginxProxyProvider)
- SSL certificate management with Let's Encrypt placeholder
- Web server template profiles (nginx/apache, PHP/WordPress/Laravel)

**Remaining:** SSL auto-renewal scheduler, real ACME integration.

## Version 0.3 — Platform Services (Complete)

**Features:**
- Daemon architecture (containercpd + containercp CLI)
- REST API server with 15+ endpoints
- Static file serving for Web UI
- Auth middleware interface (AllowAll)
- Router with GET/POST support
- JSON formatter for all resources

**Remaining:** Real authentication, rate limiting.

## Version 0.4 — Infrastructure Resources (Complete)

**Features:**
- AccessUser and AccessGrant resources
- LocalSftpProvider placeholder
- Profile subsystem with ProfileType enum
- TemplateProfile migrated to Profile
- Disk-based web templates with validation
- TarBackupProvider with create/restore/remove
- Backup CLI and REST API
- In-memory Job tracking

**Remaining:** Backup scheduling, backup rotation.

## Version 0.5 — Web Administration ✅ COMPLETE

**Features:**
- Admin panel with 13 pages (dashboard, sites, domains, databases, SSL, proxy, access, backups, profiles, templates, nodes, logs, settings)
- Dark/light theme toggle
- Responsive layout with sidebar
- Toast notification system
- Site detail page with tabs
- Create Site modal with backend API call
- Backups page with create functionality
- Background job tracking
- CRUD POST endpoints (sites create/remove, backups create)
- Docker network based multi-site routing (ARCH-004) — no host port allocation
- Central reverse proxy surviving daemon restarts
- Apache2 as default backend web server (Nginx selectable via profiles)
- Web UI backend web server selector on site creation
- Per-site private Docker networks for backend service isolation
- Template overwrite on startup to prevent stale configs
- Fix for Apache PHP upstream handling

**Release Candidates:**
- v0.5.0-rc1 — first validation on Debian 13 — **passed**
- v0.5.0-rc2 — validated on real Debian 13 — **all items complete**

**Completed epics:**
- ARCH-003: Web UI Public Access (dual listener)
- ARCH-004: Docker Network Multi-Site
- ARCH-005: SSL/HTTPS Management (ACME HTTP-01, auto-renewal, redirect, Web UI)
- ARCH-006: Mail Module (MailDomain, Mailbox, MailAlias, DKIM, Docker mail stack)

## Version 0.6 — DNS and Mail (RC1)

**Release:** v0.6.0-rc1 (2026-07-16)

**Scope:**

**Mail (ARCH-006, completed during v0.5):**
- MailDomain resource with 4 modes (Disabled, LocalPrimary, ExternalRelay, SplitM365)
- Mailbox CRUD with SHA-512-CRYPT password hashing
- Mail alias support with domain-level routing
- Docker mail stack: Postfix, Dovecot, Redis, Rspamd
- DKIM key generation via OpenSSL, stored in MailDomain
- TLS configuration for Postfix + Dovecot
- Rspamd DKIM signing via milter proxy
- External relay mode: per-domain transport maps
- Split-M365 mode: local mailboxes + catch-all relay to M365
- Runtime synchronization (11 mail CRUD handlers trigger config regeneration)
- Mail health reporting (Postfix/Dovecot/Redis status)
- Module lifecycle (activate/deactivate/status)
- Mail reload, recover endpoints
- SMTP server fixes (chroot, socket cleanup, DNS resolution)
- Smarthost API with TLS + SASL support
- DKIM signing fix for PHP Mail (allow_username_mismatch)

**DNS Diagnostics (ARCH-007):**
- DnsCheckService using c-ares library (A, AAAA, MX, TXT, CNAME, NS, SOA, CAA)
- 60s in-memory cache with refresh=1 bypass
- DNS Check REST API with type filtering and error semantics
- Domain List with progressive DNS/Runtime/Health column loading
- Domain Detail with 5 tabs: Overview, DNS Records, Mail, Security, Health
- Configured vs Published comparison for A, MX, SPF, DKIM, DMARC, MTA-STS
- SPF analysis (RFC 7208) with SpfAnalyzer
- DMARC Wizard with 3 policies (Monitor, Quarantine, Reject)
- Evidence/Why panels with expected/actual/reason/fix
- Context-aware Health Score (weighted, grade boundaries, mail/no-mail context)
- Admin-panel virtual system Site and Domain (site_id=0)

**SSL and Security (ARCH-005, completed):**
- ACME HTTP-01 with Let's Encrypt (staging + production)
- CertificateStore with versioned metadata
- Auto-renewal scheduler
- HTTP→HTTPS redirect support
- SSL Web UI page
- Admin panel virtual Site SSL (site_id=0)

**Deferred to future releases:**
- Authoritative DNS zone management (CLI, API, provider)
- 24-hour stability test (RC2 criterion deferred)
- Backup scheduling and rotation
- PortManager cleanup (deprecated after ARCH-004)
- Pagination for large datasets
- Real authentication (AuthMiddleware is AllowAll)
- Persistent theme preference

**Acceptance criteria for v0.6.0-rc1:**
- [x] Zero compiler warnings
- [x] All deterministic tests pass (242+)
- [x] Mail module operational (Postfix, Dovecot, Redis, Rspamd)
- [x] DKIM generation and signing
- [x] DNS diagnostics operational (live resolution, comparison, health)
- [x] Admin-panel system Site and Domain visible and protected
- [x] No /api/runtime/0 or /api/sites/0/mail-status calls
- [x] API documentation updated
- [x] CHANGELOG updated
- [x] Architecture documentation updated

## Version 0.7 — Monitoring and Observability

**Planned features:**
- System metrics collection (disk, memory, CPU)
- Site-level resource usage
- Log aggregation and viewer
- Health check dashboard
- Alert configuration
- Notification channels (email, webhook)

**Estimated effort:** 2 epics, ~12 tasks

## Version 0.8 — WordPress Credentials and Databases Foundation

**Planned features:**
- WordPress credential source detection for migrated and future WordPress sites
- `WordPressConfigService` read-only inspection and safe direct-constant updates
- Database password rotation saga design and implementation path
- MariaDB provider password-change foundation
- Imported myVestaCP database state handling without assuming lifecycle ownership
- Databases DB-1 read-only inventory after the WordPress credential foundation is stable
- Databases GUI only after API and credential boundaries are ready

**Deferred from old v0.8 scope:** Multi-node and cluster work moves to a later release so the immediate post-v0.7 work can address migrated WordPress/database safety.

**Estimated effort:** 2 epics, ~12 tasks

## Future — Multi-node and Cluster

**Planned features:**
- Remote node registration
- Node-to-node communication
- Distributed storage
- Cross-node site migration
- Load balancing
- High availability

## Version 1.0 — Production Ready

**Features:**
- All subsystems stable and tested
- Production installation script
- systemd service files
- Backup/restore validated
- Security hardening
- Documentation complete
- Upgrade path from 0.x

---

## First Production Validation

**Objective:** Deploy ContainerCP on a clean Debian 13 (Trixie)
Validation VM (see `planning/TEST_ENVIRONMENT.md`) and verify the
complete hosting workflow end-to-end.

### Prerequisites

- Clean Debian 13 (Trixie) installation
- Docker and Docker Compose installed
- Git, CMake, Ninja, g++ installed
- Port 8080 open for Web UI
- Port 8081 open for external Web UI
- Root or sudo access

### Validation checklist

#### Installation
- [ ] Clone repository
- [ ] Run `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release`
- [ ] Run `cmake --build build-release`
- [ ] No compilation errors
- [ ] Binaries exist: `build-release/containercpd`, `build-release/containercp`

#### Daemon startup
- [ ] Start `./build-release/containercpd`
- [ ] Daemon logs "Listening on 127.0.0.1:8080"
- [ ] Daemon logs "Listening on /srv/containercp/containercpd.sock"
- [ ] UNIX socket file exists
- [ ] Storage directory `/srv/containercp/database/` contains all .db files

#### REST API availability
- [ ] `curl http://127.0.0.1:8080/api/version` returns version
- [ ] `curl http://127.0.0.1:8080/api/health` returns `{"status":"ok"}`
- [ ] All GET endpoints return valid JSON

#### Web UI availability
- [ ] `http://127.0.0.1:8080/` loads dashboard
- [ ] Sidebar shows 13 navigation items
- [ ] Status indicator shows "Connected"
- [ ] Dashboard cards show zero counts

#### CLI availability
- [ ] `./build-release/containercp node list` shows "local"
- [ ] `./build-release/containercp user list` shows "admin"
- [ ] `./build-release/containercp php list` shows 8.2, 8.3, 8.4

#### Site creation
- [ ] `containercp site create admin demo.local` succeeds
- [ ] Site record appears in `site list`
- [ ] Domain record appears in `domain list`
- [ ] Database record appears in `database list`
- [ ] Directory `/srv/containercp/sites/demo.local/` exists
- [ ] `docker-compose.yml` exists in site directory
- [ ] `.env` file exists with credentials
- [ ] Docker Compose stack starts (`docker compose up -d`)
- [ ] Containers are running (`docker compose ps` shows healthy)

#### Web server configuration
- [ ] Nginx config exists at `config/nginx/default.conf`
- [ ] Config uses correct PHP upstream
- [ ] Proxy config exists at `/srv/containercp/proxy/sites/demo.local.conf`

#### SSL certificate
- [ ] `containercp ssl request demo.local` creates certificate record
- [ ] Certificate appears in `ssl list`
- [ ] Certificate status is "active"
- [ ] Proxy config includes SSL block

#### Access user
- [ ] `containercp access user create developer demo.local` creates user
- [ ] User appears in `access user list`
- [ ] Grant record exists in `access grant list`

#### Backup and restore
- [ ] `containercp backup create demo.local` creates backup file
- [ ] Backup file exists at `/srv/containercp/backups/`
- [ ] Backup record appears in `backup list`
- [ ] `containercp backup restore <id>` restores site files

#### Site removal
- [ ] `containercp site remove demo.local --force` removes everything
- [ ] Site directory is deleted
- [ ] Database records are cleaned up
- [ ] Containers are stopped and removed
- [ ] Backup files are removed

#### Web UI operations
- [ ] Dashboard shows correct site count after creation
- [ ] Sites page lists all sites
- [ ] Clicking site domain opens detail page
- [ ] Backups page shows created backup
- [ ] Create Site modal submits via API
- [ ] Create Backup modal submits via API

#### Cleanup
- [ ] All test sites removed
- [ ] All test backups removed
- [ ] Daemon stops cleanly
- [ ] No orphan files on disk

### Acceptance criteria

The platform passes the First Production Validation when every
item on the validation checklist has been verified on a clean
Debian 13 (Trixie) installation.

---

## Proposed next epic: DNS Management

**Why:** DNS is the next biggest functional gap. A hosting panel
without DNS management cannot be used for production websites.
DNS integration completes the core hosting workflow: register
domain → point DNS → deploy site → serve HTTPS.

**Product value:** High
**Dependencies:** Profile subsystem (done), Storage (done)
**Estimated effort:** 2 epics, 10-12 tasks
**Risk:** Low (follows established provider pattern)
