# Sprint 2 — PHP Hosting MVP

**Goal:** One command must create and run a PHP hosting stack.

```
containercp site create admin example.com
```

## Expected result

After executing the command, the system must produce:

- persistent site record in `/srv/containercp/database/sites.db`
- filesystem structure at `/srv/containercp/sites/example.com/`
- `docker-compose.yml` — fully configured PHP stack
- `.env` — environment variables for the stack
- `www/` — web root for the site
- `public/` — public entry point
- `logs/` — per-site logs
- `tmp/` — temporary files
- `ssl/` — TLS certificates (placeholder)
- `backups/` — backup storage
- running Docker Compose stack (nginx + php + mariadb + redis)

## Scope

- Complete site directory layout generation
- Environment file generation with secrets
- Docker Compose hardening (restart policies, health checks, resource limits)
- Start / stop / status polish
- Site remove command
- Input validation
- Error handling
- Docker availability check
- Basic test suite

## Out of scope

- SSL certificate provisioning
- Reverse proxy integration
- User management
- Backups
- Monitoring
- Web UI

## Definition of done

1. `containercp site create admin example.com` works end to end
2. Generated stack boots without errors
3. Persistent storage survives restart
4. All previous commands still work
