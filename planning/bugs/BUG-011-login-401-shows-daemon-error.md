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

## Follow-up issue: password file desync

Even after the route fix, login still fails because the generated
password in `/etc/containercp/ui-password` does not match the hash
stored in `auth_users.db`.

**Root cause:**

`AuthService::initialize()` generates a random password on first start
and stores its SHA-256 hash in `auth_users.db`. It logs the password
but does NOT write the plaintext to the password file.

The old Basic Auth code (`WebServer::load_password()`, now removed)
also generated a random password and wrote it to
`/etc/containercp/ui-password` — but it generated a *different* random
password. On the next daemon restart, `AuthService::initialize()` sees
that `auth_users.db` already has an admin user and does nothing, so
the password file and the stored hash remain permanently desynchronized.

**Fix:**

1. On first start: `AuthService::initialize()` now writes the
   generated plaintext password to `/etc/containercp/ui-password` so
   operators can discover it.

2. On every subsequent start: if admin has `must_change_password=true`
   and the password file exists, `AuthService::initialize()` re-reads
   the file, re-hashes the password, and updates `auth_users.db` to
   match. This keeps the file as the source of truth for the temporary
   password and prevents desync.

3. The file is read with `std::getline` which strips the trailing
   newline. An additional `\r` trim handles any Windows line endings.

**Files changed:**

- `libs/auth/AuthService.cpp` — write password file on first start,
  sync hash from file on subsequent starts
- `tests/test_api.cpp` — password hash consistency test, password
  file round-trip test

**Commit:**

e469d0d (to be updated)

## Validation

- [ ] POST /ui-api/auth/login without token reaches login handler
- [ ] GET /ui-api/auth/me without token returns login_required
- [ ] Protected /ui-api/* without token returns 401
- [ ] GET /ui-api/health is public (no session required)
- [ ] Login page shows "Invalid username or password" for bad credentials
- [ ] Password from `/etc/containercp/ui-password` successfully
      authenticates admin on first login
- [ ] After password change, file-based sync stops (must_change_password
      is false)
