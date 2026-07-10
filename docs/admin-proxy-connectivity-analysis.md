# Admin Proxy Connectivity Analysis

## Problem

After changing the Web UI bind address from `0.0.0.0:8081` to
`127.0.0.1:8081`, the admin panel became unreachable through the
reverse proxy.  Direct external access was correctly blocked, but
proxy access broke too.

---

## 1. Complete request path

```
Browser
  ↓  HTTPS :443
Docker reverse proxy (containercp-proxy)
  ↓  http://<upstream>:8081
Web UI server (host process)
  ↓  /ui-api/api/*
Web UI server (reverse proxy to API)
  ↓  http://127.0.0.1:8080
ApiServer
```

## 2. Where it fails

The failure is between the nginx reverse proxy container and the
Web UI server on the host.

The nginx config for the admin domain contains:

```
location / {
    set $backend "http://<upstream>:8081";
    proxy_pass $backend;
}
```

The `<upstream>` is determined at daemon startup in
`ServiceRegistry::start()` (lines 217-229):

1. Default: `host.docker.internal`
2. Then tries Docker bridge gateway: `172.17.0.x`

The upstream resolves to the Docker host's bridge interface IP
(e.g. `172.17.0.1`).

## 3. Why it worked before and broke now

### Before: Web UI on `0.0.0.0:8081`

```
Inside the nginx container (containercp-proxy):

  nginx → http://172.17.0.1:8081/
    → Host's docker0 bridge interface (172.17.0.1)
    → Web UI socket bound to 0.0.0.0:8081
    → ACCEPT — 0.0.0.0 binds ALL interfaces, including docker0
```

### After: Web UI on `127.0.0.1:8081`

```
Inside the nginx container (containercp-proxy):

  nginx → http://172.17.0.1:8081/
    → Host's docker0 bridge interface (172.17.0.1)
    → Web UI socket bound to 127.0.0.1:8081
    → REJECT — 127.0.0.1 binds ONLY loopback, NOT docker0
```

`127.0.0.1` inside the host means "lo interface only".  The Docker
bridge (`docker0` or equivalent) is a separate network interface.
A socket bound to `127.0.0.1` does not accept connections arriving
on any other interface.

## 4. Docker networking details

```
┌──────────────────────────────────────────────────┐
│  Docker Host                                      │
│                                                   │
│  lo: 127.0.0.1           ← Web UI binds here      │
│                                                   │
│  docker0: 172.17.0.1     ← nginx sends requests   │
│                            HERE, but socket is on  │
│                            127.0.0.1 only          │
│                                                   │
│  eth0: <public IP>       ← external access blocked │
│                                                   │
│  ┌──────────────────────────┐                     │
│  │ containercp-proxy        │                     │
│  │ (nginx container)        │                     │
│  │                          │                     │
│  │ 127.0.0.1 → container lo │                     │
│  │ 172.17.0.2 → container   │                     │
│  │            eth0          │                     │
│  │                          │                     │
│  │ nginx proxies to:        │                     │
│  │ http://172.17.0.1:8081/  │                     │
│  └──────────────────────────┘                     │
└──────────────────────────────────────────────────┘
```

**Key insight:** The nginx container cannot reach `127.0.0.1` on the
host.  `127.0.0.1` inside a Docker container is the container's own
loopback, not the host's.

The proxy container reaches the host via `host.docker.internal`
(which resolves to the Docker gateway, e.g. `172.17.0.1`), or via
the detected bridge gateway IP.  These reach the host's `docker0`
interface — NOT the `lo` interface.

## 5. Affected modules

| Module | File | Role |
|--------|------|------|
| Web UI | `app/containercpd/main.cpp:133` | Sets bind address (recently changed) |
| Admin proxy setup | `libs/core/ServiceRegistry.cpp:210-290` | Creates admin proxy, sets upstream |
| Proxy config builder | `libs/proxy/ProxyConfigBuilder.cpp` | Generates nginx config with upstream |
| DNS/resolver | `libs/proxy/NginxProxyProvider.cpp:355` | `--add-host host.docker.internal:host-gateway` |

---

## 6. Possible solutions

### Option A: Bind to Docker bridge gateway IP (recommended)

Instead of `127.0.0.1`, detect the Docker bridge gateway at startup
and bind the Web UI server to that specific IP:

```cpp
std::string ui_bind = detect_docker_gateway();  // e.g. "172.17.0.1"
WebServer web_server(services, ui_bind, ui_port, api_port);
```

**Effect:**
- Web UI is NOT on `0.0.0.0` — not accessible from external networks
- Web UI IS reachable from the nginx container via the Docker bridge
- Non-Docker interfaces (eth0, etc.) don't have port 8081 open
- Requires external firewall to also block 8081 on docker0 (defense in depth)

**Risk:** If Docker uses a non-standard bridge IP (e.g. `172.18.0.1`
for custom networks), the detected address might not match. However,
the admin proxy already detects the gateway the same way.

### Option B: Bind to both `127.0.0.1` and the bridge IP

The WebServer would need to bind two sockets (or one socket on
`0.0.0.0` with iptables filtering).  More complex code.

### Option C: Use a Unix domain socket

Have the WebServer listen on a Unix socket (e.g.
`/tmp/containercp-web.sock`) and configure nginx to proxy via that
socket.  This eliminates TCP port binding entirely.  Requires nginx
to support `proxy_pass http://unix:/tmp/containercp-web.sock;`.

**Effect:** Most secure — no TCP port to attack.  But requires
changes to ProxyConfigBuilder and NginxProxyProvider.

### Option D: Restore `0.0.0.0` and use iptables

```bash
iptables -A INPUT -p tcp --dport 8081 ! -i docker0 -j DROP
```

This keeps the Web UI on all interfaces but drops non-Docker traffic.
Requires `CAP_NET_ADMIN` and iptables cleanup on shutdown.

### Option E: Firewall-based approach (no code change)

Keep `0.0.0.0:8081` and rely on the system firewall (iptables, nftables,
UFW) to block external access to port 8081 while allowing Docker bridge
traffic.  Document this as a deployment requirement.

## 7. Recommendation (implemented)

**Option A (bind to Docker bridge gateway IP)** was chosen as the
simplest and most secure approach:

| Criterion | Score |
|-----------|-------|
| No public exposure | ✅ Port 8081 not on external interfaces |
| Proxy can reach Web UI | ✅ Reachable via docker0 |
| No iptables dependency | ✅ Kernel-independent |
| No nginx config changes | ✅ Upstream stays the same |
| Backward compatible | ✅ Same detection used for admin upstream |
| Code change | Small: detect gateway IP → pass to WebServer |

Implementation steps:

1. Extract Docker gateway detection from `ServiceRegistry::start()`
   into a reusable helper (or reuse the upstream detection result)
2. Pass the gateway IP as the WebServer bind address instead of
   `"127.0.0.1"` or `"0.0.0.0"`
3. Keep the `host.docker.internal` check for the nginx upstream
   (already works)

## 8. Related documents

- `docs/admin-recovery-architecture.md` — recovery design
- `docs/runtime-architecture.md` — process management
- `planning/proxy-page-redesign.md` — proxy module audit
