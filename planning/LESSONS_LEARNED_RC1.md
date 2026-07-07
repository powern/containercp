# Lessons Learned — RC1 Validation

## Overview

The first Release Candidate validation on a clean Debian 13 (Trixie)
virtual machine ran in [Month Year]. Multiple critical issues were
discovered that had survived all prior testing — unit tests,
integration tests, and manual development-environment checks.

This document captures what went wrong, why it went undetected, and
what concrete changes to the development process will prevent
recurrence.

---

## What Problems Were Discovered?

### 1. Web UI inaccessible from external machines

The daemon bound its HTTP server to `127.0.0.1:8080`. From the
server console, everything worked. From any other machine, the Web UI
was unreachable.

**Impact:** Blocking — the validation checklist required
`http://<server-ip>:8080/` to load.

**Fix:** ARCH-003 introduced a second HTTP listener on `0.0.0.0:8081`
serving only static files. The REST API stayed on `127.0.0.1:8080`.

### 2. REST API exposed to the public network

Because the single listener bound to `127.0.0.1`, the fix for problem
#1 could not simply be "bind to 0.0.0.0". The REST API has no
authentication (AuthMiddleware is AllowAll). Exposing it to the
public network would be a security vulnerability.

**Impact:** Blocking — architectural conflict between Web UI access
and API security.

**Fix:** Separate listeners with different bind addresses. The public
listener explicitly rejects `/api/*` paths with HTTP 403.

### 3. Unit tests did not catch network-level issues

All unit tests communicate through in-memory mocks or direct method
calls. No test verifies that the daemon actually listens on the
configured address, that the port is reachable from localhost, or
that an external connection is properly rejected.

**Impact:** The test suite passed despite a fundamentally broken
deployment.

### 4. Integration tests ran on the developer's machine

Integration tests assumed the local environment matched production.
They did not test against a clean OS installation, so missing system
dependencies, permission issues, and filesystem layout assumptions
went undetected.

**Impact:** False confidence — green CI did not mean deployable.

### 5. No validation checklist item exercised external access

The existing validation checklist (`product-validation.md` at that
time) only checked `http://127.0.0.1:8080/`. There was no item for
external access, no item for port configuration, and no item for
checking that `/api/` was not publicly accessible.

**Impact:** The checklist itself was incomplete.

---

## Why Were They Not Detected Earlier?

| Root Cause | Explanation |
|------------|-------------|
| Developer environment bias | Every developer ran the daemon on their workstation. `127.0.0.1` worked fine. Nobody tested from a separate machine. |
| Unit test abstraction level | Unit tests test functions and classes, not deployed systems. The `ApiServer` constructor accepts a bind address; no test validated that the default was correct for production. |
| No clean-room validation | The project had never been built and run on a freshly installed OS. Dependencies were assumed present because they were present on dev machines. |
| Single-listener architecture | The original design used one HTTP server for both API and Web UI. Security concerns were deferred ("auth is a future Epic"). When VM testing forced the issue, the abstraction was not ready for two listeners. |
| Checklist blind spot | The validation checklist was written from the developer's perspective ("open browser to localhost"), not from the operator's perspective ("open browser from remote workstation"). |

---

## Which Unit Tests Should Be Added?

### 1. ApiServer bind address test

Verify that the default bind address is `127.0.0.1`. If the default
ever changes to `0.0.0.0`, the test fails and forces a security review.

### 2. WebServer bind address test

Verify that the default bind address is `0.0.0.0`.

### 3. WebServer API rejection test

Verify that a request to `/api/*` on the WebServer returns HTTP 403
before any handler logic runs.

### 4. Port parsing tests

Test `--api-port`, `--ui-port`, `CONTAINERCP_API_PORT`,
`CONTAINERCP_UI_PORT` argument and environment variable parsing with
valid, missing, and invalid values.

### 5. Daemon log assertion tests

Verify that the daemon logs the correct bind addresses on startup so
that operators can confirm the configuration at a glance.

---

## Which Integration Tests Should Be Added?

### 1. Socket reachability test

Start the daemon, then attempt HTTP connections to both ports from
the same host. Verify that port 8080 returns API responses and port
8081 returns HTML (the Web UI).

### 2. External access simulation test

Bind the WebServer to `0.0.0.0`, then connect from a separate
network namespace or via localhost to verify that the API is not
accessible on the public port.

### 3. Fresh install test

Build from a clean git clone on a minimal OS image (Docker container
or VM), install dependencies, build, start, and run the top-10
validation checks without any pre-seeded data.

### 4. Port conflict test

Start the daemon when one or both ports are already in use. Verify
graceful error handling and a clear log message.

### 5. Dual-port lifecycle test

Start the daemon, verify both ports respond, stop the daemon, verify
both ports are closed. Repeat.

---

## Which Validation Checklist Items Should Be Improved?

### Added (from this validation)

| # | Check | Rationale |
|---|-------|-----------|
| — | External Web UI loads on port 8081 | Direct result of discovered problem |
| — | Port 8081 rejects `/api/*` with 403 | Security boundary verification |
| — | Fresh clone on clean VM | Eliminates developer environment bias |
| — | No pre-existing runtime data | Ensures idempotent deployment |
| — | Docker daemon running | Dependency check before validation |

### Strengthened

- Every network-level check should now specify *both* ports
- Every CLI command that creates resources should verify the resource
  is actually visible via the API (not just via the CLI)
- Configuration checks should verify the value in the log output,
  not just assume defaults

---

## Which Architectural Decisions Proved Correct?

### 1. Daemon owns business logic

When the two-listener problem emerged, all changes were contained
inside `libs/api/`. The daemon's business logic, storage, and
providers required zero modification.

### 2. API-first design

The REST API was already a separate subsystem (`ApiServer`) with a
clean interface. Adding a sibling `WebServer` class was
straightforward. If the Web UI had been tightly coupled to the daemon
process, the refactor would have been much larger.

### 3. Configuration is editable without recompilation

The bind addresses and ports are configurable via environment
variables and command-line flags. Operators can change them without
rebuilding the binary.

### 4. Resources are always linked

The resource model (sites have domains, databases, users, etc.)
proved robust. No cascade issues were discovered during site
creation/removal validation on the VM.

---

## Which Assumptions Turned Out to Be Wrong?

| Assumption | Reality |
|------------|---------|
| "Localhost binding is fine because admins will use SSH" | SSH port forwarding is not documented, not obvious, and not practical for every operator. The product must work out of the box. |
| "Auth can wait until a later Epic" | Lack of auth forced a two-listener architecture that wouldn't have been necessary if auth had been present. Deferring auth had architectural consequences. |
| "Unit test coverage is sufficient for RC readiness" | Unit tests caught zero of the VM-discovered issues. The test suite gave false confidence. |
| "The developer's machine is representative of production" | It is not. Package versions, directory layouts, and running services all differ. |
| "Validation checklist completeness" | The checklist had a localhost-only blind spot. It tested what developers tested, not what operators needed. |

---

## What Process Improvements Resulted From This Validation?

### 1. Validation VM becomes mandatory

Every Epic now finishes with:

```
Implementation → Unit Tests → Integration Tests
→ Git Commit → Git Push → Deploy to Validation VM
→ Execute validation checklist → Fix issues
→ Repeat until validation passes → Close Epic
```

### 2. Official test environment documented

`planning/TEST_ENVIRONMENT.md` specifies the exact OS, packages, and
procedure. No more ad-hoc testing.

### 3. Validation checklist expanded

126 checks (was 114). New categories for VM preparation, external
access, and security boundaries.

### 4. Network-level tests added to backlog

Every new listener, port, or bind address must have reachability and
security tests before the feature is considered complete.

### 5. Developer environment bias now recognized as a risk

The development rules in `AGENTS.md` now require VM testing before
closing any Epic. Developers must not assume their workstation is
representative.

### 6. Security and production concerns moved earlier in the lifecycle

The ARCH-003 proposal explicitly evaluated the security implications
of opening a port to `0.0.0.0`. This happened *before* implementation,
not as a surprise during validation.

---

## Summary

The RC1 validation was painful but invaluable. It exposed five
critical issues that no amount of unit testing would have caught.
The root cause was not bad code but bad assumptions — about the
environment, about the checklist, and about what "ready" means.

The fixes are not just code changes. They are process changes:

1. **Real environments catch real bugs** — VMs are not optional
2. **Checklists must reflect operations, not development**
3. **Security cannot be deferred without architectural cost**
4. **Tests that pass on a laptop are not sufficient**

The project is now stronger for having done this validation.
Future Epics will benefit from the lessons learned here.

---

## RC1 closure

As of 2025-07-07, the core lifecycle validation has passed on a
clean Debian 13 (Trixie) Validation VM:

- 128 of 137 checklist items pass (9 stability items deferred)
- All 13 discovered bugs are fixed
- The Validation VM is now an established part of the development
  process
- Every new Epic will follow the same lifecycle:
  Architecture Proposal → Implementation → Tests → VM Validation

The lessons from RC1 have permanently changed how the project
validates:

1. **Real environments catch real bugs** — unit tests alone are
   never sufficient
2. **Developer environment bias** must be actively guarded against
3. **Checklists must reflect operations, not just development**
4. **Security cannot be deferred without architectural cost**
5. **Database directories must be created before writing to them**
