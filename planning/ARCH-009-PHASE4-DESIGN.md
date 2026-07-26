# ARCH-009 Phase 4 — OpenSSH Integration Design and Safety Contract

**Status:** DESIGN — Task 50 SshdDiscovery implemented

**Revision:** 2

**Base:** `a5a61315f8484076541de8de91a8a6bbcedc0ece`
**SshdDiscovery SHA:** `170ff4a9be5ca08e3fdc3d62fc9c4229c9e4be7d` + Task 50 commit

**New files since Revision 1:**
- `libs/access/SshdDiscovery.h` — discovery result structs, version parser, RAII temp file, discovery class
- `libs/access/SshdDiscovery.cpp` — implementation: executable identity, version parsing, config/service/directive discovery, static helpers
- `tests/test_sshd_discovery.cpp` — 25 unit tests covering version parsing, executable validation, temp file safety, service discovery, result formatting

**Schema version:** 7

**Phase 3 acceptance:** ACCEPTED WITH RESIDUAL RISKS — see `docs/ARCH-009-PHASE3-ACCEPTANCE.md`

---

## 1. Architecture Overview

Phase 4 integrates the managed SFTP user lifecycle with the running OpenSSH
daemon.  It does NOT replace or modify the system `sshd_config`.  Instead it
owns one dedicated include file and one managed authorized-keys directory,
leaving unmanaged SSH users and existing configuration entirely untouched.

```
LocalSftpProvider
  │
  ├── SshdConfigWriter          (new)
  │     renders 90-containercp-sftp.conf
  │     atomic write → sshd -T → reload → verify
  │
  ├── SshdAuthorizedKeysWriter  (new)
  │     renders per-user authorized_keys under managed root
  │     atomic write → mode/owner check → verify
  │
  └── Runtime safety (existing Phase 3 contract)
        operation_gate, _internal methods, reconciliation
```

### What already exists (Phases 1-3)

- `AccessKey` struct, `AccessKeyManager`, SQLite `access_keys` table (schema v2)
- `SshKeyValidator` for parsing + SHA256 fingerprinting
- `LocalSftpProvider` with full Phase 3 lifecycle
- `SystemAccountCommandRunner` with canonical executable allowlist
- `CommandExecutor::run_safe()` with environment sanitization
- `SftpRuntimeState` machine (Disabled, Starting, Healthy, Degraded, Failed)
- `RealSystemIdentityInspector` for OS identity checks
- `verify_dependencies()`, `operation_gate()`, `_internal` reconciliation split

### What Phase 4 must add

- `SshdConfigWriter` — render, validate, install, reload, rollback the managed
  sshd include file
- `SshdAuthorizedKeysWriter` — render, validate, install per-user
  authorized_keys outside the chroot jail
- Wires to `LocalSftpProvider`: key add/remove triggers authorized_keys rebuild;
  user create/remove triggers sshd include rebuild
- Wires to `ServiceRegistry`: production callbacks for discovery, config write,
  key write
- `SshdDiscovery` helper: detect OpenSSH version, binary path, service name,
  include support, internal-sftp availability
- Privileged integration tests for sshd -T, reload, config rollback

### What Phase 4 must NOT own

- The main `/etc/ssh/sshd_config` file — never edited directly
- Any configuration for unmanaged SSH users
- Any Docker/container sshd setup
- Any SSHFP DNS records
- Any SSH certificate authority infrastructure

---

## 2. SSHD Integration Model

### 2.1 Chosen Model: Dedicated Include File

**One file, one Match block, no edits to main config:**

```
/etc/ssh/sshd_config.d/90-containercp-sftp.conf
```

**Why this model:**

- Main `sshd_config` is already installed by the distribution and may contain
  site-specific settings from the administrator.  ContainerCP must not touch
  it.
- Debian 12+ and Ubuntu 22.04+ ship with `Include /etc/ssh/sshd_config.d/*.conf`
  commented IN the default `sshd_config`.
- OpenSSH 8.2p1+ supports the `Include` directive in `sshd_config`.
- Our file is placed last (`90-` prefix) so it can override earlier settings
  without conflicting.
- If the include directory or directive is absent, Phase 4 detects this at
  dependency verification time and fails closed — it does NOT try to create
  directories or modify `sshd_config`.

### 2.2 Exact Match Policy

```ssh
Match Group containercp-sftp
    ChrootDirectory /srv/containercp/users/%u
    ForceCommand internal-sftp
    PasswordAuthentication no
    PubkeyAuthentication yes
    AuthorizedKeysFile /srv/containercp/ssh/authorized_keys/%u
    PermitTTY no
    AllowTcpForwarding no
    AllowAgentForwarding no
    X11Forwarding no
    PermitTunnel no
    GatewayPorts no
```

**Why Match Group instead of Match User:**

- Every managed SFTP user's primary group is `au-<username>`, but they all
  share the supplementary group `containercp-sftp` (created by
  `ensure_global_sftp_group()` in Phase 3).
- Matching on the common supplementary group is more maintainable than listing
  individual users.
- `containercp-sftp` is already enforced at account creation time.

**OpenSSH version compatibility (OpenSSH 8.0+):**

| Directive | Min version | Supported on Debian 13? |
|-----------|-------------|------------------------|
| `Match Group` | 4.9 | yes (10.0p2) |
| `ChrootDirectory` | 4.9 | yes |
| `ForceCommand internal-sftp` | 4.9 | yes |
| `PasswordAuthentication` | 2.3 | yes |
| `PubkeyAuthentication` | 2.3 | yes |
| `AuthorizedKeysFile` | 2.3 | yes |
| `PermitTTY` | 4.9 | yes |
| `AllowTcpForwarding` | 4.9 | yes |
| `AllowAgentForwarding` | 4.9 | yes |
| `X11Forwarding` | 2.3 | yes |
| `PermitTunnel` | 4.9 | yes |
| `GatewayPorts` | 4.9 | yes |

All directives are well-supported across OpenSSH 8.0–10.x. No experimental or
deprecated directive is used.

### 2.3 Match Block Safety

The `containercp-sftp` supplementary group is shared only by managed SFTP
accounts.  No unmanaged Linux user should be a member of this group.  If an
administrator manually adds an unmanaged user to `containercp-sftp`, that user
will also be matched by this block.  This is documented as a deliberate
operator responsibility — ContainerCP documents that `containercp-sftp` is a
managed group and must not be modified manually.

---

## 3. Chroot Contract

### 3.1 Phase 3 Current Layout

```
/srv/containercp/users/au-<username>/     ← mapping.home, ChrootDirectory root
    owner: root   mode: 0755   (set by create_user)
    │
    ├── sites/                            ← created by ensure_chroot_layout
    │   owner: root   mode: 0755
    │   │
    │   ├── example.com/                  ← bind mount from site public/
    │   │   owner: root   mode: 0755      (as seen in chroot, actual bind)
    │   └── myapp.org/                    ← bind mount
    │
    └── .ssh/                             ← NOT created in Phase 3
```

### 3.2 Phase 4 Chroot Contract

The chroot root (`mapping.home`) is already owned by root:root with mode 0755.
This satisfies the OpenSSH `ChrootDirectory` requirement — the chroot root
must be owned by root and not writable by any other user or group.

| Path | Owner | Group | Mode | Writable by user? | Created by |
|------|-------|-------|------|-------------------|------------|
| `au-<user>/` | root | root | 0755 | no | create_user |
| `sites/` | root | root | 0755 | no | ensure_chroot_layout |
| `sites/<domain>/` | root | site-<id>-rw | 0755 | yes (rw grant) | apply_grant |
| `sites/<domain>/` | root | root | 0755 (ACL: g:site-<id>-ro:r-x) | no (ro grant) | apply_grant |

No `.ssh/` directory is created inside the chroot.  Authorized keys are served
from outside the chroot via `AuthorizedKeysFile`.

**This resolves Phase 3's `.ssh/` placeholder — Phase 4 does NOT create
`.ssh/` inside the chroot.**

### 3.3 Phase 3 Consistency Check

The existing `ensure_chroot_layout_internal()` verifies:
- `sites/` is root-owned (uid=0)
- `sites/` has group gid=0
- `sites/` mode is 0755
- `sites/` is not a symlink

These checks are consistent with the OpenSSH chroot requirement.  No change
required.

---

## 4. Authorized Keys Model

### 4.1 Chosen Model: AuthorizedKeysFile Outside Chroot

**Storage location:**

```
/srv/containercp/ssh/authorized_keys/au-<username>
```

**Why outside the chroot:**

- Inside the chroot, the authorized_keys file would need to be owned by the
  SFTP user (not root) for sshd to accept it.  This would allow the user to
  modify their own keys, violating the security contract.
- `AuthorizedKeysFile /srv/containercp/ssh/authorized_keys/%u` tells sshd to
  read keys from outside the chroot.  `%u` expands to the system username
  (e.g. `au-developer`).
- The file is owned by root:root, mode 0600.  Users cannot write to it.

**Why not AuthorizedKeysCommand:**

- An external command adds complexity, a binary/script to maintain, potential
  timing/race issues, and a separate process invocation per login.
- Direct file management is simpler, more predictable, and matches the existing
  lifecycle pattern (rollback, postcondition verification, atomic replace).
- AuthorizedKeysCommand requires `AuthorizedKeysCommandUser nobody` (or
  similar) and a trusted helper.  The file-based approach needs no helper.
- Recommendation: use AuthorizedKeysCommand only if per-key expiry or dynamic
  key sources are required in a future phase.

### 4.2 File Properties

| Property | Value |
|----------|-------|
| Full path | `/srv/containercp/ssh/authorized_keys/<username>` |
| Owner | root:root |
| Mode | 0600 |
| Symlink | Rejected (O_NOFOLLOW on write) |
| Hardlink | Rejected (link count check) |
| Max size | 1 MiB (operator-enforced check) |
| Ordering | Stable: sort by fingerprint hex, then by id |
| Duplicates | None: unique (access_user_id, fingerprint) enforced by SQLite |
| Format | One key per line: `restrict <key-type> <key-blob> <comment>` |

### 4.3 Per-Key Restrictions

Every key line is prefixed with `restrict`:

```
restrict ssh-ed25519 AAAAC3... comment
```

The `restrict` option (OpenSSH 6.2+) disables all forwarding, agent access,
PTY allocation, and user-rc execution for that key.  This is in addition to
the Match-block-level restrictions and survives sshd config drift because it
lives in the key file, not the sshd config.

Compatibility: `restrict` is available since OpenSSH 6.2 (released 2013).
Debian 12+ ships OpenSSH 8.7+.

### 4.4 AuthorizedKeysFile Ownership

The containing directory is:

```
/srv/containercp/ssh/authorized_keys/
    owner: root:root  mode: 0755
```

Each user file:

```
/srv/containercp/ssh/authorized_keys/au-developer
    owner: root:root  mode: 0600  (no group/world access)
```

This directory is created during Phase 4 dependency verification and is part
of the `SshdDiscovery` preflight.

---

## 5. Config Transaction

### 5.1 Generate Managed SSH Include

The include file is rendered from the fixed Match block template.  No variable
expansion or placeholder substitution is needed — the template is static with
one managed group name, one managed chroot prefix, and one authorized key path.

```
Template path:  /srv/containercp/ssh/sshd_config.d/90-containercp-sftp.conf
Production path: /etc/ssh/sshd_config.d/90-containercp-sftp.conf
```

### 5.2 Transactional Steps

```
Step 1: Render → temp file under /srv/containercp/ssh/
  Path: /srv/containercp/ssh/.tmp.90-containercp-sftp.conf.<pid>
  Owner: root:root, mode 0644

Step 2: Validate content and paths
  - ChrootDirectory matches managed home root
  - AuthorizedKeysFile matches managed key root
  - No control characters
  - Bounded file size (4 KiB)

Step 3: sshd syntax validation
  Command: sshd -t -f <temp-file>
  Only validate the full include content by running sshd -t against a
  synthesized config that includes the temp file.
  Alternative: Write a temporary complete config with Include, run sshd -t.

Step 4: Atomic replace
  rename("/srv/containercp/ssh/.tmp.90-containercp-sftp.conf.<pid>",
         "/etc/ssh/sshd_config.d/90-containercp-sftp.conf")

Step 5: Re-validate effective config
  Command: sshd -t
  (Validates the full effective configuration including the new include)

Step 6: Reload sshd
  Command: systemctl reload ssh  (or SIGHUP)
  (NOT restart — reload preserves active sessions)

Step 7: Verify service health
  Check: systemctl is-active ssh
  Check: Running config matches expected (sshd -T, grep for ChrootDirectory)

Step 8: Rollback on failure
  If any of steps 2–7 fail:
    - Restore previous known-good file from backup
    - Run sshd -t on restored config
    - Attempt reload
    - Log error with bounded diagnostic
    - Do NOT expose SFTP user (persist error, stay in failed/degraded state)
```

### 5.3 Atomic Write Implementation

Uses `CommandExecutor::run_safe()` for all external commands.  No shell.
File writes use C++ filesystem operations (`std::filesystem::rename` for
atomic replacement, not shell `mv`).

Previous known-good file is preserved at:
```
/srv/containercp/ssh/sshd_config.d/90-containercp-sftp.conf.prev
```

---

## 6. SSHD Discovery

### 6.1 Discovery Parameters

| Parameter | Detection method | Debian 13 value |
|-----------|-----------------|-----------------|
| sshd binary path | `command -v sshd` or `/usr/sbin/sshd` | `/usr/sbin/sshd` |
| Service name | `systemctl list-units --type=service \| grep sshd` | `ssh.service` |
| Reload method | `systemctl reload <name>` or `kill -HUP` | `systemctl reload ssh` |
| Include directory | `test -d /etc/ssh/sshd_config.d` | exists |
| Include support | `sshd -T \| grep -i include` or check version | supported (OpenSSH 10.0) |
| `internal-sftp` | `sshd -s` or `grep internal-sftp /etc/ssh/sshd_config` | available as subsystem |
| `restrict` option | OpenSSH version >= 6.2 | supported |
| chroot support | `sshd -T \| grep chrootdirectory` | supported |
| OpenSSH version | `ssh -V 2>&1` parsed | 10.0p2 |

### 6.2 Compatibility Model

```cpp
struct SshdCapabilities {
    std::string sshd_binary = "/usr/sbin/sshd";
    std::string service_name = "ssh";
    bool include_dir_supported = false;
    bool internal_sftp_available = false;
    bool restrict_option_supported = false;
    bool chroot_supported = false;
    bool reload_via_systemctl = false;
    std::string error; // empty if all check passed
};
```

### 6.3 Fail-Closed on Missing Support

If any required capability is absent:
- `include_dir_supported == false` → fail closed (cannot add config)
- `internal_sftp_available == false` → fail closed (cannot enforce SFTP-only)
- `chroot_supported == false` → fail closed (cannot isolate users)
- `restrict_option_supported == false` → degrade gracefully (Match block still
  provides restrictions without per-key `restrict`)
- Service not found or not running → fail closed (cannot reload)

---

## 7. Fail-Closed Policy

### 7.1 Preconditions

All of the following must pass before Phase 4 operations are allowed:

| Condition | Failure action |
|-----------|---------------|
| Include directory exists | Provider stays Disabled, log error |
| Include support in sshd | Provider stays Disabled, log error |
| internal-sftp available | Provider stays Disabled, log error |
| sshd binary found and executable | Provider stays Disabled, log error |
| `containercp-sftp` group exists | Provider stays Disabled, log error |
| Managed authorized_keys parent dir exists | Key write fails, persist error |
| Managed user mapping is active | Key/user write fails, persist error |
| ChrootDirectory path is safe | Config write fails, persist error |
| sshd -t passes | Config write fails, rollback, persist error |
| Reload succeeds | Rollback to previous config, persist error |
| Post-reload health check passes | Rollback to previous config, persist error |

### 7.2 Partial Failure

- If config write succeeds but reload fails: roll back to previous config,
  retry reload on next reconciliation.
- If authorized_keys write fails: user is created without keys, provider
  enters Degraded state, error persisted for operator intervention.
- If single-user key update fails: other users are unaffected.
- Never partially install keys — authorized_keys file is written atomically
  (full content replaced, not appended).

---

## 8. New Files

| File | Purpose |
|------|---------|
| `libs/access/SshdConfigWriter.h` | Class for rendering, validating, installing, reloading the managed sshd include |
| `libs/access/SshdConfigWriter.cpp` | Implementation |
| `libs/access/SshdAuthorizedKeysWriter.h` | Class for rendering, validating, installing per-user authorized_keys |
| `libs/access/SshdAuthorizedKeysWriter.cpp` | Implementation |
| `libs/access/SshdDiscovery.h` | Struct for runtime discovery of sshd capabilities |
| `libs/access/SshdDiscovery.cpp` | Implementation |
| `tests/test_sshd_config_writer.cpp` | Unit tests for config writer |
| `tests/test_sshd_authorized_keys.cpp` | Unit tests for authorized keys writer |
| `tests/test_sshd_discovery.cpp` | Unit tests for discovery |
| `tests/test_sshd_integration.cpp` | Optional privileged integration tests |

### 8.1 Lifecycle Wires

- `LocalSftpProvider::create_user_internal()`: after chroot layout, call
  `SshdConfigWriter::ensure_config()` if provider is enabled and config is not
  yet installed.
- `LocalSftpProvider::remove_user_internal()`: after mapping delete, no
  immediate sshd config change (other users are unaffected).  The config file
  is only rewritten when the first user is created or a phase 4 key changes.
- `LocalSftpProvider::add_key()` / `remove_key()`: call
  `SshdAuthorizedKeysWriter::write(access_user_id)` to rebuild the specific
  user's authorized_keys file.
- `SshdConfigWriter::ensure_config()` is idempotent — it only writes if the
  file content differs from the current rendered template.

### 8.2 Runtime State Transitions

- `verify_dependencies()` adds Phase 4 checks: sshd binary, include support,
  internal-sftp, managed key directory.
- If Phase 4 dependencies pass: provider can reach Healthy with Phase 4
  features active.
- If Phase 4 dependencies fail but Phase 3 passes: provider enters Degraded
  (Phase 3 operations continue, no sshd config/keys).
- If `sshd -t` or reload fails after a successful Phase 3 startup: provider
  enters Degraded with Phase 4 error persisted; Phase 3 user access continues
  via existing config (if any) or fails closed if never installed.

---

## 9. Phase 3 Residual Tests Carried Into Phase 4

Before enabling real SFTP login, Phase 4 must include privileged Linux
coverage for these Phase 3 items:

1. Real `setfacl`/`getfacl` — create RO grant on disposable site public/,
   verify RO user can read but cannot write.
2. Real `mount --bind` — create RW grant with bind mount, verify mount
   identity matches `/proc/self/mountinfo`.
3. Real bind mount idempotency — repeat `apply_grant`, verify no duplicate
   mount.
4. Real `umount` and unmount idempotency.
5. Foreign mount detection — place a foreign bind mount at the expected
   target path, verify `apply_grant` fails closed.
6. SQLite-backed lifecycle — exercise the full grant lifecycle through real
   SQLite storage (not in-memory callbacks) in the privileged integration
   harness.
7. Real account lifecycle with sshd — full create → add key → configure sshd →
   verify `sshd -T` → reload → verify user can `sftp` → remove → verify
   `sftp` rejected.

---

## 10. Test Plan

### 10.1 Fake-State Unit Tests

| Test | Class |
|------|-------|
| Config renders correct Match block | `test_sshd_config_writer` |
| Config renders correct ChrootDirectory | `test_sshd_config_writer` |
| Config renders correct AuthorizedKeysFile | `test_sshd_config_writer` |
| Config with wrong group name rejected | `test_sshd_config_writer` |
| Config with control chars rejected | `test_sshd_config_writer` |
| Config with overlong path rejected | `test_sshd_config_writer` |
| Temp file has correct owner/mode | `test_sshd_config_writer` |
| Atomic rename preserves backup | `test_sshd_config_writer` |
| sshd -t failure triggers rollback | `test_sshd_config_writer` |
| Reload failure triggers rollback | `test_sshd_config_writer` |
| Reload not called when config unchanged | `test_sshd_config_writer` |
| Post-reload health failure triggers rollback | `test_sshd_config_writer` |
| authorized_keys renders sorted, deduplicated keys | `test_sshd_authorized_keys` |
| authorized_keys includes `restrict` prefix | `test_sshd_authorized_keys` |
| authorized_keys root:root 0600 verified | `test_sshd_authorized_keys` |
| Empty key list produces empty file | `test_sshd_authorized_keys` |
| Duplicate fingerprint skipped | `test_sshd_authorized_keys` |
| Atomic replace for keys (no append) | `test_sshd_authorized_keys` |
| Symlink target rejected | `test_sshd_authorized_keys` |
| Concurrent write guarded | `test_sshd_authorized_keys` |
| Discovery returns correct values | `test_sshd_discovery` |
| Discovery handles missing binary | `test_sshd_discovery` |
| Discovery handles missing include dir | `test_sshd_discovery` |
| Discovery handles old OpenSSH version | `test_sshd_discovery` |
| Provider dependency check includes Phase 4 deps | `test_access` |
| Provider Degraded when Phase 4 deps missing | `test_access` |
| Provider Healthy when all deps present | `test_access` |
| Key add triggers authorized_keys write | `test_access` |
| Key remove triggers authorized_keys rewrite | `test_access` |
| User create triggers sshd config write | `test_access` |
| User create with no keys succeeds | `test_access` |
| Config write failure = Degraded state | `test_access` |
| Key write failure = Degraded state | `test_access` |
| Startup reconciliation rewrites keys | `test_access` |

### 10.2 Privileged Integration Tests

These are added to the existing `arch009_linux_integration` target or a new
`arch009_linux_integration_phase4` target:

| Test | Description |
|------|-------------|
| Provision user, add key, validate sshd config | Full Phase 2→3→4 path |
| sshd -T validates managed include | Verify syntax of generated config |
| sshd reload succeeds | Verify service reload |
| ChrootDirectory owner/mode rejected if wrong | Create user with incorrect chroot mode, verify fail closed |
| AuthorizedKeysFile outside chroot works | Add key, verify sshd reads it before chroot |
| Real internal-sftp invoked | Verify ForceCommand works (not destructive) |

### 10.3 Coverage by Scope

| Scope | Coverage |
|-------|----------|
| Fake-state unit | All config rendering, validation, atomic write, rollback, key formatting, discovery |
| Fake-state integration | Provider lifecycle interactions, state transitions, dependency gating |
| Privileged | Full end-to-end with real sshd -T, reload, chroot validation |

---

## 11. Deployment Sequence

### Initial Deployment (first SFTP user)

1. Daemon starts, Phase 3 runs, provider enters Healthy.
2. Operator creates an AccessUser and adds an SSH key.
3. `create_user()` completes successfully (Phase 3): OS account, group, chroot,
   global group membership.
4. `SshdConfigWriter::ensure_config()` is called for the first time:
   - Render config from template.
   - `sshd -t` validates combined effective config.
   - Atomic write to `/etc/ssh/sshd_config.d/90-containercp-sftp.conf`.
   - `systemctl reload ssh`.
   - Verify service health.
5. `SshdAuthorizedKeysWriter::write()` is called:
   - Render keys for the user (sorted, deduplicated, with `restrict`).
   - Atomic write to `/srv/containercp/ssh/authorized_keys/au-developer`.
   - Verify owner, mode, no symlink.
6. User can now `sftp -i <key> au-developer@host`.

### Subsequent User

Steps 4 and 5 repeat — the config file may not change (template is stable),
but the key file is written for the new user.

### Key Add/Remove

Only step 5 repeats for the affected user.  No sshd reload needed — sshd
reads authorized_keys on each authentication attempt.

### User Removal

Only step 5 repeats to remove the key file.  No sshd config change needed
unless the removal leaves zero managed users.

### Daemon Restart

Phase 4 data is stateless (file-based), so no persistence-dependent recovery
is needed for sshd config.  The `SshdConfigWriter::ensure_config()` call in
startup reconciliation verifies that the file exists and is valid.  If not, it
rewrites from the canonical template.

---

## 12. Non-Goals

- Password-based SFTP authentication.
- FTP/FTPS support.
- SSH certificate-based authentication.
- SSHFP DNS records.
- Multi-node or distributed sshd configuration.
- Per-user sshd configuration (all managed users share the same Match block).
- Custom sshd ports or listen addresses.
- AuthorizedKeysCommand.
- SSH agent support for SFTP users.
- Automated user key generation (operator provides public key).

---

## 13. Rollout and Rollback Plan

### Rollout

1. **Phase 4 code implementation** — all new files, wires, tests.
2. **Fake-state test pass** — all unit and integration tests pass.
3. **Privileged integration pass** — `arch009_linux_integration` passes with
   Phase 4 scenarios on disposable host.
4. **Documentation update** — SFTP-PROVIDER.md, API_REFERENCE.md, CHANGELOG.
5. **Deploy to staging** — verify sshd -T, reload, SFTP login.
6. **Deploy to production** — standard daemon update.

### Rollback

- Remove `/etc/ssh/sshd_config.d/90-containercp-sftp.conf`.
- `systemctl reload ssh`.
- Previous config is preserved at
  `/srv/containercp/ssh/sshd_config.d/90-containercp-sftp.conf.prev`.
- If the daemon is rolled back, the sshd config file remains installed
  (harmless — the Match group has no members without ContainerCP managing
  accounts) but can be removed manually.

---

## 14. Explicit Non-Goals (from original SFTP-PROVIDER.md)

The original `docs/SFTP-PROVIDER.md` describes automatic SSH key generation
for users without `password_hash`.  Phase 4 does NOT generate SSH keys —
keys are provided by the operator.  This decision is deliberate:
- ContainerCP does not generate, store, or transmit private SSH keys.
- The user generates their own key pair and provides only the public key.
- The `AccessUser.auth_type` field default `"password"` is retained for
  backward compatibility but has no effect on SFTP access (public-key only).

---

## 15. Acceptance Criteria

Phase 4 is accepted when:

1. `SshdConfigWriter` renders the exact Match block, validates with `sshd -t`,
   installs atomically, reloads sshd, and rolls back on failure.
2. `SshdAuthorizedKeysWriter` renders sorted, deduplicated keys with `restrict`
   prefix, writes atomically with correct owner/mode, and rejects symlinks.
3. `LocalSftpProvider` wires key add/remove to authorized_keys rebuild,
   wires user create/remove to sshd config install.
4. `ServiceRegistry` wires production discovery, config writer, and key writer.
5. `verify_dependencies()` includes Phase 4 checks and transitions to Degraded
   if they fail while Phase 3 succeeds.
6. Privileged integration proves `sshd -T`, reload, and key-based SFTP access
   on a disposable host.
7. Full unit suite passes.
8. `git diff --check` passes.
9. Schema unchanged (Phase 4 uses schema v7 — no migration needed).
