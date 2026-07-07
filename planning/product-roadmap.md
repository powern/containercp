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
CLI                Stable         85%
Daemon             Stable         90%
REST API           Active         85%
Web UI             Active         60%
Sites              Stable         90%
Domains            Stable         90%
Databases          Stable         90%
Users              Stable         90%
PHP Versions       Stable         85%
Docker/Runtime     Stable         85%
Reverse Proxy      Active         75%
SSL/Certs          Active         70%
Access/SFTP        Active         70%
Backup             Active         80%
Profiles           Stable         85%
Templates          Stable         80%
Jobs               Active         70%
Mail               Placeholder    10%
DNS                Not started     0%
Multi-node         Not started     0%
Tests              Growing        50%
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

## Version 0.5 — Web Administration (In Progress)

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

**Remaining:**
- Site creation wizard with deployment progress
- Edit/delete for all resources
- Resource detail pages with full data
- Dashboard with real monitoring data
- Form validation (client + server)
- Pagination for large datasets

**Release Candidates:**
- v0.5.0-rc1 — first validation on Debian 13 (Trixie) — **passed**
- v0.5.0-rc2 — (future) stability and edge cases
- v0.5.0-rc3 — (future) final validation

**First Production Validation milestone:**
The RC1 validation cycle completed on 2025-07-07.

Core lifecycle validation passed: 128 of 137 checklist items pass
on a clean Debian 13 Validation VM. The 9 remaining items are
24-hour stability checks deferred to RC2.

See `planning/validation-v0.5.0-rc1.md` for full results.

**Acceptance criteria for v0.5.0:**
- [x] Zero compiler warnings (Debug + Release)
- [x] Core lifecycle validated on clean Debian 13 VM
- [x] 128 checklist items pass (9 stability items deferred)
- [x] All unit tests pass
- [ ] 24-hour stability test passes
- [ ] No orphan resources after cleanup

## Version 0.6 — DNS and Mail

**Planned features:**
- DNS resource and provider
- DNS template profiles
- CLI and API for DNS management
- MailDomain resource activation
- Mail provider (Postfix/Dovecot placeholder)
- Web UI pages for DNS and Mail

**Estimated effort:** 2 epics, ~15 tasks

## Version 0.7 — Monitoring and Observability

**Planned features:**
- System metrics collection (disk, memory, CPU)
- Site-level resource usage
- Log aggregation and viewer
- Health check dashboard
- Alert configuration
- Notification channels (email, webhook)

**Estimated effort:** 2 epics, ~12 tasks

## Version 0.8 — Multi-node and Cluster

**Planned features:**
- Remote node registration
- Node-to-node communication
- Distributed storage
- Cross-node site migration
- Load balancing
- High availability

**Estimated effort:** 3 epics, ~20 tasks

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
Debian 12 installation.

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
