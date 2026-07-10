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

## Running the self-test

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
