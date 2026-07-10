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

## Related documents

- `docs/admin-recovery-architecture.md` — recovery design
- `docs/admin-proxy-connectivity-analysis.md` — proxy networking
- `planning/proxy-page-redesign.md` — proxy module audit
