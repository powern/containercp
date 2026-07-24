# ARCH-009 Phase 3 — Site Grant Groups & Chroot Foundation

## Final Implementation Specification

**Status:** DESIGN — AWAITING IMPLEMENTATION APPROVAL
**Base:** `5779316` fix(arch-009): make fake test infrastructure model real OS state evolution
**Revision:** 2

---

## 1. Architecture Diagram

```
                     SQLite (SSOT)
                          │
          ┌───────────────┼───────────────┐
          │               │               │
    access_users    access_grants   system_accounts
          │               │           (entity_type,
          │          permission       entity_id, gid,
          │        (ro/rw/deploy)     username, groupname,
          │               │           state)
          │               │               │
          ▼               ▼               ▼
    ┌─────────────────────────────────────────┐
    │         AccessProvider                   │
    │  (LocalSftpProvider)                    │
    │                                         │
    │  create_user ──→ apply_pending_grants   │
    │  remove_user ──→ revoke_all_grants      │
    │  apply_grant   ──→ group + mount + ACL  │
    │  revoke_grant  ──→ umount + groupdel    │
    └──────────┬──────────────────────────────┘
               │
               ▼
    ┌──────────────────────┐
    │   Linux OS Layer     │
    │                      │
    │  groupadd site-1-rw  │──→ /etc/group
    │  usermod -a -G       │──→ supplementary groups
    │  chgrp site-1-rw     │──→ public/ ownership
    │  setfacl -m g:...:rx │──→ read-only ACL
    │  mount --bind        │──→ chroot bind mounts
    └──────────┬───────────┘
               │
               ▼
    ┌──────────────────────────┐
    │   OpenSSH Chroot         │
    │                          │
    │  /srv/containercp/users/ │
    │    au-<user>/            │
    │    ├── sites/            │
    │    │   ├── example.com/  │ (bind mount)
    │    │   └── myapp.org/    │ (bind mount)
    │    └── .ssh/             │ (Phase 4)
    └──────────────────────────┘
```

---

## 2. Complete Chroot Directory Layout

```
/srv/containercp/users/au-<username>/     ← ChrootDirectory
│
├── sites/                                ← root:root  0755  created by create_user
│   │                                        never removed until remove_user
│   │
│   ├── example.com/                       ← bind mount → .../sites/example.com/public/
│   │   (appears as: root:site-<id>-rw 0770 inside mount)
│   │   created by: apply_grant(RW)
│   │   removed by: revoke_grant
│   │
│   └── myapp.org/                         ← bind mount → .../sites/myapp.org/public/
│       created by: apply_grant(RO)
│       removed by: revoke_grant
│
├── .ssh/                                  ← au-<user>:au-<user>  0700
│   └── authorized_keys                    ← au-<user>:au-<user>  0600
│       created by: Phase 4 (SSH key management)
│       removed by: Phase 4
│
├── logs/                                  ← NOT created in Phase 3
│   (future: per-user SFTP logs)
│
├── tmp/                                   ← NOT created in Phase 3
│   (future: writable scratch space)
│
└── uploads/                               ← NOT created in Phase 3
    (future: deployment staging area)
```

### Directory Table

| Path | Owner | Group | Mode | Writable? | Created By | Removed By |
|------|-------|-------|------|-----------|------------|------------|
| `au-<user>/` | root | root | 0755 | No | create_user | remove_user |
| `sites/` | root | root | 0755 | No | create_user | remove_user |
| `sites/<domain>/` | root | root | 0755 | Per grant | apply_grant | revoke_grant |
| `.ssh/` | user | user | 0700 | User only | Phase 4 | Phase 4 |
| `.ssh/authorized_keys` | user | user | 0600 | User only | Phase 4 | Phase 4 |

---

## 3. Complete Filesystem Layout Diagram

```
/srv/containercp/
│
├── sites/                                    ← ContainerCP Sites root
│   └── <domain>/                             ← Site directory
│       ├── public/                           ← Web root (real directory)
│       │   │  owner: root
│       │   │  group: site-<id>-rw
│       │   │  mode:  0770
│       │   │  ACL:   g:site-<id>-ro:r-x     (if any RO grants exist)
│       │   │
│       │   └── ... (WordPress files, etc.)
│       │
│       ├── logs/
│       ├── tmp/
│       ├── config/
│       ├── ssl/
│       ├── backups/
│       ├── .env
│       └── docker-compose.yml
│
├── users/                                    ← Managed SFTP users root
│   └── au-<username>/                        ← Per-user chroot (Phase 2)
│       │  owner: root  mode: 0755
│       │
│       ├── sites/                            ← Bind mount container dir
│       │   │  owner: root  mode: 0755
│       │   │
│       │   ├── example.com/    ───bind──→  /srv/containercp/sites/example.com/public/
│       │   └── myapp.org/      ───bind──→  /srv/containercp/sites/myapp.org/public/
│       │
│       └── .ssh/                             (Phase 4)
│
├── database/                                 ← SQLite
├── proxy/
├── backups/
└── templates/
```

---

## 4. Permission Matrix

### Per-Directory Access

| Directory | Owner | Group | Mode | RW User | RO User | SSHD |
|-----------|-------|-------|------|---------|---------|------|
| `au-<user>/` | root | root | 0755 | r-x | r-x | r-x |
| `sites/` | root | root | 0755 | r-x | r-x | r-x |
| `sites/<domain>/` (bind mount source: `public/`) | root | site-\<id\>-rw | 0770 | rwx | r-x (via ACL) | rwx |
| `.ssh/` (Phase 4) | user | user | 0700 | rwx | — | rwx |

### How RW user gets rwx on public/

```
au-developer ∈ site-1-rw (supplementary group)
public/ group = site-1-rw
public/ mode  = 0770  →  group has rwx
∴ au-developer has rwx via group membership
```

### How RO user gets r-x on public/

```
au-reader ∈ site-1-ro (supplementary group)
public/ group = site-1-rw  (NOT site-1-ro)
public/ mode  = 0770       →  RO user NOT in owning group, gets --- via "other"
ACL: setfacl -m g:site-1-ro:r-x public/
∴ au-reader has r-x via ACL
```

### How user is DENIED write

```
RO user: not in site-<id>-rw group, no write ACL entry
→ mkdir, rm, rename, chmod all fail with EACCES
```

---

## 5. ACL Design Justification

### Comparison

| Criterion | Option A: Groups Only | Option B: POSIX ACL (SELECTED) |
|-----------|----------------------|-------------------------------|
| RO enforcement | Public dir must be world-readable (mode o+rx) — security risk | Scoped to `site-<id>-ro` group only |
| RW enforcement | Group ownership + mode 770 — works | Same, no ACL needed |
| Portability | Works everywhere | Requires `acl` package (standard on Debian 13) |
| Backup/restore | Standard tar | `tar --acls` or `getfacl -R` for ACL backup |
| Deterministic | Yes | Yes — `setfacl` is idempotent |
| Testing complexity | Low (just chmod) | Medium (need FakeCommandRunner support for setfacl) |
| Complexity | Simple but insecure (world-readable) | Slightly more complex but scoped |
| Filesystem compat | ext4, xfs, btrfs — all OK | ext4, xfs, btrfs — all support ACLs (default on Debian) |

### Verdict

Option B (POSIX ACL) is selected because:
- World-readable directories leak Site content to any local user
- ACLs scope access to exactly the RO group members
- `acl` package is standard on Debian 13
- `setfacl`/`getfacl` are stable, well-tested utilities
- Only needed on directories with active RO grants (rare)

---

## 6. Multi-Site Access Example

User `au-developer` with grants:
- Site A (id=1, domain=example.com): **READ_WRITE**
- Site B (id=2, domain=myapp.org): **READ_ONLY**
- Site C (id=3, domain=blog.dev): **READ_WRITE**

### Groups

```
au-developer:
  primary group: au-developer (gid=21000)
  supplementary: containercp-sftp (gid=30000)
  supplementary: site-1-rw (gid=21001)
  supplementary: site-2-ro (gid=21002)
  supplementary: site-3-rw (gid=21003)
```

### Bind Mounts

```
/srv/containercp/users/au-developer/sites/
├── example.com/  → /srv/containercp/sites/example.com/public/  (mount)
├── myapp.org/    → /srv/containercp/sites/myapp.org/public/    (mount)
└── blog.dev/     → /srv/containercp/sites/blog.dev/public/     (mount)
```

### Effective Permissions

| Operation | example.com/ | myapp.org/ | blog.dev/ |
|-----------|-------------|-----------|-----------|
| ls | ✓ | ✓ (ACL r-x) | ✓ |
| get/download | ✓ | ✓ | ✓ |
| put/upload | ✓ | ✗ | ✓ |
| rm/delete | ✓ | ✗ | ✓ |
| mkdir | ✓ | ✗ | ✓ |
| chmod | ✗ (not owner) | ✗ | ✗ |

---

## 7. Bind Mount Lifecycle

### Creation

```
apply_grant(access_user_id, site_id, permission):
  1. resolve username from system_accounts
  2. resolve domain from Site resource
  3. mkdir -p /srv/containercp/users/<username>/sites/<domain>/
  4. mount --bind <source-public/> <user-sites/domain/>
  5. mountpoint -q <user-sites/domain/> → verify
  6. On failure: rmdir mount point, return error
```

### Verification

```
mount_verify(path):
  mountpoint -q <path> → exit 0 = is mountpoint
  If NOT a mountpoint after mount attempt → error
  After umount, verify it is NOT a mountpoint
```

### Recovery (daemon restart)

```
Reconciler startup:
  for each system_accounts where entity_type == "access_user":
    for each access_grants where access_user_id == entity_id:
      if grant is active:
        compute mount_path = users/<username>/sites/<domain>/
        if NOT mountpoint -q <mount_path>:
          remount from desired state
```

### Rollback

```
Failure during apply_grant:
  group created, usermod failed → delete group (if unused), return error
  usermod succeeded, mount failed → remove from group, rmdir, return error
  mount succeeded, ACL failed → umount, remove from group, return error
```

### Removal

```
revoke_grant(access_user_id, site_id):
  1. umount <user-sites/domain/>
  2. rmdir <user-sites/domain/>
  3. rmdir <user-sites/> if empty
  4. remove user from site group
```

### Idempotent Retry

```
apply_grant called twice:
  - Group already exists → skip groupadd (check system_accounts)
  - User already in group → skip usermod (check via `id -G <user>` or `groups <user>`)
  - Mount already exists → skip mount (mountpoint -q)
  - ACL already applied → skip setfacl (getfacl check)
```

### Unmount Failure

```
umount fails (EBUSY — process has open file):
  - Return error, preserve mapping
  - Reconciler can retry later
  - Force-unmount (umount -l lazy) is NOT used — too dangerous
```

---

## 8. SQLite Impact — Detailed

### Why No Schema Migration

`system_accounts` (v3) is a generic mapping table:

```sql
CREATE TABLE system_accounts (
    entity_type TEXT NOT NULL,    -- polymorphic: "access_user", "site_group_rw", "site_group_ro"
    entity_id   INTEGER NOT NULL, -- AccessUser.id or Site.id
    uid         INTEGER,          -- NULL for groups
    gid         INTEGER NOT NULL, -- allocated GID
    username    TEXT NOT NULL,     -- system username or group name
    groupname   TEXT NOT NULL,     -- primary group name or site group name
    state       TEXT NOT NULL DEFAULT 'active',
    PRIMARY KEY (entity_type, entity_id)
);
```

### Example Records

| entity_type | entity_id | uid | gid | username | groupname | state |
|------------|-----------|-----|-----|----------|-----------|-------|
| access_user | 1 | 10000 | 21000 | au-developer | au-developer | active |
| access_user | 2 | 10001 | 21001 | au-reader | au-reader | active |
| site_group_rw | 1 | NULL | 21010 | site-1-rw | site-1-rw | active |
| site_group_ro | 1 | NULL | 21011 | site-1-ro | site-1-ro | active |
| site_group_rw | 2 | NULL | 21012 | site-2-rw | site-2-rw | active |

### Ownership Proof

- A `site_group_*` entry in `system_accounts` proves ContainerCP created the OS group
- Before `groupdel site-1-rw`: verify `system_accounts` has matching entry → safe
- No entry → `unmanaged_group_conflict` → fail closed

### Rollback via State

- Group mapped but `groupadd` failed → state = "provisioning" → reconciler retries
- Group mapped and OS exists → state = "active" → normal
- Group mapped but OS deleted → reconciler detects orphan → recreates

### No Changes to access_grants

The `access_grants` table already contains:
- `access_user_id` → FK to access_users
- `site_id` → FK to sites
- `permission` → TEXT: "read_only", "read_write", "deploy"

No new columns needed. The grant row is the SSOT for which user has which permission on which site.

---

## 9. Failure Matrix

| Operation | Failure Point | Rollback | Final State | Retry |
|-----------|--------------|----------|-------------|-------|
| `groupadd site-X-rw` | Already exists | N/A (check first, idempotent) | Group exists | No-op on retry |
| `groupadd site-X-rw` | Permission denied | N/A | No group created | Fails again (admin issue) |
| `usermod -a -G` | User not found | Delete group (if unused) | No group, no membership | Retry after user created |
| `usermod -a -G` | Group not found | Error returned | Previous state | Retry after group created |
| `mkdir -p sites/X/` | Permission denied | Error returned | Directories up to failure exist | Retry succeeds (mkdir -p) |
| `mount --bind` | Source missing | rmdir mount point | No mount | Retry after source exists |
| `mount --bind` | Already mounted | N/A (check first) | Mount exists | No-op |
| `mountpoint -q` | Not a mount (verify fail) | umount if partial, rmdir | No mount | Retry |
| `chgrp site-X-rw public/` | Permission denied | Error, no rollback | public/ unchanged | Admin must fix |
| `chmod 770 public/` | Permission denied | Error | public/ unchanged | Admin must fix |
| `setfacl -m g:X-ro:r-x` | ACL not supported | Error, grant still applied (RO via group only) | ACL missing | Reconciler retries |
| `umount sites/X/` | EBUSY (open files) | Error, preserve state | Mount still exists | Reconciler retries |
| `umount sites/X/` | Not mounted | N/A (check first) | Already unmounted | No-op |
| `groupdel site-X-rw` | Group not empty | Error, preserve mapping | Group still exists | Check membership |

---

## 10. Acceptance Checklist

| # | Requirement | Test |
|---|-----------|------|
| A1 | `site-<id>-rw` group created idempotently via system_accounts + groupadd | 3a unit |
| A2 | `site-<id>-ro` group created idempotently | 3a unit |
| A3 | GID allocated via SystemAccountAllocator (20000-29999, monotonic, non-reuse) | 3a unit |
| A4 | Unmanaged `site-*` group → conflict error | 3a unit |
| A5 | User added to supplementary group on grant create | 3a unit |
| A6 | User removed from supplementary group on grant revoke | 3a unit |
| B1 | public/ chgrp to `site-<id>-rw` | 3b unit |
| B2 | public/ chmod 0770 | 3b unit |
| B3 | RO users get `setfacl -m g:site-<id>-ro:r-x` | 3b unit |
| B4 | ACL removed on RO grant revocation | 3b unit |
| B5 | RO user cannot write (mkdir/rm/rename fail) | 3b integration |
| C1 | `sites/` directory layout created under chroot | 3c unit |
| C2 | Bind mount created via `mount --bind` | 3c unit |
| C3 | Bind mount verified via `mountpoint -q` | 3c unit |
| C4 | Bind mount removed via `umount` | 3c unit |
| C5 | Mount point directory cleaned via `rmdir` | 3c unit |
| C6 | All mounts cleaned on user removal | 3c unit |
| C7 | Mount recovery: reconciler remounts after restart | 3c integration |
| D1 | `apply_grant` creates group + membership + mount + ACL | 3d integration |
| D2 | `revoke_grant` removes mount + membership + group | 3d integration |
| D3 | `apply_pending_grants` called at end of create_user | 3d integration |
| D4 | `revoke_all_grants` called at start of remove_user | 3d integration |
| D5 | Idempotent: double apply = no-op | 3d integration |
| D6 | Rollback: usermod failure → group removed (if unused) | 3d unit |
| D7 | Rollback: mount failure → user removed from group | 3d unit |
| D8 | Multi-site: 3 grants → 3 mounts, 3 groups, correct ACLs | 3d integration |
| D9 | User without grants: create/remove unaffected | 3d regression |
| D10 | Phase 2 tests unchanged (create/remove/enable/disable) | Full suite |

---

## 11. Open Questions

1. **Should `apply_pending_grants` run synchronously inside `create_user` or be deferred?**
   - Recommendation: synchronous inside create_user. If grants fail, the user still exists but grants are pending. Reconciler handles them.

2. **Should we support grant changes (RO→RW) without full revoke+reapply?**
   - Recommendation: implement `update_grant` as revoke+apply in one transaction. Simpler than incremental group+ACL changes.

3. **Should `system_accounts` delete be ON DELETE CASCADE for site removal?**
   - No. FK RESTRICT on access_grants ensures grants are revoked before site deletion. No cascade needed.

4. **Should `mount --make-shared` or `--make-slave` be used?**
   - `mount --make-private` under the managed root ensures mounts don't propagate outside ContainerCP's tree.

---

## 12. ADR Recommendation

One ADR should be created before implementation:

**ADR-010: POSIX ACLs for Read-Only SFTP Enforcement**

Documents the decision to use ACLs instead of world-readable directories or read-only bind mounts for enforcing read-only SFTP access. Covers security rationale, alternatives considered, and Debian 13 dependency analysis.
