# ARCH-009: Implementation Checklist

## Phase 1 — Public Key Model and Validation
- [ ] Schema migration m002 (access_keys + system_accounts tables)
- [ ] AccessKey struct (key_type, key_data, key_comment, fingerprint, enabled)
- [ ] AccessKeyManager (create, remove, list_by_user, find, set_keys)
- [ ] SshKeyValidator (validate syntax, extract type, compute SHA256 fingerprint)
- [ ] Wire to ServiceRegistry (access_keys() → AccessKeyManager&)
- [ ] Wire to Storage (save_access_keys, load_access_keys, SQLite round-trip)
- [ ] Wire to LegacyImporter (migration from TXT if applicable)
- [ ] Unit tests: key validation, fingerprint, duplicates, deprecated algorithms
- [ ] Schema migration tests: upgrade m001→m002, downgrade m002→m001
- [ ] Full test suite: 0 regressions

## Phase 2 — System Account Provisioning
- [ ] SystemAccountAllocator (UID/GID range, sequential allocation, persistence)
- [ ] LocalSftpProvider::create_user → useradd + groupadd via CommandExecutor
- [ ] LocalSftpProvider::remove_user → userdel + groupdel (ownership-verified)
- [ ] LocalSftpProvider::enable_user → usermod -U
- [ ] LocalSftpProvider::disable_user → usermod -L
- [ ] User home directory creation with correct permissions
- [ ] Tests: mock CommandExecutor, verify command vectors
- [ ] Tests: ownership verification before deletion
- [ ] Tests: unmanaged account conflict detection
- [ ] Full test suite: 0 regressions

## Phase 3 — Chroot, Bind Mounts, and Grants
- [ ] SftpBindMountManager (mkdir, mount --bind, umount, rmdir)
- [ ] Grant create → per-site group, supplementary group, bind mount
- [ ] Grant revoke → umount, remove from supplementary groups
- [ ] site-<id>-rw group ownership of public/ directory
- [ ] Read-only grant → site-<id>-ro group, 550 permissions
- [ ] Multi-Site: user sees all granted sites under ~/sites/<domain>/
- [ ] Site removal cleanup (grant revoke before FK cascade)
- [ ] Tests: bind-mount lifecycle, grant churn, incorrect permission attempts
- [ ] Full test suite: 0 regressions

## Phase 4 — sshd Configuration and authorized_keys
- [ ] SshdConfigWriter (atomic temp-file, sshd -t validation, rename)
- [ ] Match Group global block (ForceCommand internal-sftp, no TTY, no forwarding)
- [ ] Per-user Match User blocks for ChrootDirectory
- [ ] authorized_keys generation (temp write, rename, chmod 600)
- [ ] Key add/remove/revoke triggers authorized_keys rebuild
- [ ] User create/remove triggers sshd config update
- [ ] Validation failure → old config preserved, error returned
- [ ] sshd reload only after validation passes
- [ ] Tests: config generation, validation, key file permissions
- [ ] Full test suite: 0 regressions

## Phase 5 — Reconciliation
- [ ] SftpReconciler (desired state from SQLite, observed from OS)
- [ ] State classification (OK, MISSING, STALE, ORPHAN, CONFLICT)
- [ ] Plan operation (diff output)
- [ ] Apply operation (idempotent repair)
- [ ] Verify operation (post-apply confirmation)
- [ ] Unmanaged accounts skipped, reported
- [ ] Orphan cleanup (only managed accounts)
- [ ] Tests: state comparison, idempotent apply, conflict handling
- [ ] Full test suite: 0 regressions

## Phase 6 — CLI Commands
- [ ] access user create → create resource + provider call + output username
- [ ] access user show → display user with grant list, key list, status
- [ ] access user disable → set enabled=false + provider.disable_user()
- [ ] access user enable → set enabled=true + provider.enable_user()
- [ ] access user remove → provider.remove_user() + resource.remove()
- [ ] access grant create → create grant + provider call
- [ ] access grant revoke → revoke grant + provider cleanup
- [ ] access key list → display keys with fingerprint + status
- [ ] access key add → validate + store + rebuild authorized_keys
- [ ] access key remove → delete + rebuild authorized_keys
- [ ] access key revoke → disable + rebuild authorized_keys
- [ ] sftp status → provider health, account count, last reconcile
- [ ] sftp reconcile → preview diff
- [ ] sftp reconcile --apply → apply reconciliation
- [ ] sftp validate-config → sshd -t test
- [ ] Tests: CLI output format, error codes, destructive operation confirmation
- [ ] Full test suite: 0 regressions

## Phase 7 — REST API
- [ ] POST /api/access-users/create
- [ ] GET /api/access-users/<id>
- [ ] POST /api/access-users/<id>/disable
- [ ] POST /api/access-users/<id>/enable
- [ ] GET /api/access-grants
- [ ] POST /api/access-grants/create
- [ ] POST /api/access-grants/<id>/revoke
- [ ] GET /api/access-users/<id>/keys
- [ ] POST /api/access-users/<id>/keys
- [ ] DELETE /api/access-users/<id>/keys/<key_id>
- [ ] POST /api/access-users/<id>/keys/<key_id>/revoke
- [ ] GET /api/sftp/status
- [ ] POST /api/sftp/reconcile
- [ ] POST /api/sftp/reconcile/apply
- [ ] Tests: API response format, error envelopes, idempotency
- [ ] No secrets in any response
- [ ] Full test suite: 0 regressions

## Phase 8 — Web UI
- [ ] Access user detail drawer
- [ ] Grant list with permission badges and revoke button
- [ ] Grant create modal (site dropdown, permission dropdown)
- [ ] Key list with fingerprint display
- [ ] Key add modal (public key textarea)
- [ ] Key revoke button
- [ ] SFTP provider status indicator (enabled, account count, last reconcile)
- [ ] Disabled user visual distinction
- [ ] Tests: static source analysis for secret leaks
- [ ] Full test suite: 0 regressions

## Phase 9 — Integration, Documentation, Validation
- [ ] End-to-end mock tests (full lifecycle)
- [ ] SFTP-PROVIDER.md updated
- [ ] API_REFERENCE.md updated
- [ ] single-source-of-truth.md updated
- [ ] runtime-architecture.md updated (if needed)
- [ ] CHANGELOG.md entries for each phase
- [ ] project-status.md → ARCH-009 marked complete
- [ ] Production validation: clean Debian 13 VM, real SFTP client
- [ ] 22 end-to-end scenarios from validation plan
- [ ] All existing tests still pass
