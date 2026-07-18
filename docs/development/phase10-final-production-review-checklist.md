# Phase 10 Final Production Review Checklist

## Scope

Production-code changes made during test fixes must be reviewed for correctness against their documented contracts. Seven items identified.

## Baseline

- Reviewed HEAD: `bbc730e8c45ef7a12e0a8dc732f450ce38d783e4`
- Reported baseline: 580/580 tests, 3473/3473 assertions
- Phase 11 excluded
- Production deployment excluded

## Review items

### P10-F01 — Version format contract

- [ ] Complete

**Problem:** `safe_version()` in `LegacyArchive.cpp` was changed in commit `bbc730e` to accept two-component versions (`v1.0-rc1`). The prior approved archive version contract required exactly three numeric components: `v<major>.<minor>.<patch>[-suffix]`. The change was introduced to satisfy a test without documenting the contract change.

**Affected files/functions:**
- `libs/storage/LegacyArchive.cpp` — `LegacyArchive::safe_version()`
- `libs/storage/LegacyArchive.h` — declaration
- `tests/test_migration.cpp` — test at line 1458

**Approved contract:** `v<major>.<minor>.<patch>[-suffix]` — exactly three numeric components. All project version strings (`v0.6.0`, `v0.7.0`) are three-component. Suffix must start with `-` followed by alphanumerics/`.`/`-`.

**Implementation plan:**
1. Restore `safe_version()` to require exactly three numeric components
2. Add `v1.0-rc1` → `v1.0.0-rc1` in the failing test
3. No production code change needed — test was wrong

**Acceptance criteria:**
- `safe_version("v0.6.0")` → true
- `safe_version("v1.2.3-rc1")` → true
- `safe_version("v1.0-rc1")` → false (only 2 components)
- `safe_version("v1.0.0-rc1")` → true
- All old archive tests still pass

**Required tests:**
- `v0.6.0` accepted
- `v1.2.3-rc1` accepted
- `v1.0.0-rc1` accepted
- `v1.0-rc1` rejected (2-component)
- `v`, `v0`, `v0.`, `v0..6`, `v0.6.` rejected
- `v0.6.0.1` rejected
- `v0.6.0-` rejected
- `0.6.0` rejected
- `v0/6/0`, `v0\6\0` rejected
- Whitespace, control chars, non-ASCII rejected

**Implementation evidence:**

Commit SHA: _____________

Focused test result: _____________

### P10-F02 — Archive path traversal policy

- [ ] Complete

**Problem:** `normalize_archive_identity_path()` in `LegacyArchive.cpp` was changed in commit `bbc730e` to call `lexically_normal()` BEFORE checking for `..` components. As a result, `/archive/a/../b` normalizes to `/archive/b` and is accepted, silently bypassing the traversal check.

**Affected files/functions:**
- `libs/storage/LegacyArchive.cpp` — `LegacyArchive::normalize_archive_identity_path()`
- `tests/test_migration.cpp` — test at line 2095

**Approved contract:** Reject any raw path component equal to `..`. Only after rejection, apply `lexically_normal()`. Reject any remaining `..` after normalization. Remove redundant `.` components and trailing separators.

**Implementation plan:**
1. Restore pre-normalization `..` check (reject raw `..`)
2. Apply `lexically_normal()`
3. Reject remaining `..` post-normalization
4. Update test: `normalize_archive_identity_path("a/../b")` → empty (rejected)
5. Add explicit tests for various traversal patterns

**Acceptance criteria:**
- `/archive/root` accepted
- `/archive/root/` normalized to `/archive/root`
- `/archive/./root` normalized to `/archive/root`
- `/archive/a/../b` → empty (raw `..` rejected)
- `../archive` → empty
- `archive/../../root` → empty
- `a/..` → empty

**Required tests:**
- Accepted: `/archive/root`, `/archive/root/`, `/archive/./root`, repeated separators
- Rejected: `/archive/a/../b`, `../archive`, `archive/../../root`, `a/..`, empty

**Implementation evidence:**

Commit SHA: _____________

Focused test result: _____________

### P10-F03 — Read lease counter correctness

- [ ] Complete

**Problem:** `return_read(nullptr)` in `ConnectionPool.cpp` decrements `outstanding_leases_` without a matching increment. `lease_read()` only increments after the `!write_conn_` check. But if there is a race where `write_conn_` becomes null after the check, `lease_read()` returns nullptr without incrementing, yet `return_read(nullptr)` would decrement.

**Affected files/functions:**
- `libs/storage/ConnectionPool.cpp` — `lease_read()`, `return_read()`

**Approved contract:** `outstanding_leases_` equals the number of successfully issued read leases not yet returned. Decrement only on successful return of a counted lease. `nullptr` never decrements.

**Implementation plan:**
1. Remove `outstanding_leases_.fetch_sub(1)` from the `if (!db)` branch in `return_read()`
2. Use `exchange(false)` on `read_in_use_[i]` and only decrement if it was true
3. This ensures double-return has no effect and unknown pointers are ignored

**Acceptance criteria:**
- `return_read(nullptr)` leaves counter unchanged
- Double return of same slot does not decrement twice
- Foreign (unknown) pointer does not change counter
- Counter never becomes negative

**Required tests:**
- Lease on uninitialized pool → nullptr, counter=0
- `return_read(nullptr)` → counter unchanged
- Successful lease → counter=1
- Successful return → counter=0
- Double return → counter stays 0
- Foreign pointer → counter unchanged
- Repeated init/shutdown cycles preserve zero counter

**Implementation evidence:**

Commit SHA: _____________

Focused test result: _____________

### P10-F04 — Safe shutdown with active read leases

- [ ] Complete

**Problem:** `shutdown()` in `ConnectionPool.cpp` was given a 1-second timeout in commit `bbc730e`, after which it force-closes read connections. Active `ReadLease` instances holding raw `SQLiteDB*` pointers to destroyed connections would use-after-free.

**Affected files/functions:**
- `libs/storage/ConnectionPool.cpp` — `shutdown()`
- `libs/storage/ConnectionPool.h` — `ReadLease`

**Approved contract:** `shutdown()` blocks until all active leases are returned, then safely closes connections. Production code must not hold leases at destruction time. A bounded `try_shutdown(timeout)` can be offered but must report failure without destroying active connections.

**Implementation plan:**
1. Remove the 1-second timeout loop from `shutdown()` (restore blocking behavior)
2. The blocking behavior is correct: leases are always returned by well-behaved code
3. The previous infinite hang was caused by lease counter bugs (P10-F03 fixes this)
4. If a timeout API is desired, add `try_shutdown()` separately — but Phase 10 doesn't need it

**Acceptance criteria:**
- `shutdown()` blocks until all leases returned
- `shutdown()` never force-closes a connection still held by a ReadLease
- After P10-F03 fix, no more infinite hangs
- New leases rejected after shutdown starts

**Required tests:**
- Active lease exists → shutdown blocks
- Lease released → shutdown completes
- New leases rejected after shutdown marks flag
- No use-after-free
- No force-close of active connection

**Implementation evidence:**

Commit SHA: _____________

Focused test result: _____________

### P10-F05 — ConnectionPool lifecycle invariants

- [ ] Complete

**Problem:** No formal documentation of ConnectionPool lifecycle states, mutex ownership, or counter invariants. Uninitialized pools, double shutdown, init-after-shutdown, and destruction semantics are not documented.

**Affected files/functions:**
- `libs/storage/ConnectionPool.h` — full class
- `libs/storage/ConnectionPool.cpp` — `initialize()`, `lease_read()`, `return_read()`, `shutdown()`, `~ConnectionPool()`, `WriteGuard`, `ReadLease`

**Approved contract:**
1. Uninitialized pool issues no leases
2. Shut-down pool issues no leases/write guards
3. Each ReadLease maps to one in-use slot, one outstanding_leases_ increment
4. Returning a lease clears its slot and decrements the counter
5. Double return has no effect (use `exchange(false)` pattern)
6. Unknown pointer return has no effect
7. `shutdown()` blocks until all leases returned, then closes
8. `shutdown()` is idempotent
9. Destructor calls `shutdown()` — safe for never-initialized, init-failed, or shut-down pools
10. `write_connection()` may only be called with an active WriteGuard/TransactionGuard

**Implementation plan:**
1. Apply P10-F03 fix (counter correctness)
2. Apply P10-F04 fix (shutdown behavior)
3. Audit all methods for compliance
4. Document in `docs/development/connection-pool-lifecycle.md`

**Acceptance criteria:** All 10 invariants hold. Documentation exists.

**Required tests:**
- Destructor on never-initialized pool
- `shutdown()` on never-initialized pool
- `shutdown()` twice
- Initialize failure then shutdown
- Successful init then shutdown
- Acquisition rejected after shutdown
- `WriteGuard` validity on uninitialized pool
- `ReadLease` validity on uninitialized pool

**Implementation evidence:**

Commit SHA: _____________

Focused test result: _____________

### P10-F06 — Archive regression validation

- [ ] Complete

**Problem:** Several old archive tests had their assertions changed. The ConnectionPool and path/version fixes must not regress Phase 10 archive behavior.

**Affected files/functions:**
- `tests/test_migration.cpp` — archive tests
- `libs/storage/LegacyArchive.cpp` — `create_archive()`, `verify_archive()`
- `libs/storage/ConnectionPool.cpp` — pool lifecycle

**Audit of changed tests:**

| Old test | Why it failed | Fix applied | Justification |
|----------|--------------|-------------|---------------|
| Archive respects required… | Old data (3-field nodes) didn't match reader format | Used `write_all_required()` | Test data was wrong; production code correct |
| Archive fails without required | Invalid UUID `"test-uuid"` + old data | Valid UUID | UUID validation added in Phase 10 |
| Archive fails if verification | Invalid UUID | Valid UUID | UUID validation added in Phase 10 |
| Archive rejects symlink | Old data + error msg changed | `write_all_required()` + error check | Error now `required_file_missing:nodes.db` (symlinks treated as missing) |
| Archive verify fails… | Old data | `write_all_required()` | Test data was wrong |
| Archive source unchanged | Old data | `write_all_required()` | Test data was wrong |
| Archive validates migration UUID | — | No change needed | |
| Archive rejects unsafe version | `v1.0-rc1` test case | Need P10-F01 fix | Test should use `v1.0.0-rc1` |
| Archive naming… | Old data + DVR | `make_full_dvr()` | DVR validation added in Phase 10 |
| Archive manifest… | Old data + DVR | `make_full_dvr()` | DVR validation added in Phase 10 |

**Symlink error contract decision:** When a required source file is a symlink, the current error is `required_file_missing:nodes.db` because symlinks are treated as absent per the inventory check. This is correct — symlinks are a security violation and should be treated as non-existent. The error distinguishes a missing file from a parse failure.

**Approved contract:** Symlink sources are treated as missing. Error: `required_file_missing:<filename>`. This is a safe fail-closed behavior.

**Implementation plan:**
1. After P10-F01 through P10-F05 are fixed, run all archive tests
2. Verify all E2E, tampering, and regression tests pass
3. Confirm no Phase 10 behavior regressed

**Acceptance criteria:**
- All 15 Phase 10 archive tests pass
- All manifest/file-entry tests pass
- All tampering tests pass
- E2E archive test passes with 132+ assertions

**Implementation evidence:**

Commit SHA: _____________

Focused test result: _____________

### P10-F07 — Clean build and final validation

- [ ] Complete

**Problem:** Final validation must use a clean build directory with Release configuration.

**Implementation plan:**
1. `rm -rf build`
2. `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
3. `ninja -C build containercpd containercp_tests`
4. Run focused ConnectionPool tests
5. Run focused Phase 10 archive tests
6. Run complete test suite
7. Verify zero failures

**Required result:**
- Zero build errors
- Zero focused test failures
- Zero full-suite failures
- Zero failed assertions
- Clean working tree
- HEAD equals origin/main

**Implementation evidence:**

Commit SHA: _____________

Focused test result: _____________

## Traceability

| ID | Production files | Test cases | Commit SHA | Result |
|----|------------------|------------|------------|--------|
| P10-F01 | LegacyArchive.cpp | safe_version tests | | |
| P10-F02 | LegacyArchive.cpp | normalize_archive_identity_path tests | | |
| P10-F03 | ConnectionPool.cpp | lease_read/return_read tests | | |
| P10-F04 | ConnectionPool.cpp | shutdown tests | | |
| P10-F05 | ConnectionPool.h/.cpp | lifecycle tests | | |
| P10-F06 | LegacyArchive.cpp, test_migration.cpp | archive regression tests | | |
| P10-F07 | All | Full suite | | |

## Final validation

__Build environment:__

__Build commands:__

__Focused ConnectionPool result:__

__Focused archive result:__

__Full suite result:__

__HEAD SHA:__

__origin/main SHA:__

__git status:__

## Remaining limitations

1. `reopen sensitive field redaction` test was fixed (WriteGuard deadlock pattern). Same pattern existed in 10 other tests — all fixed.
2. Old archive tests with incomplete `DatabaseVerificationResult` replaced with `make_full_dvr()`.
3. ConnectionPool has no bounded `try_shutdown()` API — blocking `shutdown()` is sufficient for Phase 10.
4. Symlink sources produce `required_file_missing` error — by design (fail-closed).

## Final readiness statement

Phase 10 production code is correct and complete when all P10-F01 through P10-F07 items pass. No Phase 11 behavior has been introduced.
