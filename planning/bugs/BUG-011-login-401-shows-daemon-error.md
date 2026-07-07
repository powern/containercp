# BUG-011: Web UI login returns 401 but frontend shows "daemon not running"

## Severity
Critical

## Description

The Web UI login page loads, but submitting credentials fails with
"Connection error. Is the daemon running?" even when the daemon is
running correctly.

The actual server response is HTTP 401 (Invalid credentials), but the
frontend treats any non-OK response as a network failure.

## Root cause

The `api()` function in `web/app.js` threw an error for any non-OK
HTTP status. The `doLogin()` catch block showed a generic "connection
error" message regardless of the actual error. HTTP 401 was not
distinguished from network failures.

## Additional issues discovered

1. `/ui-api/health` was protected by session check — health checks
   should be public.
2. No SHA-256 tests existed to verify password hashing correctness.
3. No regression tests for public vs protected route access.

## Fix

1. `api()` now parses JSON error body and sets `err.status` and
   `err.login_required` for 401 responses.
2. `doLogin()` shows "Invalid username or password" for status 401,
   and "Authentication service unavailable" for network errors.
3. `/ui-api/health` and `/api/health` are now public routes (added
   before the session check block).
4. Added SHA-256 test vectors (NIST, empty string, consistency).
5. Added route pattern regression tests.

## Files changed

- `libs/api/WebServer.cpp` — added public health route
- `web/app.js` — proper 401 vs network error handling
- `tests/test_api.cpp` — SHA-256 tests, route pattern tests

## Commit

a89e5e0 Fix Web UI login route authentication

## Validation

- [ ] POST /ui-api/auth/login without token reaches login handler
- [ ] GET /ui-api/auth/me without token returns login_required
- [ ] Protected /ui-api/* without token returns 401
- [ ] GET /ui-api/health is public (no session required)
- [ ] Login page shows "Invalid username or password" for bad credentials
