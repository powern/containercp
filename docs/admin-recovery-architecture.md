# Admin Panel Recovery Architecture

## Problem

When the Docker reverse proxy (`containercp-proxy`) is unavailable,
the administrator loses access to the control panel through the
public domain name.

## Current (incorrect) state

```
WebServer binds 0.0.0.0:8081  →  directly accessible from any interface
```

The admin panel is externally reachable on port 8081, which is not
acceptable for production.  Port 8081 should be blocked by firewall
and all access should go through the reverse proxy.

## Target production state

```
Browser → https://admin.domain/
  → nginx (containercp-proxy, ports 80/443)
    → WebServer (127.0.0.1:8081)   ← localhost only
      → ApiServer (127.0.0.1:8080)

Port 8081 is NOT externally accessible.
No direct access without the reverse proxy.
```

## Design decision: no port 80 / 0.0.0.0 fallback

The admin panel must NOT be exposed on port 80 or 0.0.0.0 under any
circumstances.  The recovery strategy is purely proxy self-healing:

- Do NOT implement a RecoveryServer (TCP forwarder on port 80)
- Do NOT use iptables DNAT for emergency access
- Do NOT change WebServer bind address dynamically
- Do NOT expose the admin panel directly

The only public entry point is the reverse proxy on ports 80/443.

## Recovery design

### Startup recovery

Every daemon start runs this sequence:

```
1. Start WebServer on 127.0.0.1:8081 (localhost only)
2. Start ApiServer on 127.0.0.1:8080
3. ensure_central_proxy():
   a. Check if containercp-proxy container exists and is running
   b. If missing or unhealthy:
      - Create the container with ports 80/443, SSL mount,
        host-gateway for host.docker.internal
      - Create default 404 config
      - Wait up to 15s for container to reach "running" state
   c. If container exists but is misconfigured (wrong network mode,
      missing SSL mount, missing port mapping):
      - Recreate the container
4. If central proxy is now running:
   a. Regenerate admin proxy config (always overwrites)
   b. Reload nginx
   c. Verify admin route is reachable (wget check, non-blocking)
   d. If SSL certificate exists for admin: re-attach it
5. If central proxy creation failed:
   a. Log critical error with recovery instructions
   b. Continue running on 127.0.0.1:8081
   c. Admin access requires SSH tunnel or local console
```

### Runtime recovery

A health monitor thread runs every 60 seconds:

```
1. Check containercp-proxy status (docker inspect)
2. If healthy → nothing to do
3. If unhealthy or stopped:
   a. Call ensure_central_proxy() to recreate/restart
   b. Regenerate admin proxy config
   c. Re-attach SSL certificate if applicable
   d. Reload nginx
   e. Log success or failure
4. If recovery fails after 3 consecutive attempts:
   a. Log critical error every check cycle
   b. Include clear manual recovery instructions
   c. Do NOT expose the admin panel directly
```

### Manual recovery (when automatic recovery fails)

The administrator can recover access through:

```bash
# Check daemon status
systemctl status containercpd

# Check proxy container
docker ps -a --filter name=containercp-proxy

# View daemon logs for recovery instructions
journalctl -u containercpd -n 50

# Restart the daemon (triggers full startup recovery)
systemctl restart containercpd

# Or manually recreate the proxy
docker rm -f containercp-proxy
systemctl restart containercpd
```

### Direct access for emergencies (SSH tunnel only)

If the proxy cannot be restored, the admin panel is still accessible
via SSH tunnel — no code change needed:

```bash
# From the administrator's workstation:
ssh -L 8081:127.0.0.1:8081 user@server
# Open http://localhost:8081/ in the local browser
```

This works because the WebServer always listens on 127.0.0.1:8081
regardless of proxy state.

## Required production fix

| File | Change |
|------|--------|
| `app/containercpd/main.cpp` | Change WebServer bind address from `"0.0.0.0"` to `"127.0.0.1"` |

This single change eliminates direct external access to the admin
panel.  Make this FIRST, before implementing any recovery mechanism.

## Implementation plan

| Step | Change | Purpose |
|------|--------|---------|
| 1 | `main.cpp`: bind WebServer to `127.0.0.1` | Close public 8081 access |
| 2 | `ServiceRegistry::start()`: add proxy retry on startup | Self-healing on boot |
| 3 | New health monitor thread in `ServiceRegistry` | Runtime proxy recovery |
| 4 | Improve `ensure_central_proxy()` resilience | Handle more failure modes |

## Rejected approaches

### RecoveryServer (TCP forwarder on port 80)
Would require `CAP_NET_BIND_SERVICE`, add C++ complexity for a TCP
forwarder, and risk port conflicts with Docker.  Rejected because
the simple approach (proxy self-healing + SSH tunnel fallback)
covers all realistic scenarios.

### iptables DNAT
Would require `CAP_NET_ADMIN`, iptables cleanup on crash, and risks
interaction with Docker's own iptables rules.  Rejected for the same
reason — proxy self-healing is simpler and safer.

### Dynamic socket rebinding
Would require stopping and restarting the WebServer socket at
runtime, risking connection drops and race conditions with the
Docker proxy.  Rejected.

## Edge cases

| Scenario | Behavior |
|----------|----------|
| Proxy container crashed | Detected within 60s, automatically recreated |
| Proxy container deleted | Detected within 60s, recreated by ensure_central_proxy() |
| Fresh install, proxy never created | Created during startup recovery (step 3) |
| Proxy fails to start (port conflict) | Logged as critical error, manual SSH tunnel needed |
| Daemon restarts while proxy is down | Startup recovery recreates proxy |
| SSL cert missing after recreation | Re-attached during startup recovery |
| Network down, Docker unavailable | Startup recovery fails, SSH tunnel fallback |
| Multiple consecutive failures (3+) | Critical log, no direct exposure, SSH tunnel only |

## Related documents

- `planning/proxy-page-redesign.md` — Proxy module audit
- `docs/runtime-architecture.md` — process management patterns
- `docs/development/api-rules.md` — API design rules
