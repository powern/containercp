# ARCH-009: Validation Plan

## Unit Testing

### SSH Key Validation (`test_access.cpp`)
- Valid ed25519, ecdsa, rsa keys accepted
- Malformed keys rejected (wrong prefix, invalid base64, truncated)
- DSA keys rejected
- RSA keys under 2048-bit rejected
- Duplicate fingerprints prevented
- Fingerprint calculation produces expected SHA256 output
- Key comments preserved correctly

### System Account Provisioning (`test_sftp_provider.cpp`)
- `useradd` command vector: correct username, UID, home, shell, no password
- `groupadd` command vector: correct group name, GID
- `usermod -L/-U` for disable/enable
- `userdel -r` for remove with home cleanup
- No shell metacharacters or injection in any arg
- Ownership verified before deletion (system_accounts table lookup)
- Unmanaged account conflict returns error

### Chroot and Bind Mounts (`test_sftp_provider.cpp`)
- Bind mount command vectors correct (mount --bind source target)
- Umount command vectors correct
- Grant create → supplementary group modification (usermod -aG)
- Grant revoke → group removal (gpasswd -d or usermod)
- Read-only group gets different GID from read-write group
- Cross-Site permission isolation enforced

### sshd Configuration (`test_sftp_provider.cpp`)
- Global Match block contains all security constraints
- Per-user Match block contains ChrootDirectory only
- Atomic temp-write → validate → rename or cleanup
- sshd -t validation failure preserves old config
- Config reload only after successful validation
- authorized_keys permissions: 600, owned by user
- Multiple keys concatenated correctly

### Reconciliation (`test_sftp_provider.cpp`)
- Correct state → no actions
- Missing user → create action
- Stale groups → usermod action
- Orphan managed account → remove action
- Unmanaged conflict → report, skip
- Apply is idempotent (second run produces no changes)
- Verify confirms match after apply

### SQLite Migration (`test_schema.cpp`)
- m001 → v2 upgrade succeeds with existing data
- v2 → m001 downgrade succeeds (if framework supports it)
- access_keys FK ON DELETE CASCADE works
- system_accounts UNIQUE constraint enforced
- Schema version tracked correctly

### CLI and API (`test_access.cpp`, `test_api.cpp`)
- CLI create produces expected output format
- CLI list shows enabled/disabled correctly
- API responses match standard error envelope
- No password_hash or private key in any output
- Destructive operations require confirmation

## Integration Testing (Mock Environment)

1. Create AccessUser → verify system user exists
2. Add public key → verify authorized_keys contains key
3. Grant Site READ_WRITE → verify supplementary group + bind mount
4. Grant second Site → verify both sites visible
5. Revoke first grant → verify umount + group removal
6. Disable user → verify usermod -L, auth fails
7. Re-enable user → verify usermod -U, auth succeeds
8. Revoke key → verify key removed from authorized_keys
9. Add second key → verify both keys in authorized_keys
10. Reconcile without changes → verify no-op
11. Remove user → verify userdel, home gone, groups cleaned
12. Site removal cascades to grant cleanup
13. Daemon restart → reconciler detects missing bind mounts

## End-to-End Production Validation

On a clean Debian 13 VM with a real SFTP client:

1. Create Access user → `containercp access user create dev`
2. Add public key → `containercp access key add dev "ssh-ed25519 AAAAC3..."`
3. Grant one Site as read-only → `containercp access grant create dev example.com read_only`
4. Confirm SFTP login succeeds: `sftp -i ~/.ssh/id_ed25519 au-dev@host`
5. Confirm `ls` shows `sites/example.com/`
6. Confirm `get` succeeds (download)
7. Confirm `put` and `rm` fail (read-only)
8. Upgrade grant to read-write → `containercp access grant create dev example.com read_write`
9. Confirm `put` and `rm` succeed in `sites/example.com/`
10. Confirm `put` outside sites/ fails (chroot)
11. Grant second Site → verify both `sites/example.com/` and `sites/myapp.org/` visible
12. Revoke first grant → verify `sites/example.com/` disappears
13. Disable user → confirm auth fails
14. Re-enable user → confirm auth succeeds
15. Reconcile → verify no errors
16. Remove user → confirm auth fails, Site files intact
17. Restart containercpd → confirm reconcile preview shows correct state
18. Apply reconcile → confirm idempotent
19. Reboot host → confirm access state correct after boot
20. Add revoked key → confirm auth fails with revoked key
21. Remove key → confirm key no longer accepted
22. Full test suite passes → 0 regressions

## Secret Redaction Audit

- [ ] No private keys in logs, API responses, CLI output, or test fixtures
- [ ] No password hashes in public APIs
- [ ] No authorized_keys content in logs
- [ ] No useradd/groupadd command output with sensitive data in logs
- [ ] No raw /etc/shadow data exposed
