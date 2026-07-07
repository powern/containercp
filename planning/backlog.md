# Backlog

Items are ordered by priority within each category.

## Sprint 2 — PHP Hosting MVP

- [x] Generate complete site directory layout
- [x] Generate .env file with secrets
- [x] Harden docker-compose.yml (restart, health, resources)
- [x] Site start / stop / status polish
- [ ] Site remove command
- [ ] Input validation (domain format, owner)
- [ ] Error handling (Docker missing, disk full, etc.)
- [ ] Docker availability check on startup
- [ ] Basic test suite

## Sprint 3 — Infrastructure Compatibility

- [ ] CCP-1001: User resource
- [ ] CCP-1002: Domain resource
- [ ] CCP-1003: PHP version abstraction
- [ ] CCP-1004: Database resource
- [ ] CCP-1005: Backup resource
- [ ] CCP-1006: SSL resource
- [ ] CCP-1007: Mail placeholder resource

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
