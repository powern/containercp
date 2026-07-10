# Startup Architecture Review

## 1. ServiceRegistry::start() — current state

```cpp
void ServiceRegistry::start() {
    // (a) ACME environment config
    const char* staging_env = std::getenv("LETSENCRYPT_STAGING");
    cert_provider_->set_staging(staging_env != nullptr && std::string(staging_env) == "1");

    // (b) Certificate crash recovery
    for (auto site_id : cert_store_.enumerate()) {
        // move ISSUING → ERROR
    }

    // (c) Admin proxy + HTTPS sync (extracted, public)
    ensure_admin_proxy();
    sync_all_https_configs();

    // (d) Background services
    job_executor_.start();
    renewal_scheduler_.start();
}
```

**Verdict:** `start()` is already a high-level orchestrator.  Each section
(a-d) is either a single call or a focused inline block that belongs to
startup only (ACME env, crash recovery).  No further extraction is needed.

The two extracted methods (`ensure_admin_proxy()` and `sync_all_https_configs()`)
are the correct public API for future RecoveryManager.

---

## 2. Bootstrap — clean separation

Bootstrap is:
- A **separate process** (exit 99 → systemd restart)
- Responsible for: hostname setup wizard
- Does NOT: create proxy, generate configs, start services

**Verdict:** Bootstrap is cleanly separated.  No Bootstrap logic has
leaked into runtime startup.  No changes needed.

---

## 3. Startup ownership table

| # | Operation | Location | Owner | For Recovery? |
|---|-----------|----------|-------|---------------|
| 1 | PID file + single instance | `main.cpp` lines 75-78 | Daemon lifecycle | No |
| 2 | Bootstrap mode decision | `main.cpp` line 93 | StartupManager | No |
| 3 | Application singleton | `main.cpp` line 101 | Application | No |
| 4 | Load persisted resources | `ServiceRegistry` constructor | Storage | No |
| 5 | Port scanning (site dirs) | `ServiceRegistry` constructor | Runtime | No |
| 6 | Auth initialization | `ServiceRegistry` constructor | AuthService | No |
| 7 | Directory creation | `main.cpp` lines 118-124 | Filesystem | No |
| 8 | **REST API start** | `main.cpp` lines 127-129 | ApiServer | No (always running) |
| 9 | **Web UI start** | `main.cpp` lines 135-141 | WebServer | No (always running) |
| 10 | UNIX socket creation | `main.cpp` lines 143-164 | DaemonApp | No |
| 11 | **ensure_central_proxy()** | `main.cpp` lines 171-183 | **NginxProxyProvider** | ✅ **Reuse directly** |
| 12 | DaemonApp init | `main.cpp` line 185 | DaemonApp | No |
| 13 | ACME environment | `ServiceRegistry::start()` | CertProvider | No |
| 14 | Certificate crash recovery | `ServiceRegistry::start()` | CertificateStore | No |
| 15 | **ensure_admin_proxy()** | `ServiceRegistry::start()` → **public method** | **ServiceRegistry** | ✅ **Reuse directly** |
| 16 | **sync_all_https_configs()** | `ServiceRegistry::start()` → **public method** | **ServiceRegistry** | ✅ **Reuse directly** |
| 17 | Job executor start | `ServiceRegistry::start()` | JobExecutor | No |
| 18 | Renewal scheduler start | `ServiceRegistry::start()` | RenewalScheduler | No |

Operations 11, 15, and 16 are the three that Recovery needs.
All three are already public and reusable.

---

## 4. Startup dependencies (required order)

```
1. Application singleton + Storage load
2. Directory creation
3. REST API start                         (background thread, non-blocking)
4. Web UI start                           (background thread, non-blocking)
5. UNIX socket creation
6. ensure_central_proxy()                 ═════  Recovery starts here
7. DaemonApp init
8. ACME config + cert crash recovery
9. ensure_admin_proxy()                   ═════  Recovery starts here
10. sync_all_https_configs()              ═════  Recovery starts here
11. Job executor + renewal scheduler
12. Accept loop (main thread)
```

Dependencies:

```
[Storage] ← no dependency on any server
  → [REST API] ← needs Storage only
  → [Web UI] ← needs Storage only
  → [ensure_central_proxy] ← needs Docker only
    → [ensure_admin_proxy] ← needs proxy + gateway
      → [sync_all_https_configs] ← needs proxy + cert_store
        → [renewal_scheduler] ← needs all of the above
```

The dependency chain is linear and explicit.  No hidden cycles.

---

## 5. Recovery readiness

Every operation Recovery needs is already callable through existing
public methods:

| Recovery operation | Public method | Location |
|-------------------|---------------|----------|
| Recreate proxy container | `services.proxy_provider().ensure_central_proxy()` | NginxProxyProvider |
| Regenerate admin proxy | `services.ensure_admin_proxy()` | ServiceRegistry |
| Sync HTTPS configs | `services.sync_all_https_configs()` | ServiceRegistry |
| Validate nginx config | `dynamic_cast<NginxProxyProvider*>(...)` or use internal | NginxProxyProvider |
| Reload nginx | `services.proxy_provider().reload()` | ProxyProvider |

**No additional extraction is needed.**  The architecture is ready for
RecoveryManager.

---

## 6. Startup audit findings

### No duplicated initialization

- Directories are created once in `main.cpp`
- Resources are loaded once in the ServiceRegistry constructor
- Proxy is ensured once at startup (line 173)
- Admin proxy is configured once in `start()` (line 350)
- HTTPS configs are synced once in `start()` (line 351)

### No hidden side effects

- `ensure_admin_proxy()` is idempotent — safe to call multiple times
- `sync_all_https_configs()` iterates certificates and regenerates configs — redundant but safe
- `ensure_central_proxy()` detects existing container and skips recreation — idempotent

### No unnecessary coupling

- ServiceRegistry does not depend on ApiServer or WebServer (they are created
  in `main.cpp` before ServiceRegistry is initialized)
- DaemonApp does not depend on recovery logic
- Bootstrap does not depend on normal mode logic

### No blockers for RecoveryManager

The only recommendation:

**Make `NginxProxyProvider::central_proxy_running()` public** (currently private).
RecoveryManager needs to check if the proxy is healthy without triggering
a recreation.  Alternatively, RecoveryManager can run `docker inspect`
directly via CommandExecutor (which it already has access to).

---

## 7. Bootstrap mode safety / production mode detection

### What decides Bootstrap vs normal mode

`StartupManager::needs_bootstrap()` checks two conditions (both must pass
for normal mode):

1. **`hostname` is not empty** — the admin server hostname must be
   configured (in `SERVER_HOSTNAME` env var or
   `/srv/containercp/server_hostname` file).

2. **`setup_completed` flag is `"1"`** — the file
   `/srv/containercp/setup_completed` must contain `"1"`.

If either condition is false, the daemon enters bootstrap mode (runs
the Setup Wizard on port 80).

### What marks ContainerCP as initialized

The system is considered initialized when:

- The admin hostname has been configured (via the Setup Wizard,
  which writes it to `/srv/containercp/server_hostname`).
- The Setup Wizard marks setup as complete (writes `"1"` to
  `/srv/containercp/setup_completed`).

### Why initialized systems must not enter Bootstrap mode

If an already-initialized system enters bootstrap mode:

1. The bootstrap server tries to bind port 80.
2. In production, the containercp-proxy container already owns port 80.
3. `bind()` fails → bootstrap crashes.
4. systemd restarts the daemon → same crash loop.

This makes the system completely unreachable through the admin domain.

### How recovery/proxy interacts with Bootstrap

The `mark_setup_incomplete()` call was previously used in the proxy
failure path in `main.cpp`.  This was dangerous because:

- A transient proxy failure (e.g. Docker not ready yet, container
  starting slowly) would permanently mark setup as incomplete.
- On the next restart, the system would enter bootstrap mode and
  crash-loop because port 80 is occupied.
- RecoveryManager already handles proxy recovery at runtime.

**Current rule:** proxy failure must NEVER trigger bootstrap mode.
RecoveryManager handles proxy self-healing.  The `setup_completed`
flag is only written by the Setup Wizard (Bootstrap), never by
runtime startup/recovery code.

## 8. Recommendation

The architecture is ready for RecoveryManager implementation.

Three public methods cover all recovery operations:

```
RecoveryManager::recover():
  1. services.proxy_provider().ensure_central_proxy()
  2. services.ensure_admin_proxy()
  3. services.sync_all_https_configs()
```

No additional refactoring is required.  RecoveryManager can be implemented
as a new class with a health check thread that calls these three methods
on failure detection.

## Related documents

- `docs/recovery-architecture-vs-bootstrap.md` — retry policy and sync analysis
- `docs/admin-recovery-architecture.md` — recovery design
- `planning/proxy-page-redesign.md` — proxy module audit
