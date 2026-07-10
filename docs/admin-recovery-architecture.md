# Admin Panel Recovery Architecture

## Problem

When the Docker reverse proxy (`containercp-proxy`) is unavailable,
the administrator loses access to the control panel through the
public domain name.  There is currently no automatic recovery path.

## Current architecture

```
Current (incorrect) production state:

  Browser → http://server-ip:8081/   ← direct access, should not exist
    → WebServer (0.0.0.0:8081)
      → ApiServer (127.0.0.1:8080)

  Browser → https://admin.domain/
    → nginx (containercp-proxy, ports 80/443)
      → WebServer (0.0.0.0:8081)     ← same WebServer
        → ApiServer (127.0.0.1:8080)
```

The WebServer currently binds to `0.0.0.0:8081`, exposing the admin
panel directly on all interfaces.  This is acceptable for development
but not for production — port 8081 should be blocked by firewall and
all access should go through the proxy.

## Corrected production architecture

```
Normal operation (target):

  Browser → https://admin.domain/
    → nginx (containercp-proxy, ports 80/443)
      → WebServer (127.0.0.1:8081)   ← localhost only
        → ApiServer (127.0.0.1:8080)

  Port 8081 is NOT externally accessible.
  No direct access without the reverse proxy.

  If the proxy is down, admin is inaccessible.
```

The WebServer must bind to `127.0.0.1:8081` (localhost only) in
normal mode.  External access goes exclusively through the reverse
proxy on ports 80/443.

## Key constraints

| Constraint | Detail |
|------------|--------|
| Port 80 is privileged | Requires `CAP_NET_BIND_SERVICE` or root |
| Docker owns port 80/443 | `docker-proxy` binds host:80 → container:80 |
| Daemon runs as non-root | systemd `Type=simple`, no ambient capabilities |
| WebServer can bind dynamically | Socket can be created/closed at runtime |
| Proxy detection is reliable | `docker inspect containercp-proxy` via `central_proxy_running()` |

## Proposed architecture

### Overview

```
┌─────────────────────────────────────────────────────┐
│                   containercpd                       │
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │           Health Monitor (new thread)         │   │
│  │  Every 30s: docker inspect containercp-proxy  │   │
│  │  If DOWN → signal RecoveryServer to start     │   │
│  │  If UP   → signal RecoveryServer to stop      │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │             WebServer (unchanged)             │   │
│  │  Always: 127.0.0.1:8081                      │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │           RecoveryServer (new)                │   │
│  │  When proxy is DOWN:                          │   │
│  │    Bind 0.0.0.0:80 → forward to 127.0.0.1:8081│   │
│  │  When proxy is UP:                            │   │
│  │    Release port 80, stop forwarding           │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

### Recovery order (corrected)

Emergency access on port 80 must not be the first action.
The correct recovery chain is:

```
1. Proxy detected as unavailable
    ↓
2. Try to restore the proxy:
   a. Call ensure_central_proxy() to recreate the container
   b. Wait up to 15s for the container to reach "running" state
   c. If the proxy is now healthy → done, no emergency access needed
    ↓ (if proxy still unavailable)
3. Enable emergency access:
   a. Bind 0.0.0.0:80 (RecoveryServer or iptables DNAT)
   b. Admin accessible directly on port 80
    ↓ (proxy becomes available again later)
4. Disable emergency access:
   a. Release port 80
   b. Ensure proxy is running (ensure_central_proxy if needed)
   c. Normal operation resumes through proxy only
```

### Components

**WebServer** — binds to `127.0.0.1:8081` (localhost only) in normal
mode.  This is the primary admin interface, always reachable from
localhost.  NOT externally accessible.

**RecoveryServer** — new lightweight TCP forwarder:
- Binds `0.0.0.0:80` only when proxy is confirmed down AND proxy
  restoration has been attempted and failed
- Forwards accepted connections to `127.0.0.1:8081`
- Releases port 80 when proxy recovers
- Requires `CAP_NET_BIND_SERVICE` (added to systemd unit)

**Health Monitor** — new background thread:
- Every 30 seconds: `docker inspect containercp-proxy`
- On DOWN detection: first calls `ensure_central_proxy()` to attempt
  proxy restoration.  Only if that fails, activates RecoveryServer.
- On UP detection: signals RecoveryServer to release port 80

### Port ownership transition

```
Proxy UP:
  docker-proxy binds host:80 → container:80
  RecoveryServer: NOT listening
  WebServer: 127.0.0.1:8081 only

Proxy goes DOWN (crash/stop/deletion):
  Health Monitor detects DOWN (within 30s)
  → FIRST: call ensure_central_proxy() to try restoration
  → If proxy is back: done
  → If proxy still down: RecoveryServer binds 0.0.0.0:80
  Admin accessible via http://admin.domain/ (port 80)

Proxy comes back (restart/recreation):
  Health Monitor detects UP (within 30s)
  RecoveryServer closes port 80 socket
  WebServer returns to 127.0.0.1:8081 only
  Normal operation resumes
```

### Race condition prevention

A deterministic ownership transition is critical:

```
1. Health Monitor sees proxy UP → DOWN
2. Waits 2 seconds (lets docker-proxy fully release port 80)
3. RecoveryServer tries bind(0.0.0.0:80)
4. If bind fails → retry after 2s (up to 3 times)
5. Only then starts accepting connections

Reverse transition:
1. Health Monitor sees proxy DOWN → UP
2. RecoveryServer stops accepting new connections
3. Drains existing connections (up to 5s timeout)
4. Closes the listening socket
5. Waits 1 second (lets port 80 become available)
6. Only then returns success to Health Monitor
```

This ensures that at no point are both processes holding port 80.

### Startup sequence

```
1. daemon starts
2. Load persisted state (Storage)
3. Start WebServer on 127.0.0.1:8081 (localhost only)
4. Start ApiServer on 127.0.0.1:8080
5. ensure_central_proxy() → success/failure
6. If proxy fails:
   a. Log warning: proxy unavailable
   b. Health Monitor starts in "proxy down" state
   c. Immediately call ensure_central_proxy() again (retry)
   d. If still down: RecoveryServer binds 0.0.0.0:80
   e. Emergency admin access on port 80 (direct, not through proxy)
7. If proxy succeeds:
   a. Health Monitor starts in "proxy up" state
   b. RecoveryServer stays idle
   c. Admin accessible through proxy only (127.0.0.1:8081 not public)
8. Health Monitor begins periodic checks (every 30s)
```

### Required production fix (before recovery)

| File | Change |
|------|--------|
| `app/containercpd/main.cpp` | Change WebServer bind address from `"0.0.0.0"` to `"127.0.0.1"` so the admin panel is NOT externally accessible on port 8081. |

This single change eliminates direct external access to the admin
panel and forces all traffic through the reverse proxy.  Make this
change FIRST, before implementing any recovery mechanism.

### Recovery implementation

| File | Change |
|------|--------|
| `libs/api/RecoveryServer.h` | **New** — TCP forwarder class |
| `libs/api/RecoveryServer.cpp` | **New** — implementation |
| `libs/api/WebServer.h` | No changes needed (bind address set from constructor) |
| `libs/core/ServiceRegistry.h` | Add `recovery_server_` member, `health_monitor_` |
| `libs/core/ServiceRegistry.cpp` | Start/stop recovery in `start()` and `shutdown()` |
| `app/containercpd/main.cpp` | Keep `127.0.0.1:8081` for WebServer |
| `packaging/containercp.service` | Add `AmbientCapabilities=CAP_NET_BIND_SERVICE` |
| `CMakeLists.txt` | Add RecoveryServer.cpp |

### Edge cases

| Scenario | Behavior |
|----------|----------|
| Proxy was never created (fresh install) | ensure_central_proxy() called first. If it fails, RecoveryServer on port 80 |
| Proxy crashes while admin is in use | 30s max downtime, then RecoveryServer takes over |
| Both try port 80 simultaneously | RecoveryServer retries bind with backoff |
| Proxy comes back during RecoveryServer drain | Drain completes first, then proxy binds |
| Admin behind NAT with port forwarding | Port 80 works from outside (RecoveryServer binds 0.0.0.0) |
| Multiple daemon instances | Only one daemon runs (systemd singleton) |
| Daemon restarts while proxy is down | RecoveryServer re-binds port 80 on startup (step 6) |
| Firewall blocks port 80 | RecoveryServer binds but traffic doesn't reach it — admin uses 8081 directly |
| `CAP_NET_BIND_SERVICE` not granted | RecoveryServer fails to bind, logs warning, admin uses 8081 |

### Risks

1. **Systemd capability required** — `CAP_NET_BIND_SERVICE` is a change to the deployment. Without it, RecoveryServer cannot bind port 80. The 8081 fallback still works.

2. **TCP forwarder complexity** — A raw TCP forwarder needs careful buffer management, connection tracking, and timeout handling. Must not leak file descriptors.

3. **Proxy health detection latency** — Up to 30 seconds between proxy failure and recovery activation. Acceptable for a management panel.

4. **Connection draining** — Existing connections through the closed RecoveryServer socket would be terminated. Brief disruption during transition.

### Simpler alternative (recommended)

Instead of a TCP forwarder, use **iptables DNAT**:

```bash
# When proxy is down:
iptables -t nat -A CONTAINERCP_RECOVERY -p tcp --dport 80 \
  -j REDIRECT --to-port 8081

# When proxy is back:
iptables -t nat -D CONTAINERCP_RECOVERY -p tcp --dport 80 \
  -j REDIRECT --to-port 8081
```

Advantages:
- No new C++ code (shell out via `std::system` or `CommandExecutor`)
- No port conflict (iptables DNAT runs before routing)
- No connection tracking needed
- No drain logic
- Kernel handles the transition atomically

Disadvantages:
- Requires `CAP_NET_ADMIN` instead of `CAP_NET_BIND_SERVICE`
- iptables rules persist if daemon crashes without cleanup
- Docker's own iptables rules interact in complex ways

### Verified compatibility

Docker's `-p 80:80` creates iptables DNAT rules in the `DOCKER` chain.
A separate `CONTAINERCP_RECOVERY` chain with lower priority would not
conflict because:
- When docker-proxy is running: it handles port 80, our rule sits idle
- When docker-proxy stops: our rule directs traffic to 8081
- There is no bind conflict (both are DNAT rules, not socket binds)

The iptables approach is simpler and avoids the need for
`CAP_NET_BIND_SERVICE`.  It only needs `CAP_NET_ADMIN` for the
`iptables` command.

## Recommendation

Start with the **iptables DNAT approach**:

1. Add `CAP_NET_ADMIN` to the systemd unit
2. Add a `RecoveryManager` class that runs `iptables` to add/remove the DNAT rule
3. Monitor proxy health via `central_proxy_running()` every 30 seconds
4. Add/remove the DNAT rule on state transitions
5. Clean up the rule on daemon shutdown

If iptables proves unreliable (rule persistence, Docker interaction),
fall back to the TCP forwarder approach.

## Related documents

- `planning/proxy-page-redesign.md` — Proxy module audit
- `planning/product-roadmap.md` — version milestones
- `docs/runtime-architecture.md` — process management patterns
