# SFTP Provider Implementation Plan

## AccessUser to Linux user mapping

Each `AccessUser` resource will eventually map to a Linux system user
when the SFTP provider is enabled.

### Mapping rules

```
AccessUser.username  →  system user: au-<username>
                      (prefixed with "au-" to avoid conflicts)

AccessGrant.site_id  →  system group: site-<id>
                      (one group per site, controls filesystem access)
```

### Example

An AccessUser "developer" with grants to sites 1 and 3 would produce:

```
system user:  au-developer
primary group: au-developer
supplementary groups: site-1, site-3

home directory: /srv/containercp/users/au-developer (chroot jail)
```

### Directory permissions

```
/srv/containercp/sites/<id>/     root:site-<id>  750
/srv/containercp/sites/<id>/public/  site-<id>:site-<id>  770
```

### Key generation

When the SFTP provider is enabled, each `AccessUser` without a
`password_hash` will be assigned an SSH key pair automatically.

## Chroot strategy

Each SFTP user will be chrooted to their site path using OpenSSH's
`internal-sftp` subsystem with the `ChrootDirectory` directive.

### Directory layout

```
/srv/containercp/sites/<domain>/
├── public/        # web root — SFTP user's writeable space
├── logs/
├── tmp/
├── backups/
├── ssl/
│
[chroot] → /srv/containercp/sites/<domain>/
[writeable] → ./public/
```

The chroot root must be owned by root and not writeable by the SFTP
user. A writeable subdirectory (`public/`) is provided inside.

### sshd_config Match block

```
Match User <access-username>
    ChrootDirectory /srv/containercp/sites/<domain>
    ForceCommand internal-sftp
    X11Forwarding no
    AllowTcpForwarding no
    PermitTTY no
```

## Permissions model

- Site files are owned by a dedicated system user per site
- Access users are mapped via a dedicated group
- Each site gets a unique system UID/GID pair
- The `public/` directory is group-writeable

### Planned layout

```
system user: site-<id>      (owns all site files)
system group: site-<id>     (primary group for site)
access user: au-<username>  (member of site-<id> group)
```

## SSH key authentication

- Each AccessUser stores one or more SSH public keys
- Keys are written to `~/.ssh/authorized_keys` for the system user
- Only SSH key authentication is permitted
- Keys are added/removed via CLI:
  `containercp access user add-key <username> "<key>"`
  `containercp access user remove-key <username> <key-id>`

## Password authentication

- Disabled by default for all SFTP access users
- Future: optional enable with `--password` flag on create
- If enabled, passwords use bcrypt or argon2id
- Rate limiting applied via fail2ban

## No classic FTP by default

- FTP is plaintext and insecure
- Explicit FTPS (FTP over TLS) is optional
- Recommended protocol: SFTP only

## Optional FTP provider

- Pure-FTPd or vsftpd running in a per-site Docker container
- Separate FTP port mapped per site
- Configurable via `containercp access config <domain> --ftp-port 21`
- FTPS mandatory when FTP is enabled
- Control via `containercp access ftp enable <domain>`
  and `containercp access ftp disable <domain>`
