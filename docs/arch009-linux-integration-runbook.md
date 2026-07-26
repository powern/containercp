# ARCH-009 Linux Integration Runbook

This runbook covers the isolated privileged integration validation for ARCH-009 Phase 3 lifecycle hardening.

The test is intentionally opt-in. It creates and removes real Linux users and groups through the production path:

`LocalSftpProvider -> SystemAccountCommandRunner -> CommandExecutor::run_safe() -> execve() -> Linux system tools`.

## Safety Scope

Run only on a disposable Linux VM or disposable container with throwaway `/etc/passwd`, `/etc/group`, `/etc/shadow`, and `/srv/containercp` state.

Do not run on a workstation, production host, staging host with real accounts, or any host that has important local users/groups.

The test uses these disposable identities and paths:

- Access username: `arch46it`
- Linux username and private group: `au-arch46it`
- Global SFTP group: `ccp-arch009-it`
- Managed root: `/srv/containercp/arch009-linux-integration/users`
- Marker file: `/tmp/containercp-allow-arch009-linux-integration`

## Prerequisites

- Linux host.
- Root privileges.
- Disposable host confirmation via `CONTAINERCP_DISPOSABLE_TEST_HOST=1`.
- Explicit test opt-in via `CONTAINERCP_ARCH009_LINUX_INTEGRATION=1`.
- Marker file exists: `/tmp/containercp-allow-arch009-linux-integration`.
- Required canonical executables exist and pass identity checks: root-owned, regular file, executable, non-symlink, not group/world writable.
- `/usr/sbin/nologin` exists.
- No pre-existing `au-arch46it`, `au-arch46it` group, or `ccp-arch009-it` group.

If any prerequisite is missing, CTest reports the integration test as skipped with return code `77`.

## Build

```bash
cmake --build build --target arch009_linux_integration
```

Use the existing project build directory if it is not named `build`.

## Safe Skip Validation

This command is safe on non-disposable hosts because it should skip unless all opt-in prerequisites are present:

```bash
ctest --test-dir build -L arch009_linux_integration --output-on-failure
```

Expected result without opt-in: skipped.

## Privileged Execution

On the disposable host only:

```bash
touch /tmp/containercp-allow-arch009-linux-integration
CONTAINERCP_DISPOSABLE_TEST_HOST=1 \
CONTAINERCP_ARCH009_LINUX_INTEGRATION=1 \
ctest --test-dir build -L arch009_linux_integration --output-on-failure
```

Expected result with all prerequisites: pass.

## What The Test Verifies

- Canonical executable identity checks run before privileged mutation.
- `LocalSftpProvider::create_user()` creates the managed account through real `groupadd`, `useradd`, `usermod`, `passwd`, `mkdir`, `chown`, and `chmod` calls.
- `SystemAccountCommandRunner` enforces managed UID/GID ranges before dispatch.
- `CommandExecutor::run_safe()` uses `execve()` with sanitized environment.
- The created account has UID `10000-19999`, GID `20000-29999`, expected home, expected shell, global SFTP group membership, and chroot `sites/` layout.
- `LocalSftpProvider::remove_user()` removes the account and private group.
- Final cleanup removes the test global group and managed test root.

## Cleanup Verification

After a successful or failed privileged run, verify:

```bash
getent passwd au-arch46it
getent group au-arch46it
getent group ccp-arch009-it
test ! -e /srv/containercp/arch009-linux-integration
```

All `getent` commands should return no records, and the final `test` should return success.

If manual cleanup is needed on the disposable host:

```bash
userdel au-arch46it
groupdel au-arch46it
groupdel ccp-arch009-it
rm -rf /srv/containercp/arch009-linux-integration
rm -f /tmp/containercp-allow-arch009-linux-integration
```

Do not run the manual cleanup commands on a non-disposable host.
