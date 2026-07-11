# Mail Hardening Plan

Audit date: 2026-07-11
Baseline HEAD: `1bd6893ba839124b31d115dc7cb5234cf211251d`
Baseline tests: 146 passed, 677 assertions

---

## Found problems overview

| # | Area | Severity | Status |
|---|------|----------|--------|
| A | LMTP published on host port 24; all ports hardcoded to 0.0.0.0 | HIGH | ✅ Fixed |
| B | Router uses HTTP 404 as internal "not matched" signal | HIGH | ✅ Fixed |
| C | No config validation before Postfix reload; no rollback on failure | HIGH | ✅ Fixed |
| D | Self-signed cert required manually on fresh install; mail activate fails | HIGH | ✅ Fixed |
| E | Alias runtime behavior never validated end-to-end | MEDIUM | ✅ Fixed |
| F | No end-to-end mail routing tests (local, relay, split) | MEDIUM | ✅ Fixed |
| G | Health API does not report cert status, apply status, or detailed readiness | MEDIUM | ✅ Fixed |
| H | Wrong dates in docs (2025 vs 2026); stale status claims | LOW | ✅ Fixed |
| I | passwd file and DKIM keys created without explicit chmod 0600 | MEDIUM | ✅ Fixed |
| J | `relay_host` written to transport maps without format validation | MEDIUM | ⬜ Deferred |
| K | `MailPasswordHasher` uses non-thread-safe `crypt()` vs `crypt_r()` | MEDIUM | ⬜ Deferred |

---

## Stage A — Secure mail network model

**Risk:** HIGH — LMTP port 24 published to host; all ports hardcoded to 0.0.0.0

**Root cause:**
- Docker compose generation hardcodes `0.0.0.0:24->24/tcp` etc.
- Postfix uses `lmtp:127.0.0.1:24` but Postfix and Dovecot are in separate containers — `127.0.0.1` refers to Postfix's own container, not Dovecot's
- LMTP should only be available inside the Docker network

**Fix:**
1. Remove port 24 from docker-compose Dovecot publish list
2. Change LMTP transport from `lmtp:127.0.0.1:24` to `lmtp:containercp-mail-dovecot:24` (Docker DNS name)
3. Make published ports configurable via config or env, with safe defaults
4. Bind ports to `127.0.0.1` by default (not `0.0.0.0`) — admin must opt in for external access
5. Document firewall requirements per port

**Files:**
- `libs/mail/providers/DockerMailProvider.cpp` — compose generation, transport maps, postfix config
- `libs/mail/providers/DockerMailProvider.h` — optional port config
- `libs/core/Config.h/.cpp` — optional mail port config
- `docs/mail-module-plan.md` — network architecture update
- `docs/api/API_REFERENCE.md` — port documentation

**Tests:**
- Verify generated docker-compose.yml has no host port 24
- Verify `lmtp:` uses container name, not 127.0.0.1
- Verify `docker compose config` passes
- Verify LMTP not accessible from host

**Acceptance:**
- `nc -z 127.0.0.1 24` returns connection refused
- Postfix can still deliver to Dovecot (verified via Docker network)
- Ports 25, 465, 587, 143, 993 are published on `127.0.0.1` by default
- Redis port 6379 NOT published to host

**Dependencies:** None

---

## Stage B — Fix Router architecture

**Risk:** HIGH — alias and DKIM endpoints were dead code due to prefix routing bug; current fix uses 404 as control flow

**Root cause:**
- `Router::dispatch()` returns first matching prefix handler's response
- Handlers return 404 for sub-paths they don't handle
- 404 is overloaded: "route not matched" vs "resource not found"

**Fix:**
Introduce a `RouteMatch` result type that distinguishes:
- `Matched` — handler processed the request (use response as-is)
- `NotMatched` — prefix matched but sub-path doesn't belong to this handler

Replace the 404 fallthrough with explicit `NotMatched` propagation.
Also consolidate mail domain sub-routes into a single dispatcher to reduce prefix handler count.

**Files:**
- `libs/api/Router.h` — new `RouteResult` type or `RouteMatch` enum
- `libs/api/Router.cpp` — updated dispatch logic
- `libs/api/ApiServer.cpp` — all prefix handlers return `NotMatched` where appropriate
- `tests/test_router.cpp` — new tests

**Tests:**
- Route not matched (prefix matches but sub-path unknown)
- Matched route with missing resource (HTTP 404 from handler)
- Mailbox POST route
- Alias POST route
- DKIM POST route
- Invalid domain ID (404 with error message)
- Unknown subresource (e.g. `/api/mail/domains/1/unknown`)
- Same-prefix routes resolve correctly
- HTTP 404 from handler is NOT interpreted as "try next handler"

**Acceptance:**
- `POST /api/mail/domains/1/mailboxes` creates mailbox
- `POST /api/mail/domains/1/aliases` creates alias
- `POST /api/mail/domains/1/dkim/generate` generates DKIM key
- `POST /api/mail/domains/1/unknown` returns 404 with error message
- No dead prefix handlers in the codebase

**Dependencies:** None

---

## Stage C — Transactional runtime apply

**Risk:** HIGH — bad config silently breaks mail stack; no validation before reload

**Root cause:**
- `RuntimeSynchronizer` calls `write_configs()` → `reload()` without validation
- No staging directory for candidate configs
- No rollback on failure
- No `postfix check` or `dovecot -n` before applying

**Fix:**
1. Add `validate_configs()` to `DockerMailProvider` that runs:
   - `docker exec containercp-mail-postfix postfix check`
   - `docker exec containercp-mail-dovecot doveconf -n` (via `doveconf -c`)
2. Rewrite `write_configs()` to write to a unique staging directory first
3. Add `apply_staged_configs()` that atomically promotes staging → live
4. On validation failure: discard staging, keep current, return error
5. On reload failure: restore previous known-good config, return error
6. Add mutex/lock to serialise concurrent applies
7. Track last successful apply result in memory
8. Report apply status in health endpoint

**Files:**
- `libs/mail/providers/DockerMailProvider.h` — new methods
- `libs/mail/providers/DockerMailProvider.cpp` — staging, validation, apply, rollback
- `libs/mail/providers/MailProvider.h` — interface update if needed
- `libs/mail/providers/DockerMailProvider.cpp` — `is_running()` rename/refine
- `libs/core/ServiceRegistry.cpp` — mail sync callback uses new transactional API
- `tests/test_mail_provider.cpp` — new tests

**Tests:**
- Successful apply (staging → validation → promote → reload → health check)
- Generation failure (bad domain data)
- Validation failure (invalid config syntax)
- Reload failure (Postfix rejects config)
- Health check failure after reload
- Rollback restores previous config
- Concurrent apply is serialised
- Re-apply after failure works

**Acceptance:**
- Bad config never reaches live Postfix/Dovecot
- Previous known-good config is preserved and restored on failure
- Health reports apply failure clearly
- No partial apply state

**Dependencies:** Stage B (Router fix) recommended first for clean error reporting

---

## Stage D — Self-signed cert fallback on fresh install

**Risk:** HIGH — mail activation fails if no SSL cert at `/srv/containercp/ssl/0/`

**Root cause:**
- TLS config hardcodes `/srv/containercp/ssl/0/fullchain.pem`
- No certificate exists on fresh install
- Dovecot refuses to start without valid cert

**Fix:**
1. During `prepare_environment()`, check for certificate at expected path
2. If missing, generate a self-signed cert automatically via OpenSSL
3. Set secure permissions (0600) on generated private key
4. Mark health as `degraded` when using self-signed cert (via details)
5. Add cert info to health response: source, expiry, hostname, self-signed status
6. When a real cert is later provisioned, detect and promote to `ok`

**Files:**
- `libs/mail/providers/DockerMailProvider.cpp` — cert check + fallback in `prepare_environment()`
- `libs/mail/providers/DockerMailProvider.h` — `ensure_certificate()`
- `libs/core/ServiceRegistry.cpp` — populate cert details in health report
- `docs/mail-module-plan.md` — cert lifecycle documentation

**Tests:**
- Fresh install: no cert → self-signed created automatically
- Existing cert: not overwritten
- Self-signed cert has 0600 permissions
- Health reports `degraded` with self-signed cert info
- After real cert provisioned, health reports `ok`

**Acceptance:**
- `POST /api/mail/activate` succeeds on clean system without manual cert creation
- Dovecot starts and stays healthy
- `/api/mail/health` shows cert source, expiry, self-signed status
- Private key file has `0600` permissions

**Dependencies:** Stage A (LMTP fix) for clean network; Stage C for validation

---

## Stage E — Full alias verification

**Risk:** MEDIUM — alias API works but runtime delivery never validated

**Root cause:**
- Alias map file generated but Postfix lookup and SMTP delivery not verified
- No validation of alias destinations for loops or ambiguity

**Fix:**
1. Add `postmap -q` lookup test after alias file generation
2. Validate destination is not identical to source (self-loop)
3. Validate destination domain exists in managed domains or is external
4. Add integration test with real Postfix `postmap -q` command
5. Test runtime sync after alias CRUD

**Files:**
- `libs/mail/providers/DockerMailProvider.cpp` — optional alias validation
- `libs/api/ApiServer.cpp` — self-loop check in alias create handler
- `tests/test_mail_provider.cpp` — alias runtime tests

**Tests:**
- Enabled alias: `postmap -q` returns destination
- Disabled alias: `postmap -q` returns not found
- Alias to local mailbox: resolves correctly
- Alias to external address: resolves correctly
- Multiple aliases with same source: all resolved
- Duplicate source+destination: rejected (409)
- Self-loop (source == destination): rejected (400)
- Deleted domain: alias creation returns 404

**Acceptance:**
- `postmap -q info@testmail.local texthash:/path/virtual_aliases` returns destination
- Disabled alias lookup returns not found
- Sync fires on create, update, enable/disable, delete

**Dependencies:** Stage C (transactional apply) ensures alias config is validated

---

## Stage F — End-to-end mail routing tests

**Risk:** MEDIUM — unit tests pass but actual SMTP delivery never verified

**Root cause:**
- No integration tests that send actual SMTP messages
- Routing behavior (local, relay, split) only tested at config level

**Fix:**
Create a documented runtime test suite (`scripts/test-mail-routing.sh`) that:
1. Starts clean environment
2. Activates mail module
3. Creates domain + mailbox + alias
4. Sends SMTP message via `swaks` or raw `nc`
5. Verifies delivery via Dovecot (`doveadm fetch`)
6. Tests ExternalRelay mode with a local fake upstream
7. Tests SplitM365 transport map precedence
8. Tests alias expansion
9. Tests open relay prevention
10. Cleans up

**Files:**
- `scripts/test-mail-routing.sh` — new runtime test script
- `docs/testing.md` — test documentation
- `README.md` — reference to test script

**Tests:**
- Local primary: SMTP → LMTP → Maildir
- Alias: SMTP → alias → destination mailbox
- External relay: SMTP → transport map → smarthost
- Split M365: local user → LMTP, unknown → relay
- Open relay: external domain rejected
- TLS delivery (if available)

**Acceptance:**
- Runtime test script passes on clean environment
- Each scenario verifies actual message delivery, not just config generation

**Dependencies:** Stage A (LMTP), Stage C (validation), Stage D (certs), Stage E (aliases)

---

## Stage G — Health API and recovery

**Risk:** MEDIUM — health endpoint reports container status but not system readiness

**Root cause:**
- Health check only uses `docker inspect` for container running state
- No certificate status, no apply status, no config validation state
- No recovery actions defined

**Fix:**
1. Add per-service process-level checks (not just container running):
   - Postfix: `docker exec ... postfix status`
   - Dovecot: `docker exec ... doveadm who`
   - Redis: `docker exec ... redis-cli ping`
2. Add cert info to mail health details: source, expiry, self-signed
3. Add apply status: last_successful, last_failed, pending
4. Add config revision tracking
5. Extend status model to: `ok`, `warning`, `degraded`, `error`, `inactive`
6. Implement safe recovery: container restart with backoff, max retries

**Files:**
- `libs/mail/providers/DockerMailProvider.cpp` — extended `check_health()`
- `libs/core/ServiceRegistry.cpp` — cert details in health
- `libs/runtime/HealthReport.h` — optional severity enum
- `docs/api/API_REFERENCE.md` — health response documentation

**Tests:**
- All services healthy → status `ok`
- One service down → status `degraded`
- Config validation failed → status `error`
- Self-signed cert → status `warning` or `degraded`
- Recovery restarts stopped container
- Recovery respects max retries and backoff

**Acceptance:**
- Health endpoint reflects real system readiness
- Self-signed cert clearly visible in health response
- Config apply failures reported
- Recovery does not infinite-loop on persistent failures

**Dependencies:** Stage C (apply status), Stage D (cert status)

---

## Stage H — Documentation cleanup

**Risk:** LOW — wrong dates and stale claims mislead developers

**Root cause:**
- Runtime validation documented as 2025-07-10 (should be 2026-07-10)
- Various status claims may be stale after fixes

**Fix:**
1. Fix all date references from 2025 to 2026 where appropriate
2. Review all mail-related docs for accuracy
3. Add clear status labels: `implemented`, `unit tested`, `integration tested`, `runtime validated`, `production ready`, `known limitation`
4. Update project-status.md to reflect current state

**Files:**
- `docs/testing.md` — dates, validation history
- `docs/mail-module-plan.md` — stage statuses
- `docs/mail-routing-design.md` — any stale references
- `docs/api/API_REFERENCE.md` — accuracy check
- `planning/project-status.md` — current state
- `CHANGELOG.md` — hardening entries

**Tests:** None (documentation only)

**Acceptance:**
- No 2025 dates in mail-related docs
- Status labels are honest and specific
- Known limitations clearly documented

**Dependencies:** All other stages (document the final state)

---

## Additional audit findings

### I — File permissions hardening

**Risk:** MEDIUM — passwd file with password hashes created with default umask

**Fix:**
- Set explicit `0600` on `passwd` file after creation
- Set explicit `0600` on DKIM private key after generation
- Set explicit `0600` on self-signed cert private key

**Integrated into:** Stage D (cert handling), Stage C (staging directory)

### J — relay_host input validation

**Risk:** MEDIUM — `relay_host` written verbatim to transport maps

**Fix:**
- Validate `relay_host` format: must be `hostname` or `hostname:port`
- Reject IP addresses without brackets (injection vector)
- Reject newlines, quotes, shell metacharacters

**Integrated into:** Stage C (config generation hardening)

### K — Thread-safe password hashing

**Risk:** MEDIUM — `::crypt()` uses static buffer, not thread-safe

**Fix:**
- Replace `::crypt()` with `::crypt_r()` in `MailPasswordHasher`
- Add test for concurrent password hashing

**Integrated into:** Stage C (separate commit)

---

## Stage dependency graph

```
A (LMTP/ports) ──→ C (validation) ──→ D (certs) ──→ E (aliases) ──→ F (E2E tests)
       │                │                                │
       └── B (Router) ──┘                                │
                                                          │
                                          G (health/recovery)
                                                          │
                                          H (docs cleanup)
```

Plan: A → B → C → D → E → F → G → H

---

## Commit strategy

```
1. docs(mail): add mail hardening implementation plan  ✅ ba4ebbb
2. fix(mail-network): isolate LMTP, use container DNS, bind ports to 127.0.0.1 ✅ 08c7502
3. refactor(api-router): consolidate mail domain sub-routes, remove 404 fallthrough ✅ 12a6706
4. feat(mail-runtime): add transactional config validation and rollback ✅ 3c35b5f
5. feat(mail-tls): automate fresh-install certificate fallback ✅ e9fe762
6. test(mail-alias): add self-loop rejection and postmap lookup validation ✅ f9799e2
7. test(mail-routing): add end-to-end routing test script + health improvements ✅ a071dbb
8. feat(mail-health): report apply, certificate, and runtime readiness ✅ a071dbb
9. docs(mail): fix dates, update statuses, document known limitations ✅ a071dbb
```

---

## Final verification

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

Runtime smoke test:
1. Clean mail environment
2. Activate module (should auto-create self-signed cert)
3. Create local domain, mailbox, alias
4. Verify config apply and health
5. Send local test message, verify LMTP delivery
6. Test alias resolution via SMTP
7. Test external relay
8. Break candidate config, verify validation blocks apply
9. Verify rollback preserves previous config
10. Verify no open relay, no LMTP on host, no secrets in logs
