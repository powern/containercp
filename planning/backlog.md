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

- [ ] CCP-5001: Add /api/profiles endpoint
- [ ] CCP-5002: Add /api/nodes endpoint
- [ ] CCP-5003: Add /api/logs endpoint
- [ ] CCP-5004: Add /api/site-config endpoint
- [ ] CCP-5005: Site detail page with tabs
- [ ] CCP-5006: Create Site modal/dialog
- [ ] CCP-5007: Toast notification system
- [ ] CCP-5008: Improved tables with row actions
- [ ] CCP-5009: Global error handling and loading states

## Future

- [ ] SSL certificate generation (Let's Encrypt)
- [ ] Reverse proxy integration (Traefik / Caddy)
- [ ] User management CLI commands
- [ ] Backup command
- [ ] Resource usage monitoring
- [ ] Stack logs command
- [ ] Multi-node support
- [ ] Web UI (Phase 2)
- [ ] Mail server support
- [ ] DNS server integration
- [ ] README update
