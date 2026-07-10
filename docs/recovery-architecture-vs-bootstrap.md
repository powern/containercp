# Recovery Architecture vs Bootstrap

## Bootstrap analysis

### What Bootstrap does

Bootstrap is the **initial deployment mode**.  It activates when:

- `server_hostname` is empty (not configured), OR
- `setup_completed` flag file is not `"1"`

In bootstrap mode, ContainerCP starts a minimal HTTP server on port 80
that serves a two-step Setup Wizard:
1. Save server hostname
2. Complete setup (writes `setup_completed=1`, exits with code 99)

After exit, systemd restarts the daemon into normal mode.

Bootstrap does NOT:
- Create the proxy container
- Generate nginx configs
- Create admin proxy entries
- Validate or reload nginx

These operations happen in the **normal startup sequence** via:

| Operation | Owner | When |
|-----------|-------|------|
| Proxy container creation | `ensure_central_proxy()` | `main.cpp` line 167 |
| Admin proxy entry + config | `ServiceRegistry::start()` | `main.cpp` line 180 |
| SSL re-attachment | `ServiceRegistry::start()` | `main.cpp` line 180 |
| nginx reload | `proxy_provider_.reload()` | After each config change |

### What Bootstrap's `mark_setup_incomplete()` does

When the proxy fails to start, `main.cpp` calls `mark_setup_incomplete()`
which writes `"0"` to the flag file.  This does NOT trigger an immediate
restart — it only affects the **next** daemon start.

## Recovery analysis

Recovery needs to handle the same proxy failures at **runtime**
(after startup, while the daemon is running normally).

Operations needed for recovery:

| Recovery operation | Already exists in startup? | Where? |
|-------------------|---------------------------|--------|
| Create/recreate proxy container | ✅ | `ensure_central_proxy()` in `NginxProxyProvider` |
| Create admin proxy entry | ✅ | `ServiceRegistry::start()` step 3c |
| Regenerate admin nginx config | ✅ | `ServiceRegistry::start()` step 3d |
| Reload nginx | ✅ | `proxy_provider_.reload()` |
| Re-attach SSL cert | ✅ | `ServiceRegistry::start()` step 3g |
| Regen all HTTPS configs | ✅ | `ServiceRegistry::start()` step 4 |

## Duplicated responsibilities

**All operations that Recovery needs already exist in startup code.**

The risk is that a Recovery module would reimplement:

- Docker network creation (already in `ensure_central_proxy()`)
- nginx config generation (already in `ProxyConfigBuilder`)
- Admin proxy entry creation (already in `ServiceRegistry::start()`)
- Config validation with rollback (already in `attach_certificate()`)

**Recovery must NOT duplicate these.**  It should call the same
functions that the startup sequence already uses.

## Proposed ownership

| Operation | Owner | Reused by Recovery? |
|-----------|-------|---------------------|
| Proxy container lifecycle | `NginxProxyProvider::ensure_central_proxy()` | ✅ Call directly |
| Admin proxy entry + config | `ServiceRegistry::start()` (steps 3c-3g) | ✅ Refactor into `ServiceRegistry::ensure_admin_proxy()` |
| nginx config validation | `NginxProxyProvider::validate_nginx_config()` | ✅ Call directly |
| nginx reload | `NginxProxyProvider::reload()` | ✅ Call directly |
| SSL re-attachment | `ServiceRegistry::start()` (step 3g) | ✅ Part of `ensure_admin_proxy()` |
| All-site HTTPS sync | `ServiceRegistry::start()` (step 4) | ✅ Call method directly |
| Health monitoring | **New**: `RecoveryManager` thread | New — detects proxy state changes |

## Proposed call graph

```
                    ┌─────────────────────┐
                    │   RecoveryManager    │
                    │   (new thread)       │
                    │                      │
                    │  Every 60s:          │
                    │  docker inspect      │
                    │  containercp-proxy  │
                    └────────┬────────────┘
                             │ unhealthy
                             ▼
              ┌──────────────────────────────┐
              │  RecoveryManager::recover()  │
              │                              │
              │  1. Call ensure_central_proxy│
              │     (reuses existing logic)  │
              │                              │
              │  2. Call ensure_admin_proxy  │
              │     (extracted from start()) │
              │                              │
              │  3. Call sync_all_https()    │
              │     (extracted from start()) │
              │                              │
              │  4. Verify reachability      │
              └──────────────────────────────┘
                             │
                             ▼
              ┌──────────────────────────────┐
              │  Reused services             │
              │                              │
              │  ensure_central_proxy()       │
              │    → NginxProxyProvider       │
              │                              │
              │  ensure_admin_proxy()         │
              │    → ServiceRegistry          │
              │    → ProxyConfigBuilder       │
              │    → NginxProxyProvider       │
              │                              │
              │  sync_all_https()             │
              │    → CertificateStore        │
              │    → NginxProxyProvider      │
              └──────────────────────────────┘
```

## Proposed refactoring

### Extract `ServiceRegistry::ensure_admin_proxy()`

Current `ServiceRegistry::start()` has steps 3a-3g inline.  Extract
them into a public method so both startup and recovery can call it:

```cpp
// Before:
void ServiceRegistry::start() {
    // ... step 1-2 ...
    // step 3a-3g (inline, ~90 lines)
    // ... step 4-5 ...
}

// After:
void ServiceRegistry::start() {
    // ... step 1-2 ...
    ensure_admin_proxy();           // extracted
    sync_all_https_configs();       // extracted
    // ... step 5 ...
}

core::OperationResult ServiceRegistry::ensure_admin_proxy() {
    // steps 3a-3g (moved here)
}
```

### Add `RecoveryManager` (minimal)

A new lightweight class in `libs/core/` or `libs/proxy/`:

```cpp
class RecoveryManager {
public:
    RecoveryManager(ServiceRegistry& services, logger::Logger& log);

    void start();   // starts health check thread
    void stop();    // stops health check thread

private:
    void check_loop();
    bool is_proxy_healthy();
    core::OperationResult recover();

    ServiceRegistry& services_;
    logger::Logger& log_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
```

The `recover()` method simply calls existing services — no new logic:

```cpp
core::OperationResult RecoveryManager::recover() {
    // 1. Restore proxy container
    auto result = services_.proxy_provider().ensure_central_proxy();
    if (!result.success) return result;

    // 2. Restore admin proxy config
    services_.ensure_admin_proxy();  // new extracted method

    // 3. Sync all HTTPS configs
    services_.sync_all_https_configs();  // new extracted method

    return {true, "Recovery completed"};
}
```

### Health check detection

```cpp
bool RecoveryManager::is_proxy_healthy() {
    // Simple: docker inspect containercp-proxy
    // Returns true if running and ports are mapped
    // Reuse NginxProxyProvider::central_proxy_running() if made accessible
}
```

## Recommendation

1. **Extract** `ensure_admin_proxy()` and `sync_all_https_configs()`
   from `ServiceRegistry::start()` — makes them reusable

2. **Add** a minimal `RecoveryManager` — only the health check thread
   and retry loop.  The actual recovery calls existing methods.

3. **Do NOT duplicate** any proxy creation, config generation, or
   nginx reload logic — the existing code already handles all of it.

4. **Keep Bootstrap unchanged** — Bootstrap only handles initial
   setup.  Recovery handles runtime proxy failures.  They share
   services but have different triggers.

## Recovery retry policy

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Health check interval | 60 seconds | Proxy failures are infrastructure events, not sub-second |
| Max retries before cooldown | 3 consecutive failures | Prevents infinite loops |
| Cooldown period | 300 seconds (5 minutes) | After 3 failures, wait before retrying again |
| Between retries | No delay (first retry immediate) | Fast recovery for transient failures |
| Degraded state | After 3 failures + cooldown | Logged as critical error |
| Resumption | After cooldown expires | Will attempt recovery again at next health check |

### Flow

```
check_interval=60s
max_retries=3
cooldown=300s
fail_count=0

LOOP:
  sleep(60)
  if proxy_healthy:
    fail_count = 0
    continue

  fail_count++
  if fail_count > max_retries:
    log_critical("Proxy recovery failed 3 times, cooling down for 300s")
    sleep(cooldown)
    fail_count = 0   // reset after cooldown
    continue

  recover()  // calls ensure_central_proxy + ensure_admin_proxy + sync_all_https
```

## Bootstrap/Recovery synchronization

| Question | Answer |
|----------|--------|
| Bootstrap and Recovery race? | **No.** Bootstrap runs in a separate process (exit 99 spawns a new daemon). If the daemon is in bootstrap mode, it never reaches normal mode where Recovery would activate. |
| Can Bootstrap still be running when Recovery starts? | **No.** Bootstrap is a separate process. When it completes, it exits with code 99, systemd restarts the daemon, the new process enters normal mode, and only then does Recovery become active. |
| Is startup synchronous? | **Yes.** `main.cpp` executes sequentially. `ServiceRegistry::start()` runs synchronously before `main.cpp` enters the accept loop. |
| Race condition between startup and recovery? | **No.** `RecoveryManager` would start only after `ServiceRegistry::start()` completes. All proxy/config initialization finishes before the first health check runs. |
| Does Recovery need to wait for Bootstrap to complete? | **No.** They are separate processes. Bootstrap exits before the normal-mode daemon starts. |
| Should Recovery activate only after `ServiceRegistry::start()`? | **Yes.** Recovery must wait until all startup initialization (including `ensure_admin_proxy()` and `sync_all_https_configs()`) has completed. This is guaranteed because `RecoveryManager::start()` would be called at the end of `ServiceRegistry::start()` or after it. |

### Startup timeline

```
t=0     main.cpp starts (normal mode, post-bootstrap)
t=0.1   ApiServer starts on 127.0.0.1:8080
t=0.1   WebServer starts on gateway_ip:8081
t=0.2   ensure_central_proxy() — proxy container
t=0.5   ServiceRegistry::start():
          ├── ACME config
          ├── Certificate crash recovery
          ├── ensure_admin_proxy()      ← extracted method
          ├── sync_all_https_configs()  ← extracted method
          ├── job_executor_.start()
          └── renewal_scheduler_.start()
t=0.5   accept loop begins
t=0.5   RecoveryManager::start()       ← would start health checks here
t=60    First health check (if proxy is healthy, nothing to do)
```

No synchronization mechanism is needed because execution is strictly
sequential — the accept loop only starts after `start()` returns.

## Related documents

- `docs/admin-recovery-architecture.md` — recovery design
- `docs/admin-proxy-connectivity-analysis.md` — proxy networking
- `planning/proxy-page-redesign.md` — proxy module audit
