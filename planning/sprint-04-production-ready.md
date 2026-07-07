# Sprint 4 — Production Ready PHP Hosting

**Goal:** Make `containercp site create` reliable, safe, and
production-ready.

After this sprint, creating a site must be fully reversible, validate
all inputs, use consistent credentials, and never leave partial state
behind.

## Scope

- Shared utility module for password generation and common helpers
- RFC-compliant domain validation
- Username validation
- `site remove` command with full cleanup
- Database credentials propagated to `.env`
- `--dry-run` mode for `site create`
- Docker availability caching
- Automatic rollback on failure

## Out of scope

- Let's Encrypt integration
- Mail server setup
- Reverse proxy configuration
- Web UI
- Multi-node support
- SQLite migration

## Definition of done

1. `site create` validates domain format before creating anything
2. `site create --dry-run` validates without writing anything to disk
3. `site remove` removes compose, filesystem, database records, and all
   linked resources
4. `.env` contains database credentials from the Database resource
5. Docker check runs once per process
6. Partial creation failure rolls back all created resources
7. All previous commands continue working
