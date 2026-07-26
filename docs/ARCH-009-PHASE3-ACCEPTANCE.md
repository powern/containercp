# ARCH-009 Phase 3 Acceptance Report

## Summary

**Decision:** Phase 3 ACCEPTED WITH RESIDUAL RISKS.

**Reviewed starting HEAD:** `6966f96e5c378ccff0643c854c2e33077b420b79`

**Final Task48 HEAD:** reported in the task completion response after push. This report is part of that final acceptance commit; embedding the commit's own SHA in the same commit would require a second hash-only commit.

**Schema version:** `7` (`libs/storage/Storage.cpp:20`)

**Scope:** Local SFTP Phase 3 account, grant, mount, persistence, runtime reconciliation, and privileged command hardening.

**Phase 4 status:** Not started. SSHD config and `authorized_keys` management remain out of scope.

## Architecture Summary

ARCH-009 Phase 3 implements the backend foundation for local SFTP access:

- Linux system account mappings live in SQLite `system_accounts` and are represented by `SystemAccountMapping`.
- Site grant lifecycle state lives in SQLite `grant_lifecycle`.
- Managed bind-mount lifecycle state lives in SQLite `managed_mounts`.
- `LocalSftpProvider` owns user lifecycle, site-group lifecycle, grant application/revocation, chroot layout, bind mount reconciliation, startup reconciliation, and runtime health state.
- `SystemAccountCommandRunner` validates command arguments and dispatches canonical absolute executable paths only.
- Production wiring in `ServiceRegistry::start()` injects real identity inspection, allocator ranges, persistence callbacks, real mount/filesystem inspectors, and `CommandExecutor::run_safe()`.

## Evidence Classes

| Class | Meaning | Phase 3 Evidence |
|-------|---------|------------------|
| Implemented and unit-tested | Production code exists and direct doctest coverage exercises behavior using fake or in-memory state | Broad Phase 3 test coverage in `tests/test_access.cpp`, schema tests, runtime tests |
| Fake-state integration | Multiple provider subsystems interact through fake OS/filesystem/mount/storage models | Grant lifecycle, mount lifecycle, rollback, crash recovery, reconciliation |
| Privileged Linux executed | Real root-owned Linux tools executed through `CommandExecutor::run_safe()` on disposable host | Account create/remove, identity verification, chroot `sites/` layout, global group membership |
| Not privileged executed | Implemented/tested only with fake state, not real kernel/tools | Real ACL enforcement, bind mount identity/idempotency/unmount, grant lifecycle, SQLite lifecycle inside privileged harness |

## Source Verification

| Item | Evidence |
|------|----------|
| Expected schema version | `libs/storage/Storage.cpp:20` = `kExpectedSchemaVersion = 7` |
| Phase 3 migrations | `libs/storage/SchemaMigrations.cpp:257-312`, `395-469` cover system accounts, home, managed mounts, grant lifecycle, last error |
| ServiceRegistry wiring | `libs/core/ServiceRegistry.cpp:652-763` wires mount storage, identity inspector, allocator, runner, mapping persistence, grant lifecycle, grants loader/lookup, dependency verification, reconciliation |
| Runtime state machine | `libs/access/SftpRuntimeState.h`; `LocalSftpProvider::retry_reconciliation()` in `libs/access/LocalSftpProvider.cpp:3182-3212` |
| Dependency verification | `LocalSftpProvider::verify_dependencies()` in `libs/access/LocalSftpProvider.cpp:119-150` |
| Public/internal split | Public wrappers call `operation_gate()` and delegate to `_internal`; internal reconciliation uses `_internal` variants |
| Privileged hardening | `libs/access/SystemAccountCommandRunner.cpp:14-198`, `216-432`; `libs/runtime/CommandExecutor.cpp:217-353` |
| Integration target | `tests/CMakeLists.txt:213-241`; `tests/arch009_linux_integration.cpp` |
| Runbook | `docs/arch009-linux-integration-runbook.md` |

## Acceptance Matrix

Status definitions:

- PASS: implemented and directly tested. Privileged evidence is listed where available.
- PARTIAL: implemented and unit/fake-state tested, but not privileged-executed against the real Linux kernel/system tools.
- FAIL: known blocking defect remains.
- NOT EXECUTED: no direct test evidence exists.

### A. User Identity Lifecycle

| Row | Behavior | Status | Production source | Direct test reference | Privileged evidence | Residual risk |
|-----|----------|--------|-------------------|-----------------------|---------------------|---------------|
| A1 | Create user | PASS | `LocalSftpProvider.cpp:2712-2899` | create failure/lifecycle tests in `tests/test_access.cpp:7182-7377` | `arch009_linux_integration` PASS creates `au-arch46it` through real `groupadd`, `useradd`, `usermod`, `passwd` | None for account create path |
| A2 | Verify identity | PASS | `verify_ownership()` and create postcondition in `LocalSftpProvider.cpp:2857-2868` | identity mismatch/recovery tests in `tests/test_access.cpp:7676-7719` | UID/GID/home/shell verified by privileged executable | None for checked fields |
| A3 | Chroot `sites/` creation | PASS | `ensure_chroot_layout_internal()` in `LocalSftpProvider.cpp:756-819` | chroot and safe-rmdir tests in `tests/test_access.cpp:5087-5255` | real root-owned `sites/` layout verified | `.ssh` remains Phase 4 |
| A4 | Remove user | PASS | `LocalSftpProvider.cpp:2910-3030`; USERGROUPS_ENAB guard in private group removal | `remove_user` tests in `tests/test_access.cpp:7379-7541` | real `userdel` executed; Debian `USERGROUPS_ENAB=yes` defect fixed in Task 47 | None known |
| A5 | Restart recovery | PASS | `reconcile_user_lifecycle_internal()` in `LocalSftpProvider.cpp:1447-1871` | user reconciliation tests in `tests/test_access.cpp:7667-7876` | Not privileged-executed | Fake-state model accepted for Phase 3 |

### B. Site Permissions

| Row | Behavior | Status | Production source | Direct test reference | Privileged evidence | Residual risk |
|-----|----------|--------|-------------------|-----------------------|---------------------|---------------|
| B1 | Site groups | PASS | `ensure_site_group_internal()` in `LocalSftpProvider.cpp:241-331` | site-group tests in `tests/test_access.cpp` around Phase 3a | Not privileged-executed for site groups | Real `groupadd` path separately exercised for account groups only |
| B2 | RW permissions | PARTIAL | `apply_directory_permissions_internal()` in `LocalSftpProvider.cpp:503-553` | permissions tests in `tests/test_access.cpp` Phase 3b | Not privileged-executed | Needs real `chgrp/chmod` site public/ validation |
| B3 | RO ACL | PARTIAL | `apply_read_only_acl_internal()` and `remove_read_only_acl_internal()` in `LocalSftpProvider.cpp:562-661` | ACL tests in `tests/test_access.cpp` Phase 3b/Task35 | Not privileged-executed | Needs real `setfacl/getfacl` and RO write-denial validation |
| B4 | Shared ACL retention | PARTIAL | `revoke_grant_internal()` retention logic in `LocalSftpProvider.cpp:2406-2458` | Task35 tests in `tests/test_access.cpp:6566-6745` | Not privileged-executed | Needs real ACL retention proof |
| B5 | Group retention/removal | PARTIAL | `delete_site_group_if_unused_internal()` and lifecycle counts | Task35 tests in `tests/test_access.cpp:6605-6745` | Not privileged-executed | Needs real site-group retention proof |

### C. Mount Lifecycle

| Row | Behavior | Status | Production source | Direct test reference | Privileged evidence | Residual risk |
|-----|----------|--------|-------------------|-----------------------|---------------------|---------------|
| C1 | Bind mount | PARTIAL | `bind_mount_site_internal()` in `LocalSftpProvider.cpp:827-985` | bind tests in `tests/test_access.cpp:2856-4749` | Not privileged-executed | Needs real `mount --bind` proof |
| C2 | Bind identity verification | PARTIAL | `MountInspector.cpp`; bind-root checks in `LocalSftpProvider.cpp` | mount parser and bind tests in `tests/test_access.cpp` | Not privileged-executed | Needs real `/proc/self/mountinfo` validation on disposable host |
| C3 | Idempotency | PARTIAL | `bind_mount_site_internal()` pre-existing mount logic | idempotency tests in `tests/test_access.cpp` | Not privileged-executed | Needs repeated real bind run |
| C4 | Rollback | PARTIAL | apply-grant rollback in `LocalSftpProvider.cpp:2171-2219` | rollback tests in `tests/test_access.cpp:2869-3999` | Not privileged-executed | Needs real failed-mount cleanup proof |
| C5 | Unmount | PARTIAL | `unmount_site_internal()` in `LocalSftpProvider.cpp:992-1105` | unmount tests in `tests/test_access.cpp:4752-5085` | Not privileged-executed | Needs real `umount` proof |
| C6 | Orphan/stale reconciliation | PARTIAL | `reconcile_mounts_internal()` and `reconcile_startup_mounts_internal()` | reconciliation tests in `tests/test_access.cpp:5680-6170` | Not privileged-executed | Needs real stale/orphan mount proof |
| C7 | Foreign mount protection | PARTIAL | foreign mount handling in mount/user reconciliation | fake-state tests around foreign mounts | Not privileged-executed | Needs real foreign mount non-mutation proof |

### D. Persistence

| Row | Behavior | Status | Production source | Direct test reference | Privileged evidence | Residual risk |
|-----|----------|--------|-------------------|-----------------------|---------------------|---------------|
| D1 | Schema migrations | PASS | `SchemaMigrations.cpp:257-469` | `tests/test_schema.cpp`, `tests/test_sqlite_storage.cpp` | Not in privileged harness | None known |
| D2 | System account mappings | PASS | `SQLiteStorage.cpp:749-796`; ServiceRegistry mapping callbacks | storage and provider lifecycle tests | Account mapping path used in privileged harness with in-memory callbacks | Real SQLite callback not privileged-executed |
| D3 | Grant lifecycle | PASS | `SQLiteStorage.cpp:910-1021`; `LocalSftpProvider.cpp:1888-2646` | Task31-37 tests in `tests/test_access.cpp` | Not privileged-executed | Privileged grant lifecycle deferred |
| D4 | Managed mount lifecycle | PASS | `SQLiteStorage.cpp:808-907`; startup mount reconciliation | Task29-30 tests in `tests/test_access.cpp` | Not privileged-executed | Privileged mount lifecycle deferred |
| D5 | Crash states | PASS | applying/revoking/error state handlers in `LocalSftpProvider.cpp` | crash recovery tests in `tests/test_access.cpp:6171-6378` | Not privileged-executed | Fake-state accepted for Phase 3 |
| D6 | Retry behavior | PASS | `retry_reconciliation()` and RAII guard | Task42/43 tests in `tests/test_access.cpp:8061-8556` | Not privileged-executed | None known after Task48 internal-call fix |

### E. Runtime Safety

| Row | Behavior | Status | Production source | Direct test reference | Privileged evidence | Residual risk |
|-----|----------|--------|-------------------|-----------------------|---------------------|---------------|
| E1 | Disabled/Starting/Healthy/Degraded/Failed | PASS | `SftpRuntimeState.h`; `operation_gate()` in `LocalSftpProvider.h` | runtime-state tests in `tests/test_access.cpp:8061-8556` | Not privileged-executed | None known |
| E2 | Public mutation gating | PASS | public wrappers in `LocalSftpProvider.cpp` | operation gate tests in `tests/test_access.cpp` | Not privileged-executed | None known |
| E3 | Internal reconciliation | PASS | `_internal` methods and `run_reconciliation_flow()` | Task48 regression `removing user with applying grant reconciles during Starting` | Not privileged-executed | None known after fix |
| E4 | Concurrency guard | PASS | `ScopedReconciliationGuard` in `LocalSftpProvider.h` | guard tests in `tests/test_access.cpp:8431-8515` | Not privileged-executed | True concurrent thread race not load-tested |
| E5 | Degraded behavior | PASS | runtime state transitions in `retry_reconciliation()` | Degraded/read behavior tests | Not privileged-executed | None known |

### F. Privileged Execution

| Row | Behavior | Status | Production source | Direct test reference | Privileged evidence | Residual risk |
|-----|----------|--------|-------------------|-----------------------|---------------------|---------------|
| F1 | Canonical executable identity | PASS | `SystemAccountCommandRunner.cpp:14-198` | Task45/46 tests in `tests/test_access.cpp:8773-9232` | Executable identity preflight runs before privileged mutation | None known |
| F2 | Argv validation | PASS | `SystemAccountCommandRunner.cpp:72-155`, command methods | Task44/45 tests | Account lifecycle commands executed through argv vector | Coverage strongest for account lifecycle |
| F3 | UID/GID ranges | PASS | `validate_uid/validate_gid()` in `SystemAccountCommandRunner.cpp:216-227`; ServiceRegistry ranges | UID/GID range tests | Privileged user created in managed ranges | None known |
| F4 | ACL grammar | PASS | `is_valid_acl_spec()` in `SystemAccountCommandRunner.cpp:102-133` | strict ACL grammar tests | Not privileged-executed | Grammar covered; real ACL behavior deferred |
| F5 | chmod grammar | PASS | `is_valid_octal_mode()` and `chmod()` | strict chmod tests | chroot chmod executed in privileged harness | Site chmod not privileged-executed |
| F6 | Managed path validation | PASS | `ManagedPathValidator.cpp` and runner path validation | Task45/46 managed-path tests | Chroot path used in privileged harness | Site paths rely on provider-level semantic checks |
| F7 | Environment sanitization | PASS | `CommandExecutor::run_safe()` in `CommandExecutor.cpp:217-353` | runtime command tests | Privileged harness uses `run_safe()` | None known |
| F8 | Timeout/output bounds | PASS | `run_safe_capture()` timeout and output caps | runtime command tests | Not directly stressed by privileged harness | No load/stress validation |

### Matrix Totals

| Status | Count |
|--------|-------|
| PASS | 25 |
| PARTIAL | 11 |
| FAIL | 0 |
| NOT EXECUTED | 0 |

## Privileged Integration Scope

Task 47 and Task 48 privileged execution validates only account lifecycle scope:

- Real canonical executable identity checks.
- Real managed user/private group creation.
- Real global SFTP group membership.
- Real UID/GID/home/shell verification.
- Real password lock command path.
- Real chroot `sites/` directory creation and permissions.
- Real account/private group/global group cleanup.
- Real cleanup proof for identities, marker, paths, mounts, and processes.

The current privileged executable does **not** execute:

- `apply_grant()` against real Linux.
- `revoke_grant()` against real Linux.
- real `setfacl/getfacl` ACL enforcement.
- real `mount --bind` identity/idempotency.
- real `umount` and stale/orphan/foreign mount reconciliation.
- SQLite-backed lifecycle persistence inside the privileged harness.

These gaps are accepted for Phase 3 because fake-state and unit coverage is broad, no known fail-open/data-loss/foreign-mount mutation defect remains after Task48 fixes, and Phase 4 should not expose SFTP login before dedicated privileged grant/mount validation is added.

## Acceptance Defects Found During Task48

| Defect | Risk | Fix | Regression |
|--------|------|-----|------------|
| `revoke_grant_internal()` called public `apply_grant()` while recovering an applying grant. Startup reconciliation in `Starting` could self-block on public gate. | Recoverable grant/user removal could fail during startup despite using internal reconciliation. | Replaced with `apply_grant_internal()`. | `ARCH-009 removing user with applying grant reconciles during Starting` |
| Pending grant revoke ignored failed cleanup of stale membership/group state, then could delete lifecycle and return success. | Lifecycle success after partial cleanup failure. | Check cleanup results, persist error, return failure. | `ARCH-009 revoke pending grant fails if stale membership cleanup fails` |

## Regression Search Results

| Pattern | Result |
|---------|--------|
| Short executable names in ARCH-009 privileged paths | No access-layer shell/short-name dispatch found; runner maps to canonical `/usr/bin` and `/usr/sbin` paths. |
| Shell execution in ARCH-009 privileged paths | No `std::system`, `popen`, `/bin/sh`, or `sh -c` found in `libs/access`. |
| Public calls from internal reconciliation | One defect found and fixed (`apply_grant` -> `apply_grant_internal`). |
| Ignored mutation results | One blocking ignored cleanup result found and fixed. Remaining ignored calls are best-effort cleanup/rollback paths that do not return success for the primary lifecycle operation. |
| Unchecked persistence writes | No blocking unchecked success path found in Phase 3 lifecycle after Task48 fixes. |
| Unbounded persisted errors | Diagnostics remain string-based; most are bounded tokens. Full hard maximum length is not enforced and is accepted residual risk. |
| Lifecycle success after partial failure | One defect found and fixed in pending revoke cleanup. |

## Accepted Residual Risks

- Real Linux grant lifecycle, ACL, bind mount, unmount, stale/orphan mount, and foreign mount protection remain **not privileged-executed**.
- Persisted `last_error` and diagnostic strings are not centrally length-capped. Current messages are generated from bounded internal tokens or command output summaries, but a hard storage-level maximum is deferred.
- Concurrency guard is unit-tested structurally and sequentially, not with a threaded stress test.
- Phase 3 does not configure OpenSSH or `authorized_keys`; no SFTP login is expected until Phase 4.
- `build2/` is an untracked local validation directory and is not part of repository state.

## Validation Evidence

| Validation | Result |
|------------|--------|
| Focused ARCH-009 doctests | PASS: `197` cases, `680` assertions |
| Full unit suite | PASS: `ctest --test-dir build2 -R '^containercp_tests$' --output-on-failure` |
| Privileged integration suite | PASS, not skip: `CONTAINERCP_DISPOSABLE_TEST_HOST=1 CONTAINERCP_ARCH009_LINUX_INTEGRATION=1 ctest --test-dir build2 -L arch009_linux_integration --output-on-failure` |
| Full CTest suite | PASS: `2/2` tests passed with privileged opt-in |
| Cleanup verification | PASS: no test user, no private group, no global group, no matching mounts, no managed test path, no marker, no leaked process, no SQLite test file |
| Whitespace check | PASS: `git diff --check` |

## Operator Requirements

- Run privileged integration only on disposable Linux hosts or disposable privileged containers.
- Required tools: `groupadd`, `useradd`, `usermod`, `userdel`, `groupdel`, `passwd`, `gpasswd`, `chgrp`, `chmod`, `setfacl`, `getfacl`, `mount`, `umount`, `mountpoint`, `rmdir`, `chown`, `ls`, SQLite and build dependencies.
- Required opt-in: `CONTAINERCP_DISPOSABLE_TEST_HOST=1`, `CONTAINERCP_ARCH009_LINUX_INTEGRATION=1`, and `/tmp/containercp-allow-arch009-linux-integration` marker.
- Cleanup must prove absence of `au-arch46it`, `ccp-arch009-it`, matching mounts, marker file, and `/srv/containercp/arch009-linux-integration`.

## Recovery Behavior

- Startup reconciliation runs after dependency verification and provider enablement.
- Public mutations are blocked while `Starting`, `Failed`, or `Degraded`.
- Internal reconciliation uses `_internal` variants to bypass public gate while retaining safety checks.
- User lifecycle handles `provisioning`, `active`, `removing`, and `error` states.
- Grant lifecycle handles `pending`, `applying`, `active`, `revoking`, and `error` states.
- Managed mounts reconcile `pending`, `applying`, `active`, `removing`, and `error` states without mutating foreign mounts.

## Deferred Work

- Extend privileged integration to create a disposable site public directory, apply RW and RO grants, verify real ACL enforcement, verify real bind mount identity, repeat bind idempotency, unmount, and reconcile stale/orphan/foreign mount states.
- Add SQLite-backed privileged harness coverage for `system_accounts`, `grant_lifecycle`, and `managed_mounts` rather than in-memory callbacks.
- Add hard diagnostic length caps for persisted lifecycle `last_error` fields.
- Add threaded reconciliation guard test if runtime starts to allow concurrent admin-triggered reconciliation.

## Phase 4 Entry Criteria

Phase 4 may start only after this report is committed and pushed, with the following constraints:

- Do not expose real SFTP login before Phase 4 sshd config and authorized key management is implemented and validated.
- Before any user-facing SFTP login acceptance, add privileged grant/mount/ACL integration coverage listed under deferred work.
- Keep API-first order for any new operations.
- Preserve public/internal reconciliation separation and fail-closed lifecycle semantics.
