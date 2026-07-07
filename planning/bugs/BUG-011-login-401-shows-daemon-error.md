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

After the bootstrap consistency fix (commit e469d0d), the password
file and stored hash now match — but authentication still fails.

**Final root cause: JSON field offset one-off error.**

The custom JSON parser in `WebServer::handle_auth_login()` used
`uname_pos += 11` to skip past the field name `"username":"`. But
`"username":"` is 12 characters, so the opening quote `"` of the
value was included in the extracted username. The result was
`"admin"` (with quote) instead of `admin`, which never matched the
stored username. Same bug for `"password":"` (12 chars, offset was
11) and `"old_password":"` / `"new_password":"` (16 chars, offset
was 15).

**Timeline of bugs:**

| # | Commit | Bug | Fix |
|---|--------|-----|-----|
| 1 | c0dfa12 | login route returned 401 (wrong frontend error) | a89e5e0 — proper 401 handling |
| 2 | c0dfa12 | password file never written | 46437ad — write + sync file |
| 3 | c0dfa12 | JSON field offset off by 1 | this commit |

**Fix:**

1. `"username":"` offset: 11 → 12 (was including opening quote)
2. `"password":"` offset: 11 → 12 (same bug)
3. `"old_password":"` offset: 15 → 16 (same bug)
4. `"new_password":"` offset: 15 → 16 (same bug)
5. Added null-check and empty-check for extracted values.
6. Added detailed auth failure logging (unknown user, disabled,
   password mismatch, malformed request, missing credentials).
7. Added `hash_password → verify_password` round-trip unit test.

**Files changed:**

- `libs/api/WebServer.cpp` — fix JSON field offsets, add logging
- `libs/auth/AuthService.cpp` — add auth failure logging
- `tests/test_api.cpp` — hash round-trip test

**Commits:**

- 46437ad Fix Web UI bootstrap password consistency (file sync)
- (this) Fix Web UI password verification (JSON offset)

## Follow-up issue: password reset after restart

After successful password change (must_change_password=false), a
daemon rebuild and restart caused the password to reset to a new
temporary password.

**Investigation:** The code logic correctly persists `auth_users.db`
after password change. `AuthService::initialize()` only creates a new
admin when `users.empty()`, and only syncs from the password file
when `must_change_password=true`. On restart with
`must_change_password=false`, no changes should occur.

Root cause could not be conclusively identified through code review.
Possible causes under investigation:
- The `auth_users.db` file might not be flushed/synced to disk after
  the password change write
- A filesystem issue on the validation VM
- A stale `auth_users.db` from a previous code version with different
  format

**Defensive hardening:**
- Added startup logging to show whether admin was loaded from storage
  or created fresh, and the value of `must_change_password`
- Added persistence round-trip regression test that simulates:
  bootstrap → save → load → change password → save → reload →
  verify must_change_password=false and new password works

## Validation

- [ ] POST /ui-api/auth/login with valid credentials returns 200 + token
- [ ] POST /ui-api/auth/login with wrong password returns 401
- [ ] POST /ui-api/auth/login with disabled user returns 401
- [ ] POST /ui-api/auth/login with missing fields returns 400
- [ ] Daemon log shows reason for each failed login attempt
- [ ] Daemon log does not contain the password
- [ ] After password change, file-based sync stops
