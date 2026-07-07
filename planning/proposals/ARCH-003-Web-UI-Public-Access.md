# ARCH-003: Secure Web UI External Access

Status: Draft

## Problem

The daemon's REST API and Web UI are served on the same listener,
bound to `127.0.0.1:8080`. This means:
- Web UI is not accessible from other machines on the network
- Any external access requires SSH tunneling or a reverse proxy
- Administrators cannot easily verify the Web UI from their browser

## Security Constraint

The REST API must NOT be exposed directly to the public network.
All `/api/*` endpoints are currently unauthenticated. Exposing them
would allow anyone on the network to create, modify, or delete
resources without authorization.

## Current Architecture

```
containercpd
  └── ApiServer (127.0.0.1:8080)
      ├── REST API (/api/*)
      └── Static files (/web/*)
```

## Proposed Architecture

```
containercpd
  ├── ApiServer (127.0.0.1:8080)
  │   └── REST API (/api/*) — localhost only
  └── WebServer (0.0.0.0:8081)
      └── Static files (/web/*) — public
          └── JavaScript fetch() → 127.0.0.1:8080/api/*
```

The Web UI JavaScript connects to the API on localhost from the
browser. This works when:
- The browser is on the same machine (localhost to localhost)
- A reverse proxy forwards /api/* to the local API
- Future authentication is implemented

For remote browser access, administrators can:
1. Use SSH port forwarding: `ssh -L 8080:localhost:8080 user@server`
2. Set up nginx reverse proxy (documented)
3. Open port 8081 for Web UI and access API through the browser on
   the server itself

## Options Considered

### A) Bind containercpd to 0.0.0.0
Rejected. Exposes the unauthenticated API to the network.

### B) Separate Web UI listener (recommended for v0.5 RC)
A separate `WebServer` binds to 0.0.0.0:8081 and serves only static
files. API stays on 127.0.0.1:8080. The Web UI JavaScript calls the
API on localhost:8080, which works when the browser is on the server.

### C) nginx reverse proxy in front
Recommended for production but adds a dependency. Documented as
future improvement.

### D) Authentication layer
Future epic. Not included in v0.5 RC.

## Implementation Plan

1. Create `libs/api/WebServer.h/.cpp` — simplified HTTP server that
   only serves static files and rejects API paths
2. Modify `ApiServer` to accept a bind address parameter
3. Update `containercpd/main.cpp` to start both servers
4. Document the access model

## Validation Plan

1. Start daemon, verify API on 127.0.0.1:8080 responds
2. Verify Web UI on 0.0.0.0:8081 serves index.html
3. Verify 0.0.0.0:8081/api/* returns forbidden
4. Verify existing tests pass
5. Verify CLI still works through UNIX socket
