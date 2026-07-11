# ContainerCP Testing Guide

## Purpose

The self-test (`scripts/selftest.sh`) validates critical production
functionality after architectural changes, before releases, or after
any modification to startup, recovery, proxy, or bootstrap subsystems.

It simulates real failure scenarios:

- Legacy corruption of `setup_completed` flag
- Reverse proxy container being stopped
- RecoveryManager automatic self-healing

And verifies that the system returns to a healthy state without manual
intervention.

## Mail Module Runtime Validation

### Purpose

Validate that the Mail Docker stack starts, stays healthy, and
synchronizes configuration after data changes.

### Prerequisites

```bash
# Build the project
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

# Build the mail Docker images (only needed once)
docker build -t ghcr.io/containercp/mail-postfix:latest \
    -f docker/mail/Dockerfile.postfix docker/mail/
docker build -t ghcr.io/containercp/mail-dovecot:latest \
    -f docker/mail/Dockerfile.dovecot docker/mail/
```

### Validation procedure

```bash
# 1. Start the daemon
SERVER_HOSTNAME=mail-test.local ./build-release/containercpd &

# 2. Activate the Mail module
curl -s -X POST http://127.0.0.1:8080/api/mail/activate
# Expected: {"success":true,"data":{"message":"Mail module activated"}}

# 3. Verify containers are running
docker ps --format "table {{.Names}}\t{{.Status}}"
# Expected: containercp-mail-postfix, containercp-mail-dovecot, containercp-mail-redis all "Up"

# 4. Verify health endpoint
curl -s http://127.0.0.1:8080/api/mail/health | python3 -m json.tool
# Expected: all services status "ok", module_state "active"
# {
#   "status": "ok",
#   "services": [
#     {"name": "postfix", "status": "ok", "message": "running"},
#     {"name": "dovecot", "status": "ok", "message": "running"},
#     {"name": "redis",   "status": "ok", "message": "running"}
#   ],
#   "details": {
#     "module_state": "active",
#     "domain_count": 0,
#     "mailbox_count": 0,
#     "alias_count": 0
#   }
# }

# 5. Create a test domain
curl -s -X POST http://127.0.0.1:8080/api/mail/domains \
  -H "Content-Type: application/json" \
  -d '{"domain":"testmail.local","mode":"local-primary","owner_id":1}'
# Expected: success true, domain record returned

# 6. Create a test mailbox
curl -s -X POST http://127.0.0.1:8080/api/mail/domains/1/mailboxes \
  -H "Content-Type: application/json" \
  -d '{"local_part":"alice","password":"secret"}'
# Expected: success true, mailbox record returned

# 7. Verify runtime sync generated config files
cat /srv/containercp/mail/config/generated/transport_maps
# Expected: testmail.local lmtp:containercp-mail-dovecot:24

cat /srv/containercp/mail/config/generated/postfix-main.cf
# Expected: virtual_mailbox_domains includes testmail.local

cat /srv/containercp/mail/config/generated/virtual_mailboxes
# Expected: alice@testmail.local with path

# 8. Verify health reflects new data
curl -s http://127.0.0.1:8080/api/mail/health | python3 -m json.tool
# Expected: domain_count=1, mailbox_count=1

# 9. Verify aliases work
curl -s -X POST http://127.0.0.1:8080/api/mail/domains/1/aliases \
  -H "Content-Type: application/json" \
  -d '{"source":"info","destination":"alice@testmail.local"}'
# Expected: success true, alias record returned

# 10. Verify alias file generated
cat /srv/containercp/mail/config/generated/virtual_aliases
# Expected: info@testmail.local\talice@testmail.local

# 11. Verify ports are published
docker ps --format "table {{.Names}}\t{{.Ports}}"
# Expected:
#   containercp-mail-postfix  0.0.0.0:25->25/tcp, 0.0.0.0:465->465/tcp, 0.0.0.0:587->587/tcp
#   containercp-mail-dovecot  0.0.0.0:24->24/tcp, 0.0.0.0:143->143/tcp, 0.0.0.0:993->993/tcp

# 12. Check daemon logs for errors
grep -E "ERROR|FATAL|FAILED" /tmp/containercpd.log
# Expected: no mail-related errors (proxy SSL mount warnings are normal)
```

### Known issues

| Issue | Status | Notes |
|-------|--------|-------|
| Postfix/Dovecot ports not exposed to host | Open | Containers use `network_mode: service:redis`; need port publishing in docker-compose.yml |
| Dovecot SSL cert on fresh install | Workaround | Create self-signed cert: `mkdir -p /srv/containercp/ssl/0 && openssl req -x509 -newkey rsa:2048 -keyout /srv/containercp/ssl/0/privkey.pem -out /srv/containercp/ssl/0/fullchain.pem -days 365 -nodes -subj "/CN=mail-test.local"` |
| ghcr.io images not published | Resolved | Build locally with provided Dockerfiles |
| `prepare_environment` order | Fixed | Moved before `write_configs` in activate handler |

### Validation history

| Date | Validator | Result | Notes |
|------|-----------|--------|-------|
| 2026-07-10 | Runtime | ✅ Pass | All 3 containers up, health OK, sync OK |

```bash
# Default admin URL (web2.softico.ua):
sudo ./scripts/selftest.sh

# Custom admin URL:
ADMIN_URL=https://admin.example.com sudo ./scripts/selftest.sh

# Custom data root:
DATA_ROOT=/srv/containercp sudo ./scripts/selftest.sh
```

The script must be run as root on the ContainerCP server.

## What is tested

| # | Test | What it validates |
|---|------|-------------------|
| 1 | Daemon status | `systemctl is-active containercpd` returns active |
| 2 | Port binding | REST API on `127.0.0.1:8080`, Web UI on port 8081 |
| 3 | Admin URL | `https://admin.domain/` returns HTTP 200/302 |
| 4 | Proxy container | `containercp-proxy` is running |
| 5 | Legacy migration | Sets `setup_completed=0`, restarts daemon, verifies automatic restoration to `1` |
| 6 | Proxy recovery | Stops `containercp-proxy`, waits up to 90s for RecoveryManager to detect and restart it |
| 7 | Final health | Admin URL returns 200/302 after proxy recovery |

## Expected output

```
============================================
  ContainerCP Self-Test
============================================

--- Test 1: Daemon status ---
  [PASS] containercpd is active
--- Test 2: Port binding ---
  [PASS] REST API on 127.0.0.1:8080
  [PASS] Web UI on port 8081
--- Test 3: Admin URL ---
  [PASS] Admin URL https://admin.domain returns 200/302
--- Test 4: Proxy container ---
  [PASS] containercp-proxy is running
--- Test 5: Legacy setup_completed migration ---
  [PASS] Legacy setup_completed migration restored flag to 1
  [PASS] Daemon active after migration
--- Test 6: Proxy recovery ---
  [PASS] Proxy recovered automatically after docker stop
  [PASS] Admin URL https://admin.domain returns 200/302 after recovery
--- Test 7: Final health ---
  [PASS] Admin URL https://admin.domain returns 200/302

============================================
  Results: 9 passed, 0 failed
============================================
```

## Cleanup

The script automatically restores the system to a healthy state on exit:

- Restores `setup_completed` to `1` if it was corrupted during testing
- Starts `containercp-proxy` if it was stopped during recovery testing

No manual cleanup is required after a normal run.

## When to run

Recommended before:

- **Releases** — validate that the build is production-ready
- **Architectural changes** — after modifying startup, recovery, or proxy code
- **Bootstrap changes** — after modifying `StartupManager` or mode detection
- **Recovery changes** — after modifying `RecoveryManager` or `ensure_central_proxy()`
- **Proxy changes** — after modifying `NginxProxyProvider` or `ProxyConfigBuilder`
- **Logger changes** — after modifying thread safety or output format

## How it integrates with the architecture

```
selftest.sh
  │
  ├── Test 1-4: Static state verification
  │   (systemctl, ss, curl, docker ps)
  │
  ├── Test 5: StartupManager migration
  │   (corrupts flag → restart → verify migration)
  │
  ├── Test 6: RecoveryManager + NginxProxyProvider
  │   (docker stop proxy → wait → verify recovery)
  │
  └── Test 7: End-to-end health
      (curl admin URL after recovery)
```

## Future extensions

The following test scenarios are planned but not yet implemented:

| Area | Test idea | Depends on |
|------|-----------|------------|
| SSL renewal | Issue a Let's Encrypt staging cert, verify renewal | `LETSENCRYPT_STAGING=1` environment |
| Compose site recovery | Stop a site's Docker Compose project, verify restart | Site must exist on the server |
| Database recovery | Stop MariaDB container, verify restart | Database module |
| Redis recovery | Stop Redis container, verify restart | Redis module |
| Backup validation | Create a backup, verify file exists, verify restore | Backup module |
| API integration | Call each API endpoint, verify JSON schema | Test framework |
| UI smoke test | Load each admin page, verify HTTP 200 | Headless browser |
| Upgrade path | Install old version, run update.sh, verify migration | Packaged releases |

## Related documents

- `scripts/selftest.sh` — the self-test script
- `docs/startup-architecture-review.md` — startup mode detection
- `docs/admin-recovery-architecture.md` — recovery design
- `docs/development/api-rules.md` — API testing guidelines
- `planning/TEST_ENVIRONMENT.md` — validation VM setup
