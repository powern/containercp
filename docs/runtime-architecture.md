# Runtime Architecture

## Overview

The runtime subsystem is ContainerCP's single source of truth for
container runtime operations (restart, stop, start, status, etc.).

It is **not** owned by the Sites module.  All modules that need
container runtime operations (Sites, Databases, Docker, Mail, Cache,
Queue, …) consume the same runtime subsystem.

```
Sites module      Databases module      Docker module     …
     │                   │                   │
     └────────┬──────────┴──────────┬────────┘
              │                     │
     SiteRuntimeManager    DatabaseRuntimeManager   (thin bridges)
              │                     │
              └──────────┬──────────┘
                         │
              RuntimeActionExecutor   (HOW)
                         │
                  CommandExecutor     (safe fork/exec)
                         │
                   docker compose
```

## Module responsibilities

| Layer | Responsibility | Example |
|-------|---------------|---------|
| `ServiceRole` | Abstract roles that describe WHAT a service does | Frontend, PHP, Database, Cache |
| `RuntimeActionExecutor` | Knows HOW to execute compose actions | `docker compose restart web` |
| `SiteRuntimeManager` | Sites-specific bridge; maps site actions to roles | `restart-web` → Frontend → `web` |
| `CommandExecutor` | Safe process execution | `fork()`+`execvp()` with `poll()` |

## Ownership

| Data / capability | Owner |
|-------------------|-------|
| Container status | `RuntimeActionExecutor` (via `docker compose ps`) |
| Compose execution | `RuntimeActionExecutor` (via `CommandExecutor`) |
| Action → role mapping | `ServiceRole` (pure functions) |
| Role → compose service | `ServiceRole` (pure functions) |
| Site → compose directory | `SiteRuntimeManager` (uses `sites_root_`) |
| Site container status | `SiteRuntimeManager` (calls `RuntimeActionExecutor`) |
| Site action dispatch | API handler → `SiteRuntimeManager` → `RuntimeActionExecutor` |

## Runtime flow

```
HTTP POST /api/runtime/<site_id>/restart-web
  │
  ├─ 1. API handler validates site_id and action
  │
  ├─ 2. SiteRuntimeManager::services_for_action("restart-web")
  │       └─ ServiceRole::roles_from_action("restart-web")
  │            → {ServiceRole::Frontend}
  │       └─ ServiceRole::roles_to_compose_services({Frontend})
  │            → {"web"}
  │
  ├─ 3. JobExecutor creates async job
  │
  ├─ 4. Job task calls:
  │       RuntimeActionExecutor::restart_services(compose_dir, {"web"})
  │       └─ compose_action(compose_dir, "restart", {"web"})
  │            └─ CommandExecutor::run({
  │                 "docker", "compose",
  │                 "--project-directory", compose_dir,
  │                 "restart", "web"
  │               })
  │                 └─ fork() + execvp() + poll(stdout, stderr)
  │
  └─ 5. Job status updated: completed / failed
```

## Service roles

Roles abstract away implementation details so the runtime layer
never cares which software provides a capability.

| Role | Compose service | Example software |
|------|----------------|-----------------|
| `Frontend` | `web` | Apache httpd, Nginx, LiteSpeed, Caddy |
| `PHP` | `php` | PHP-FPM |
| `Database` | `mariadb` | MariaDB, PostgreSQL |
| `Cache` | `redis` | Redis, Memcached |

**Future roles:** `Mail`, `Queue`, `Worker`, `Scheduler`, `Proxy`.

## Adding a new runtime action

1. Add the action string to `valid_actions()` — currently in
   `SiteRuntimeManager` (Sites-specific), eventually each module
   defines its own.

2. Add the action → roles mapping in `ServiceRole::roles_from_action()`.

3. If the action targets a new service role, add the role to the
   `ServiceRole` enum and implement `role_to_compose_service()`.

4. If the action uses a new compose subcommand (e.g. `stop`, `start`),
   `RuntimeActionExecutor::compose_action()` already supports any
   subcommand — just pass it as the `subcommand` parameter.

5. Wire the action in the API layer or the relevant module's bridge.

**No changes needed to `RuntimeActionExecutor` or `CommandExecutor`
for simple action additions.**  They are already generic.

## Adding a new module that needs runtime operations

1. Create a thin bridge class (like `SiteRuntimeManager`) that:
   - Defines the module's valid actions
   - Maps actions to `ServiceRole`s
   - Calls `RuntimeActionExecutor` for actual execution

2. The bridge should NOT reimplement compose execution or process
   management — those belong to `RuntimeActionExecutor` and
   `CommandExecutor`.

3. Wire the bridge into `ServiceRegistry`.

Example for a future Database module:

```
class DatabaseRuntimeBridge {
    std::vector<std::string> services_for_action(action);  // → {"mariadb"}
    core::OperationResult execute(compose_dir, action);
};
```

## File layout

```
libs/runtime/
├── Runtime.h                       # Abstract interface (legacy)
├── CommandExecutor.h/.cpp          # Safe fork/exec with poll
├── RuntimeActionExecutor.h/.cpp    # Docker Compose execution
├── ServiceRole.h/.cpp              # Role definitions and mappings
├── SiteRuntimeManager.h/.cpp       # Sites-specific bridge
├── DockerRuntime.h/.cpp            # Legacy compose management
└── PortManager.h/.cpp              # Port allocation (not runtime)
```

## Key principles

1. **Single source of truth** — runtime operations live in
   `libs/runtime/`.  No other module implements Docker logic.

2. **Roles, not names** — the runtime operates on abstract service
   roles.  Compose service names are an implementation detail.

3. **Safe execution** — all Docker commands run via `CommandExecutor`
   (`fork()`+`execvp()` with `poll()` for concurrent I/O).  No
   `std::system()`, no shell injection.

4. **Async by default** — runtime actions execute via `JobExecutor`
   so the API never blocks.  Job status is pollable.

5. **Module bridges are thin** — `SiteRuntimeManager` is ~120 lines.
   It maps, validates, and delegates.  It does not implement Docker
   logic.

6. **Backward compatibility** — the API actions (`restart-web`,
   `restart-php`, etc.) are stable.  Internal role mappings can
   evolve without breaking consumers.
