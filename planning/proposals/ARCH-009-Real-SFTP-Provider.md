# ARCH-009: Real SFTP Provider

## Executive Summary

Convert the existing `LocalSftpProvider` from a no-op placeholder into a real, secure, production-capable
local SFTP provider. This is the final implementation piece for the Access/SFTP subsystem architected in
Sprint 6 (CCP-4001–CCP-4006) and deferred to v0.7.0.

The provider will synchronise ContainerCP Access resources (users, grants, public keys) with real
Linux system accounts, group membership, OpenSSH `internal-sftp` chroot jails, and `authorized_keys` files.

## Verified Repository Findings

| Area | Finding |
|------|---------|
| `AccessUser` model | Has `id`, `username`, `auth_type` (default `"password"`), `password_hash`, `enabled`. No SSH key fields. No `site_id` (unlike CCP-4001 spec). |
| `AccessGrant` model | Has `id`, `access_user_id`, `site_id`, `permission` (READ_ONLY, READ_WRITE, DEPLOY). FK ON DELETE RESTRICT on both. |
| `AccessProvider` interface | 6 pure virtual methods: `create_user`, `remove_user`, `enable_user`, `disable_user`, `list_users`, `show_user`. Returns `OperationResult`. |
| `LocalSftpProvider` | Every method is a stub: logs and returns `{true, ""}`. No system calls. |
| CLI commands | Only `access-user-list` and `access-grant-list` implemented. Create/show/disable/enable/remove are NOT wired. |
| REST API | Only `GET /api/access-users` (list) and `POST /api/access-users/remove`. No create, grant, key endpoints. |
| Web UI | `web/pages/access.js` — list users, remove button. No grant/key management UI. |
| SQLite schema | `access_users` (7 columns), `access_grants` (6 columns). No `access_keys` table. FK enforced on grants. |
| Site layout | `/srv/containercp/sites/<domain>/` with `public/`, `logs/`, `tmp/`, `config/`, `ssl/`, `backups/`, `.env`, `docker-compose.yml`. |
| Container UIDs | php:33 (www-data), web:101 (nginx), db:999 (mysql), redis:999. No `user:` directive generated. |
| Command execution | `CommandExecutor` with `fork()+execvp()` — shell-injection safe. Used extensively. `DockerRuntime` still uses `std::system()` (known debt). |
| SSH key support | Does not exist anywhere in the codebase. Zero references in Access model, storage, provider, CLI, API, or Web UI. |
| System user/group management | Does not exist. `UserManager` creates in-process records only. |

## Goals

1. Real Linux system user and group lifecycle (`useradd`, `groupadd`, `usermod`, `userdel`, `groupdel`)
2. Stable UID/GID allocation persisted in SQLite
3. OpenSSH `internal-sftp` chroot per Access user, with bind-mounted Site paths
4. Public-key-only authentication; server-generated private keys **excluded**
5. SSH public key CRUD with validation and fingerprinting
6. Grant-aware filesystem permissions via supplementary Linux groups
7. Idempotent desired-state reconciliation (explicit, not at daemon startup)
8. Transactional sshd configuration generation with `sshd -t` validation
9. Graceful failure handling — fail closed, never destroy unmanaged state
10. Full CLI, REST API, and Web UI parity for all operations

## Non-Goals

- Password authentication for SFTP — excluded from ARCH-009
- Server-side private key generation — excluded
- Classic FTP, FTPS, WebDAV
- Interactive SSH shell, SCP, tunneling, port forwarding, remote commands
- Multi-node or high-availability SFTP
- LDAP/AD authentication
- fail2ban management
- Full audit-log epic
- `site_id=0` (admin panel) SFTP access — default deny
- `DEPLOY` permission filesystem semantics beyond read-write equivalent (may be refined later)

## Evaluated Architecture Options

### Option A: One system account per AccessUser with bind-mounted multi-Site chroot (SELECTED)

- One Linux user `au-<username>`, home = `/srv/containercp/users/au-<username>/`
- Under home: `sites/<domain>/` → bind-mount of `.../sites/<domain>/public/`
- Chroot to home directory → user sees only granted sites
- Supplementary groups: `site-<id>` for each granted Site
- READ_ONLY → `site-<id>-ro`, READ_WRITE → `site-<id>-rw`

**Pros:** One account per human user, natural mapping, OpenSSH Match User possible.
**Cons:** Requires bind-mount lifecycle management.

### Option B: One system account per AccessUser+Site grant

- Separate Linux user per grant: `au-<username>-<site_id>`
- Single-Site chroot, simpler permission model
- No bind-mounts needed

**Pros:** Simpler chroot, no bind mounts.
**Cons:** Multiple system accounts per human user, explosion of system accounts, poor user experience (different usernames per site), disconnect from real usage patterns.

### Option C: Bind-mount all public/ dirs into a common chroot

- Same as A but with ACLs on the bind mounts inside the chroot
- POSIX ACLs per directory

**Pros:** Fine-grained permissions possible.
**Cons:** Higher complexity, ACL dependency, harder to audit.

**Decision: Option A** — one account per Access user, bind-mounted Site paths.

## System Identity Model

### AccessUser → Linux user

```
AccessUser.username  →  "au-" + normalized_username
```

Normalization: `[^a-z0-9_-]` → `_`, lowercase, max 32 chars, must start with letter.

Collision: if `au-<username>` exists and is NOT owned by ContainerCP → error, reported as `unmanaged_account_conflict`.

### UID/GID allocation

Persisted in new `system_accounts` SQLite table:

| Column | Type | Purpose |
|--------|------|---------|
| `entity_type` | TEXT | `"access_user"` or `"site_group_rw"` or `"site_group_ro"` |
| `entity_id` | INTEGER | FK (AccessUser.id or Site.id) |
| `uid` | INTEGER | Allocated UID (or NULL for groups) |
| `gid` | INTEGER | Allocated GID |
| `username` | TEXT | Canonical system username |
| `groupname` | TEXT | Canonical system group name |

Allocation: UID range 10000–19999, GID range 20000–29999. Sequential, reused on deletion.

### Linux groups per Site

| Grant permission | Linux group |
|-----------------|-------------|
| READ_WRITE | `site-<site_id>-rw` |
| READ_ONLY | `site-<site_id>-ro` |
| DEPLOY | `site-<site_id>-rw` (same as read-write initially) |

The `site-<site_id>-rw` group owns `public/` directory (`chgrp site-<id>-rw ./public`).

## Multi-Site Chroot Design

### Directory layout

```
/srv/containercp/users/au-developer/           (home, chroot root — root:root 755)
├── sites/
│   ├── example.com/                           (bind-mount .../sites/example.com/public/)
│   └── myapp.org/                             (bind-mount .../sites/myapp.org/public/)
├── .ssh/                                      (au-developer:au-developer 700)
│   └── authorized_keys                        (au-developer:au-developer 600)
└── uploads/                                   (writable scratch, au-developer:au-developer 700)
```

### Bind-mount lifecycle

- **Grant create** → `mkdir -p`, then `mount --bind .../public/ .../users/au-dev/sites/example.com/`, then `mount --make-slave`
- **Grant revoke** → `umount`, then `rmdir`
- **Site removal** → revoke all grants first (FK RESTRICT ensures order)
- **User removal** → umount all binds, then `rm -rf` home
- **Daemon restart** → reconciler remounts missing binds from persisted desired state

## Permission Enforcement

| Permission | Group | public/ mode | Behaviour |
|-----------|-------|-------------|-----------|
| READ_ONLY | `site-<id>-ro` | `550` (r-x) | Download, list, traverse. No upload/delete/rename. |
| READ_WRITE | `site-<id>-rw` | `770` (rwx) | Full read/write including upload, delete, rename, mkdir. |
| DEPLOY | `site-<id>-rw` | `770` (rwx) | Same as read-write for now. Deploy semantics deferred. |

Access user primary group is `au-<username>`. Supplementary groups include `site-<id>-ro` or `site-<id>-rw` per grant.

Enforcement mechanism: **standard UNIX user/group ownership + mode bits**. No POSIX ACLs in initial implementation. No read-only bind mounts.

## Public Key Model

### New `access_keys` SQLite table

| Column | Type | Purpose |
|--------|------|---------|
| `id` | INTEGER PK | Auto-increment |
| `access_user_id` | INTEGER NOT NULL | FK → access_users(id) ON DELETE CASCADE |
| `key_type` | TEXT NOT NULL | e.g. `ssh-ed25519`, `ssh-rsa`, `ecdsa-sha2-nistp256` |
| `key_data` | TEXT NOT NULL | Base64-encoded key material |
| `key_comment` | TEXT | Optional comment |
| `fingerprint` | TEXT NOT NULL | SHA256 fingerprint |
| `enabled` | INTEGER DEFAULT 1 | Key active/revoked |
| `created_at` | TEXT | ISO-8601 |
| `updated_at` | TEXT | ISO-8601 |

UNIQUE constraint on `(access_user_id, fingerprint)` to prevent exact duplicates.

### Accepted algorithms

`ssh-ed25519`, `ecdsa-sha2-nistp256`, `ecdsa-sha2-nistp384`, `ecdsa-sha2-nistp521`, `ssh-rsa` (SHA256 fingerprint only, minimum 2048-bit).

Rejected: `ssh-dss` (DSA), `ssh-rsa` with SHA1 fingerprints, keys under 2048-bit, malformed keys.

### Key operations

- **Add**: validate syntax + fingerprint → check duplicate → INSERT → rebuild authorized_keys
- **Revoke**: set enabled=0 → rebuild authorized_keys
- **Delete**: DELETE → rebuild authorized_keys
- **Authorized_keys rebuild**: atomic write to temp file → `rename()` → `chmod 600` + `chown <user>:<user>`

### Exclusion of server-side key generation

ContainerCP does NOT generate, store, or transmit private SSH keys. The user generates their own key pair and provides only the public key. The `AccessUser.auth_type` field `"password"` default is retained in the schema for backward compatibility but has no effect on SFTP access (public-key only).

## OpenSSH Integration

### Configuration strategy

**Single include file:** `/etc/ssh/sshd_config.d/99-containercp-sftp.conf`

One `Match Group` block for global defaults, one `Match User` block per Access user (for `ChrootDirectory`).  

Global Match block applies to all managed groups:
```
Match Group containercp-sftp
    ForceCommand internal-sftp
    PermitTTY no
    X11Forwarding no
    AllowAgentForwarding no
    AllowTcpForwarding no
    PermitTunnel no
    GatewayPorts no
```

Per-user Match blocks:
```
Match User au-developer
    ChrootDirectory /srv/containercp/users/au-developer
```

Where `containercp-sftp` is the primary group of every managed Access user AND a supplementary group for deployment-wide config.

### Atomic generation

1. Build new config content in-memory
2. Write to `/etc/ssh/sshd_config.d/.99-containercp-sftp.conf.tmp`
3. `chmod 644` temp file
4. Run `sshd -t` to validate
5. On success: `rename()` temp → final
6. On failure: delete temp, return error
7. Run `systemctl reload sshd` (or `kill -HUP`)

Never write a partial or invalid config. Never reload without validation.

### Include directive requirement

Requires OpenSSH ≥ 7.3 (standard on Debian 10+). At install time, verify `/etc/ssh/sshd_config` contains `Include /etc/ssh/sshd_config.d/*.conf`. If not, installation fails with instructions.

## Reconciliation Model

### Explicit, not automatic at startup

Reconciliation is explicit via CLI: `containercp sftp reconcile [--apply]`.

At daemon startup, the provider logs current state but takes no action. This prevents daemon restart from becoming a destructive host-account cleanup event.

### Reconciliation operations

```
plan   → inspect desired state (SQLite) vs observed state (OS), report diff
apply  → execute plan, making observed state match desired state
verify → re-inspect and confirm match
```

### State classification

| Desired | Observed | Classification | Action |
|---------|----------|---------------|--------|
| user | exists, correct | OK | none |
| user | missing | MISSING | create |
| user | exists, wrong groups | STALE | usermod |
| — | exists, managed by CCP, no desired record | ORPHAN | remove |
| — | exists, NOT managed by CCP | CONFLICT | report, skip |

Managed ownership is proven by the `system_accounts` table entry.

## Transaction and Rollback

### Per-operation workflow

```
preflight        → validate input, check dependencies
acquire_lock     → prevent concurrent mutations (file lock on provider state)
stage            → create temp files, prepare groups, test sshd -t
commit           → finalize changes
post_verify      → confirm OS state
rollback         → if any stage fails, undo staged changes
```

### Rollback boundaries

- `useradd` succeeds but `groupadd` fails → `userdel` the created user
- `groupadd` succeeds but `usermod` fails → usermod is retried; if persistent, report but keep the group
- `authorized_keys` write fails → retry; leave existing keys intact
- `sshd -t` fails → delete temp config, do NOT reload
- `mount --bind` fails → report, skip, allow retry in reconcile

Never report success if OS state is inconsistent.

## SQLite Schema Changes

### Migration m002 (new)

```sql
CREATE TABLE IF NOT EXISTS access_keys (
    id              INTEGER PRIMARY KEY,
    access_user_id  INTEGER NOT NULL REFERENCES access_users(id) ON DELETE CASCADE,
    key_type        TEXT NOT NULL,
    key_data        TEXT NOT NULL,
    key_comment     TEXT NOT NULL DEFAULT '',
    fingerprint     TEXT NOT NULL,
    enabled         INTEGER NOT NULL DEFAULT 1,
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    UNIQUE(access_user_id, fingerprint)
);

CREATE TABLE IF NOT EXISTS system_accounts (
    entity_type     TEXT NOT NULL,
    entity_id       INTEGER NOT NULL,
    uid             INTEGER,
    gid             INTEGER NOT NULL,
    username        TEXT NOT NULL,
    groupname       TEXT NOT NULL,
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    PRIMARY KEY (entity_type, entity_id)
);
```

`access_grants` FK changed from `ON DELETE RESTRICT` to `ON DELETE CASCADE` for `access_user_id` only (user deletion must clean up grants). `site_id` RESTRICT is preserved.

## Provider and Service Boundaries

```
AccessProvider (abstract)
  └── LocalSftpProvider (real implementation)
       ├── uses CommandExecutor for useradd/groupadd/usermod
       ├── uses Filesystem for directory/mount operations
       └── uses SshKeyValidator for key validation
            └── SshdConfigWriter (generates sshd_config drop-in)

DatabaseSqlConsoleService → stays as is (no changes)
```

New components:

| Component | Responsibility |
|-----------|---------------|
| `SshKeyValidator` | Parse, validate, fingerprint public keys |
| `SshdConfigWriter` | Generate, validate, atomically apply sshd_config.d drop-in |
| `SftpReconciler` | Compare desired vs observed state, plan, apply |
| `SftpBindMountManager` | Create/remove per-site bind mounts |
| `SystemAccountAllocator` | Allocate UID/GID, persist to system_accounts |

## CLI Changes

| Command | Status |
|---------|--------|
| `containercp access user list` | ✅ Exists |
| `containercp access user create <username>` | 🔧 Wire create + provider call |
| `containercp access user show <username>` | 🔧 Wire show |
| `containercp access user disable <username>` | 🔧 Wire disable |
| `containercp access user enable <username>` | 🔧 Wire enable |
| `containercp access user remove <username>` | 🔧 Wire remove |
| `containercp access grant list` | ✅ Exists |
| `containercp access grant create <username> <domain> <perm>` | NEW |
| `containercp access grant revoke <grant_id>` | NEW |
| `containercp access key list <username>` | NEW |
| `containercp access key add <username> <pubkey>` | NEW |
| `containercp access key remove <username> <key_id>` | NEW |
| `containercp access key revoke <username> <key_id>` | NEW |
| `containercp sftp status` | NEW |
| `containercp sftp reconcile` | NEW |
| `containercp sftp reconcile --apply` | NEW |
| `containercp sftp validate-config` | NEW |

## REST API Changes

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/api/access-users` | List (exists) |
| `POST` | `/api/access-users/create` | Create user |
| `POST` | `/api/access-users/remove` | Remove (exists) |
| `GET` | `/api/access-users/<id>` | Show user with grants |
| `POST` | `/api/access-users/<id>/disable` | Disable user |
| `POST` | `/api/access-users/<id>/enable` | Enable user |
| `GET` | `/api/access-grants` | List grants |
| `POST` | `/api/access-grants/create` | Create grant |
| `POST` | `/api/access-grants/<id>/revoke` | Revoke grant |
| `GET` | `/api/access-users/<id>/keys` | List keys |
| `POST` | `/api/access-users/<id>/keys` | Add public key |
| `DELETE` | `/api/access-users/<id>/keys/<key_id>` | Remove key |
| `POST` | `/api/access-users/<id>/keys/<key_id>/revoke` | Revoke key |
| `GET` | `/api/sftp/status` | Provider health + state summary |
| `POST` | `/api/sftp/reconcile` | Preview reconciliation |
| `POST` | `/api/sftp/reconcile/apply` | Apply reconciliation |

No `password_hash`, private keys, raw authorized_keys, or `/etc/shadow` data in any response.

## Web UI Changes

Extend `web/pages/access.js` with:

- Access user detail drawer (username, status, keys, grants)
- Grant management (add site with permission dropdown, revoke)
- Key management (add public key textarea, list with fingerprints, revoke)
- Provider status indicator (enabled/disabled, last reconcile)

## Configuration

```cpp
struct SftpConfig {
    bool enabled = true;                    // SFTP_ENABLED env
    std::string username_prefix = "au-";    // SFTP_USER_PREFIX
    std::string managed_group = "containercp-sftp";
    std::string chroot_base = "/srv/containercp/users/";
    std::string sshd_include = "/etc/ssh/sshd_config.d/99-containercp-sftp.conf";
    std::string authorized_keys_dir;        // derived from chroot_base
    int uid_range_start = 10000;
    int gid_range_start = 20000;
};
```

## Debian 13 Dependencies

- `openssh-server` — already installed on all Debian systems
- `util-linux` — provides `mount`, `umount` (already standard)
- `passwd` / `shadow` — provides `useradd`, `groupadd`, `usermod`, `userdel`, `groupdel` (already standard)
- No additional packages needed beyond the existing build set

## Testing Strategy

### Unit tests (no real OS state)

- SSH key validation (valid, invalid, duplicate, deprecated algorithms)
- Fingerprint calculation
- Username normalization and collision detection
- sshd config generation and temp-file atomic write
- Command arg vector correctness (no shell, no injection)
- Provider state machine (create, remove, enable, disable)
- SQLite migration (m002)

### Integration tests (isolated environment)

- Mock `CommandExecutor` + fixture filesystem for user/group creation
- Bind-mount lifecycle on temp dirs
- Reconciliation state machine
- API response format
- CLI output format
- Web UI component rendering (static analysis)

## Risks

| Risk | Mitigation |
|------|-----------|
| sshd reload failure | Validate with `sshd -t` before reload; keep old config on failure |
| Bind mount accumulation | Reconciler detects orphaned mounts and cleans up |
| UID/GID collision with existing users | Reserved range 10000+ avoids system user conflicts |
| Daemon crash during mutation | Reconciler detects partial state on next explicit reconcile |
| Key validation bypass | Parser rejects non-compliant keys before storage |

## Product-Owner Decisions Required

1. **Confirm: one system account per AccessUser with bind-mounted multi-Site chroot** (Option A)
2. **Confirm: public-key-only authentication, no server-side key generation**
3. **Confirm: DEPLOY = READ_WRITE for filesystem purposes** in ARCH-009
4. **Confirm: explicit reconciliation (daemon startup does NOT reconcile)**
5. **Confirm: Admin Panel (site_id=0) blocked from SFTP access by default**
6. **Confirm: `access_grants.access_user_id` FK changed to ON DELETE CASCADE**
7. **Confirm: existing `auth_type` field retained as-is, no migration needed now**
8. **Approve the SQLite schema migration m002**

## References

- `docs/SFTP-PROVIDER.md` — original design proposal (superseded by this document)
- `libs/access/` — current Access subsystem implementation
- `libs/storage/SchemaMigrations.cpp` — current schema (v1)
- `libs/runtime/CommandExecutor.{h,cpp}` — approved command execution abstraction
- `planning/roadmap-post-v0.6.0.md` — ARCH-009 definition
