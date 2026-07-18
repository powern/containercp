# Backlog

Items are ordered by priority within each category.

Before planning any new Epic, read these documents IN ORDER:
1. planning/PRODUCT_VISION.md
2. planning/product-roadmap.md
3. planning/product-validation.md
4. planning/backlog.md
5. Latest reviews
6. docs/ADR/

## Sprint 2 — PHP Hosting MVP

- [x] Generate complete site directory layout
- [x] Generate .env file with secrets
- [x] Harden docker-compose.yml (restart, health, resources)
- [x] Site start / stop / status polish

## Sprint 3 — Infrastructure Compatibility

- [x] CCP-1001: User resource
- [x] CCP-1002: Domain resource
- [x] CCP-1003: PHP version abstraction
- [x] CCP-1004: Database resource
- [x] CCP-1005: Backup resource
- [x] CCP-1006: SSL resource
- [x] CCP-1007: Mail placeholder resource

## Sprint 4 — Production Ready PHP Hosting

- [x] CCP-2001: Shared utility module
- [x] CCP-2002: Domain validation
- [x] CCP-2003: Username validation
- [x] CCP-2004: Site remove command
- [x] CCP-2005: Improve .env generation
- [x] CCP-2006: Dry-run mode
- [x] CCP-2007: Cache Docker availability
- [x] CCP-2008: Recovery support

## Sprint 5 — Tests and Reliability

- [x] CCP-3001: Add test framework
- [x] CCP-3002: Unit tests for Validator
- [x] CCP-3003: Unit tests for managers and Storage
- [x] CCP-3004: Fix filesystem rollback gap
- [x] CCP-3005: Add --force for site remove
- [x] CCP-3006: Improve validation error messages

## Sprint 6 — Developer Access Layer (Completed during v0.5)

- [x] CCP-4001: AccessUser resource
- [x] CCP-4002: AccessProvider abstraction
- [x] CCP-4003: LocalSftpProvider placeholder
- [x] CCP-4004: CLI access user commands
- [x] CCP-4005: Access directory mapping
- [x] CCP-4006: Document future SFTP implementation

## Sprint 7 — Admin Panel Phase 2

- [x] CCP-5001: Add /api/profiles endpoint
- [x] CCP-5002: Add /api/nodes endpoint
- [x] CCP-5003: Add /api/logs endpoint
- [x] CCP-5005: Site detail page with tabs
- [x] CCP-5006: Create Site modal/dialog
- [x] CCP-5007: Toast notification system
- [x] CCP-5008: Improved tables with row actions
- [x] CCP-5009: Global error handling and loading states

## Version 0.5 — First Production Validation

- [x] Complete Web UI features (wizard, forms, detail pages, CRUD)
- [x] Run first Release Candidate (rc1) on clean Debian 13 VM
- [x] Run full 137-item validation checklist — 128 pass, 9 deferred
- [x] Fix all discovered issues (13 bugs fixed)
- [x] ARCH-004: Docker network routing for multi-site hosting
- [x] Apache2 as default web server backend
- [x] Web UI backend selector (Apache2 default, Nginx optional)
- [x] Show site backend in site details/list
- [x] Fix stale disk templates (overwrite on startup)
- [x] Fix Apache PHP upstream handling
- [x] Run 24-hour stability test (RC2) — *deferred; superseded by v0.6 scope*
- [x] Release v0.5.0-rc2 — *completed (2025-07-08)*
- [x] Final validation — *merged into v0.6.0-rc1*
- [x] Release v0.5.0 — *superseded by v0.6.0-rc1*

## Version 0.6 — DNS and Mail ✅ (v0.6.0)

- [x] ARCH-006: Mail module — MailDomain, Mailbox, Alias, Docker stack, DKIM
- [x] ARCH-007: DNS Diagnostics — DnsCheckService, Health Score, Admin Panel
- [x] MAIL-001: Mail provider implementation — Postfix/Dovecot/Rspamd stack

## Active Epic — Phase 11 SQLite Activation

- [x] P11-01: Current runtime storage analysis
- [x] P11-02: Backend selection contract
- [x] P11-03: Explicit migration command
- [x] P11-04: Migration orchestrator
- [x] P11-05: Phase 9 verification integration
- [x] P11-06: Phase 10 archive integration
- [x] P11-07: Activation state
- [x] P11-08: SQLite startup path
- [x] P11-09: No silent fallback
- [x] P11-10: Runtime repository wiring
- [x] P11-11: Write-path validation
- [x] P11-12: Read-path validation
- [x] P11-13: Restart persistence
- [x] P11-14: Failure handling
- [x] P11-15: Observability
- [x] P11-16: Operator workflow
- [ ] P11-17: Security

## Version 0.7 — Monitoring

- [ ] MON-001: System metrics collection
- [ ] MON-002: Resource usage dashboard
- [ ] MON-003: Log viewer enhancements
- [ ] MON-004: Health check configuration

## Version 0.8 — Multi-node

- [ ] NODE-001: Remote node registration
- [ ] NODE-002: Distributed storage
- [ ] NODE-003: Cross-node site migration

## Future

- [x] SSL certificate generation (Let's Encrypt — ACME HTTP-01, staging/production, auto-renewal, GUI)
- [ ] Reverse proxy integration (Traefik / Caddy)
- [ ] User management CLI commands
- [ ] Resource usage monitoring
- [ ] Stack logs command
- [ ] Database module (postponed — see `planning/database-module-architecture.md`)
- [ ] Multi-node support
- [ ] Mail server support
- [ ] DNS server integration
- [x] README update (docs restructure, navigation links, API reference)
- [x] Web UI: Create Site backend web server selector (Apache2 default, Nginx optional)
- [x] Web UI: Show site backend in site details/list
- [ ] Install/update scripts: systemd service, install.sh, update.sh, dev-rebuild.sh, clean-validation-vm.sh
