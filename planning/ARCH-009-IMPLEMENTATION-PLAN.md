# ARCH-009: Implementation Plan

## Phase 1 — Public Key Model and Validation

**Objective:** Add SSH public key storage, validation, and fingerprinting without modifying the provider.

**Code areas:** `libs/access/`, `libs/storage/SchemaMigrations.cpp`, `tests/`

| Step | Action | File |
|------|--------|------|
| 1.1 | Add `access_keys` SQLite table (schema v2) | `SchemaMigrations.cpp` |
| 1.2 | Add `AccessKey` struct + `AccessKeyManager` (CRUD) | `libs/access/AccessKey.{h,cpp}`, `AccessKeyManager.{h,cpp}` |
| 1.3 | Add `SshKeyValidator` (parse, validate, fingerprint) | `libs/access/SshKeyValidator.{h,cpp}` |
| 1.4 | Wire to `ServiceRegistry` + persistence | `ServiceRegistry.{h,cpp}`, `Storage/SQLiteStorage` |
| 1.5 | Tests | `tests/test_access.cpp`, `tests/test_schema.cpp` |

**Acceptance:** Keys can be created, listed, revoked, deleted via in-memory manager. SSH public keys are validated and fingerprinted. Schema migration passes upgrade/downgrade. All existing tests pass.

## Phase 2 — System Account Provisioning

**Objective:** Create real Linux users and groups via `CommandExecutor`.

**Code areas:** `libs/access/`, `libs/core/ServiceRegistry.cpp`

| Step | Action | File |
|------|--------|------|
| 2.1 | Add `SystemAccountAllocator` (UID/GID allocation + persistence) | `libs/access/SystemAccountAllocator.{h,cpp}` |
| 2.2 | Refactor `LocalSftpProvider::create_user` to call `useradd`, `groupadd` | `LocalSftpProvider.cpp` |
| 2.3 | Implement `remove_user` → `userdel`, `groupdel` (ownership-verified) | `LocalSftpProvider.cpp` |
| 2.4 | Implement `enable_user` → `usermod -U` (unlock) | `LocalSftpProvider.cpp` |
| 2.5 | Implement `disable_user` → `usermod -L` (lock) | `LocalSftpProvider.cpp` |
| 2.6 | Tests (mock CommandExecutor) | `tests/test_sftp_provider.cpp` |

**Acceptance:** User create/remove/enable/disable produce correct `useradd`/`userdel`/`usermod` command vectors. No shell injection. Ownership verified before deletion. UID/GID persisted.

## Phase 3 — Chroot, Bind Mounts, and Grants

**Objective:** Set up per-User chroot home with bind-mounted Site paths. Wire grant lifecycle.

**Code areas:** `libs/access/`, `libs/core/`

| Step | Action | File |
|------|--------|------|
| 3.1 | Add `SftpBindMountManager` (mount/umount lifecycle) | `libs/access/SftpBindMountManager.{h,cpp}` |
| 3.2 | Implement grant create → create group, add supplementary group, bind-mount | `SftpGrantReconciler.{h,cpp}` |
| 3.3 | Implement grant revoke → umount, remove supplementary group | `SftpGrantReconciler` |
| 3.4 | Create per-Site `site-<id>-rw` / `site-<id>-ro` groups with correct ownership | Chroot setup |
| 3.5 | Tests | `tests/test_sftp_provider.cpp` |

**Acceptance:** Grant create sets up bind mount and group membership. Grant revoke tears down cleanly. Site group creation idempotent. Bind mounts survive daemon restart (reconciler handles remount).

## Phase 4 — sshd Configuration and authorized_keys

**Objective:** Generate OpenSSH config, manage `authorized_keys`, validate with `sshd -t`.

**Code areas:** `libs/access/`

| Step | Action | File |
|------|--------|------|
| 4.1 | Add `SshdConfigWriter` (atomic temp-file write, `sshd -t`, reload) | `libs/access/SshdConfigWriter.{h,cpp}` |
| 4.2 | Wire key add/remove/revoke to authorized_keys rebuild | `LocalSftpProvider` |
| 4.3 | Wire user create/remove to sshd config update | `LocalSftpProvider` |
| 4.4 | Tests | `tests/test_sftp_provider.cpp` |

**Acceptance:** sshd config generated atomically. `sshd -t` validation before reload. authorized_keys correct permissions and content. Multiple keys per user. Key revocation prevents auth.

## Phase 5 — Reconciliation

**Objective:** Inspect desired vs observed state, plan, apply.

**Code areas:** `libs/access/`

| Step | Action | File |
|------|--------|------|
| 5.1 | Add `SftpReconciler` (state comparison + classification) | `libs/access/SftpReconciler.{h,cpp}` |
| 5.2 | Implement `plan` → diff output | `SftpReconciler` |
| 5.3 | Implement `apply` → idempotent repair | `SftpReconciler` |
| 5.4 | Tests | `tests/test_sftp_provider.cpp` |

**Acceptance:** Reconciler detects missing/extra/stale state correctly. Apply is idempotent. Unmanaged accounts are skipped and reported.

## Phase 6 — CLI Commands

**Objective:** Wire all CLI commands in DaemonApp.

**Code areas:** `libs/daemon/DaemonApp.cpp`

| Step | Action |
|------|--------|
| 6.1 | Wire `access user create/show/disable/enable/remove` |
| 6.2 | Wire `access grant create/revoke` |
| 6.3 | Wire `access key list/add/remove/revoke` |
| 6.4 | Wire `sftp status/reconcile/validate-config` |
| 6.5 | Tests |

## Phase 7 — REST API

**Objective:** Full REST API for Access users, grants, keys, and SFTP operations.

**Code areas:** `libs/api/ApiServer.cpp`

| Step | Action |
|------|--------|
| 7.1 | Add create/show/disable/enable endpoints for Access users |
| 7.2 | Add grant list/create/revoke endpoints |
| 7.3 | Add key list/add/remove/revoke endpoints |
| 7.4 | Add `/api/sftp/status` and reconcile endpoints |
| 7.5 | Tests |

## Phase 8 — Web UI

**Objective:** Extend Access page with grants, keys, and provider status.

**Code areas:** `web/pages/access.js`, `web/core/`

| Step | Action |
|------|--------|
| 8.1 | User detail drawer (keys, grants, enabled state) |
| 8.2 | Grant management UI (add/revoke per Site) |
| 8.3 | Key management UI (add/revoke with fingerprint display) |
| 8.4 | SFTP provider status widget |
| 8.5 | Determine: tests |

## Phase 9 — Integration, Documentation, Validation

| Step | Action |
|------|--------|
| 9.1 | End-to-end tests (mock environment) |
| 9.2 | Documentation (SFTP-PROVIDER.md, API_REFERENCE.md, SSOT, runtime-architecture) |
| 9.3 | CHANGELOG entries |
| 9.4 | Production validation on clean Debian 13 VM |
| 9.5 | Full end-to-end: create user → add key → grant site → login → upload → revoke → reconcile → remove |

## Dependencies

```
Phase 1 (Keys)
  ↓
Phase 2 (System Accounts)
  ↓
Phase 3 (Chroot + Grants)
  ↓
Phase 4 (sshd + authorized_keys)
  ↓
Phase 5 (Reconciliation)
  ↓
Phase 6 (CLI)  →  Phase 7 (API)  →  Phase 8 (Web UI)
  ↓
Phase 9 (Integration + Docs)
```

Phases 6-7-8 can start in parallel after Phase 5 provides the core service layer.
