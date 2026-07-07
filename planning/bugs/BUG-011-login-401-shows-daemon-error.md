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

## Follow-up issue: password reset after restart (attempt 2)

After successful password change (must_change_password=false), a
daemon rebuild and restart caused the password to reset to a new
temporary password. Previous fix attempt (46437ad) added file sync
but did not resolve the issue.

**Investigation approach (this commit):**

Instead of guessing, comprehensive diagnostics have been added to
capture the exact state at every stage of the auth lifecycle:

### Startup diagnostics (AuthService::initialize)
```
Auth: db path = /srv/containercp/database/auth_users.db
Auth: db exists = yes
Auth: users loaded = 1
Auth: user 'admin' enabled=1 must_change=0 hash_present=yes role=admin
Auth: DECISION: skip — admin loaded from storage, must_change=false
```

### Password change diagnostics (AuthService::change_password)
```
Auth: change_password for 'admin' must_change before=1
Auth: change_password saved to /srv/containercp/database/auth_users.db
  must_change after=0
```

### CLI diagnostics
```
containercp auth debug
  Auth users: 1
    username=admin enabled=1 must_change=0 hash_present=yes role=admin
```

### Likely root cause that can now be diagnosed

With these diagnostics, the exact failure point can be determined:

1. If **startup log** shows `users loaded = 0` despite the password
   change having logged a successful save — the file is either not
   being written to the expected path, or is being overwritten by
   another code path.

2. If **startup log** shows `users loaded = 1` but `must_change=1`
   despite the password change logging `must_change after=0` — the
   save is writing stale data, or a subsequent operation reverts it.

3. If **startup log** shows `db exists = no` — the file is at a
   different path than expected.

**Added this commit:**
- Comprehensive startup logging (db path, file existence, user count,
  each user's enabled/must_change/hash_present fields)
- Post-change verification (reloads auth_users.db after save to
  confirm it was written correctly)
- CLI `containercp auth debug` command for runtime diagnostics
- Updated startup DECISION logging (create/sync/skip) to clarify
  exactly which code path was taken
- `AuthService` now has access to `config().database_dir()` for
  logging the auth_users.db path

## Validation

- [ ] POST /ui-api/auth/login with valid credentials returns 200 + token
- [ ] POST /ui-api/auth/login with wrong password returns 401
- [ ] POST /ui-api/auth/login with disabled user returns 401
- [ ] POST /ui-api/auth/login with missing fields returns 400
- [ ] Daemon log shows reason for each failed login attempt
- [ ] Daemon log does not contain the password
- [ ] After password change, file-based sync stops
