# Backlog

Items are ordered by priority within each category.

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

## Sprint 6 — Developer Access Layer

- [ ] CCP-4001: AccessUser resource
- [ ] CCP-4002: AccessProvider abstraction
- [ ] CCP-4003: LocalSftpProvider placeholder
- [ ] CCP-4004: CLI access user commands
- [ ] CCP-4005: Access directory mapping
- [ ] CCP-4006: Document future SFTP implementation

## Sprint 7 — Admin Panel Phase 2

- [x] CCP-5001: Add /api/profiles endpoint
- [x] CCP-5002: Add /api/nodes endpoint
- [x] CCP-5003: Add /api/logs endpoint
- [x] CCP-5005: Site detail page with tabs
- [x] CCP-5006: Create Site modal/dialog
- [x] CCP-5007: Toast notification system
- [x] CCP-5008: Improved tables with row actions
- [x] CCP-5009: Global error handling and loading states

## Backlog — Backup and Restore

- [ ] BACKUP-001: BackupProvider interface
- [ ] BACKUP-002: TarBackupProvider
- [ ] BACKUP-003: Backup file management
- [ ] BACKUP-004: Update Backup resource and manager
- [ ] BACKUP-005: CLI commands
- [ ] BACKUP-006: REST API endpoint
- [ ] BACKUP-007: SiteRemoveOperation integration
- [ ] BACKUP-008: Tests

## Version 0.6 — DNS and Mail

- [ ] DNS-001: DNS resource and manager
- [ ] DNS-002: DNS provider interface
- [ ] DNS-003: DNS CLI and REST API
- [ ] DNS-004: DNS Web UI pages
- [ ] MAIL-001: Mail provider implementation

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

- [ ] SSL certificate generation (Let's Encrypt)
- [ ] Reverse proxy integration (Traefik / Caddy)
- [ ] User management CLI commands
- [ ] Resource usage monitoring
- [ ] Stack logs command
- [ ] Multi-node support
- [ ] Mail server support
- [ ] DNS server integration
- [ ] README update
