# Runtime Management Phases (1–5)

Detailed changelog entries for the runtime management feature development.

---

## 2025-07-09 | `da38415` | Phase 1 cleanup: compose_dir path join, badge update, poll-based executor, HTTPS metadata path

## 2025-07-09 | `9d62391` | Phase 1 cleanup: remove duplicated HTTPS implementation, reuse CertificateStore

## 2025-07-09 | `ad14a91` | Phase 2: Prepare runtime action architecture (stub)

## 2025-07-09 | (pending commit) | Phase 3: Real restart actions via RuntimeActionExecutor + JobExecutor

### Fixed all 6 issues from review

**1. UI column indexing** — replaced numeric cell offsets with `data-rt-id` and `data-rt-service` attributes so runtime badges find their cells by stable selectors regardless of column order.

**2. Docker Compose project resolution** — `container_status()` now accepts the real site compose directory (`sites_root_ + domain`) and uses `docker compose --project-directory <dir>` instead of an inferred project name, fixing the path resolution to match actual site layout.

**3. Error semantics** — Docker/Compose command failures (non-zero exit code) now return `"Error"` instead of `"Stopped"`. Only `exited`/`paused`/`created` states map to `"Stopped"`. Exit codes and stderr are logged.

**4. HTTPS status** — `https_status_from_metadata()` reads `<ssl_root>/<site_id>/metadata.json` and returns `Active`, `Expiring`, `Expired`, `Disabled`, `Error`, or `Issuing`. Expiry checked against current time with 30-day warning threshold.

**5. Command execution safety** — added `CommandExecutor` class using `fork()`/`execvp()` with separate pipes for stdout/stderr and `waitpid()` for exit code. No shell string concatenation, no shared temp files, no race conditions.

**6. SiteManager::find_by_id** — added for domain resolution in API handler.

### Files changed
- `libs/runtime/CommandExecutor.h` — new: safe fork/exec command runner
- `libs/runtime/CommandExecutor.cpp` — new: implementation
- `libs/runtime/SiteRuntimeManager.h` — rewritten API: `get_status(site_id, domain)`, `https_status_from_metadata`
- `libs/runtime/SiteRuntimeManager.cpp` — rewritten: docker compose ps via `--project-directory`, error-logged failures, HTTPS metadata parsing
- `libs/site/SiteManager.h` — added `find_by_id(uint64_t)`
- `libs/site/SiteManager.cpp` — implemented `find_by_id`
- `libs/api/ApiServer.cpp` — API handler resolves domain via `find_by_id`, passes to get_status, includes `https` in response
- `web/app.js` — Web/PHP/HTTPS columns use `data-rt-id`/`data-rt-service` selectors; added HTTPS column with badge
- `CMakeLists.txt` — added `CommandExecutor.cpp`
- `tests/CMakeLists.txt` — added `CommandExecutor.cpp`, `SiteRuntimeManager.cpp`, `test_runtime.cpp`
- `tests/test_runtime.cpp` — new: 9 tests for CommandExecutor + 8 for https_status_from_metadata
- `tests/test_managers.cpp` — added `find_by_id` test
- `CHANGELOG.md` — this entry

### User-visible behavior
- Sites table shows Web, PHP, and HTTPS status columns with color-coded badges
- Correct status: stopped containers show "Stopped", missing compose projects show "Error"
- HTTPS status reflects real cert state including expiry warnings

### Validation results
- Build: zero warnings
- Tests: 96 pass, 0 fail
- Manual logic verified via https_status_from_metadata unit tests

### Risks
- `docker compose ps` and `docker inspect` now run for every site on table load (parallel). For large fleets, add caching or polling in a future phase.
- HTTPS expiry check uses ISO 8601 parsing via sscanf — handles standard UTC format only.

---


## 2025-07-09 | (pending commit) | Phase 3: Real restart actions via RuntimeActionExecutor + JobExecutor

### Architecture
- **`RuntimeActionExecutor`** — new global layer that knows HOW to execute Docker Compose actions. Uses `CommandExecutor` (safe `fork()`/`execvp()` with `poll()`). Method: `restart_services(compose_dir, services)` runs `docker compose restart <services>` against the site's compose project directory.
- **`SiteRuntimeManager`** — now only maps WHAT: `services_for_action("restart-web")` returns `{"web"}`, `"restart-php"` → `{"php"}`, `"restart-all"` → `{}` (empty = all compose services). Removed the old `execute_action` stub.
- **API handler** — `POST /api/runtime/<site_id>/<action>` creates an async Job via `JobExecutor`, submits a task that calls `RuntimeActionExecutor::restart_services()`, returns `job_id`. Status code 202.
- **Backend agnostic** — service name is always `web` for both Apache and Nginx backends. No hardcoded Apache assumptions.

### Files changed
- `libs/runtime/RuntimeActionExecutor.h` — new: global compose action executor
- `libs/runtime/RuntimeActionExecutor.cpp` — new: implementation with compose_action + restart_services
- `libs/runtime/SiteRuntimeManager.h` — added `services_for_action()`, removed `execute_action()`
- `libs/runtime/SiteRuntimeManager.cpp` — implemented `services_for_action()`, removed `execute_action()` stub
- `libs/api/ApiServer.cpp` — POST handler now async via JobExecutor, returns job_id
- `libs/core/ServiceRegistry.h` — added `RuntimeActionExecutor` include, accessor, member
- `libs/core/ServiceRegistry.cpp` — initialized `runtime_action_executor_`, added accessor
- `CMakeLists.txt` — added `RuntimeActionExecutor.cpp`
- `tests/CMakeLists.txt` — added `RuntimeActionExecutor.cpp`
- `tests/test_runtime.cpp` — replaced `execute_action` tests with `services_for_action` + `RuntimeActionExecutor` tests

### Test results
- 111 tests pass (14 new), 0 fail, 0 warnings

### Risks
- `RuntimeActionExecutor` uses `docker compose --project-directory` — requires Docker Compose v2+.
- Async jobs run in a thread pool. If the daemon shuts down while a restart job is running, the job is cancelled/marked failed.

### Non-goals (Phase 3 scope only)
- No UI buttons yet
- No DB/Redis restart
- No recreate/pull/stop actions
- No SSL changes

### Risks

- Existing sites with host-port allocation retain old compose template
- Fresh site creation uses new template (always overwritten on disk)
- Deprecated PortManager not yet removed — cleanup planned

---
Back to [CHANGELOG.md](../../CHANGELOG.md)
