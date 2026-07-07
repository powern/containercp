# Sprint 6 — Developer Access Layer

**Goal:** Allow developers to access site files through an access
user system, abstracted behind an AccessProvider interface.

After this sprint:
- Developers can create access users linked to sites
- Access users are managed through CLI
- The model supports SFTP, FTP, and WebDAV providers
- Access paths are mapped to site directories
- No real FTP/SFTP servers are configured yet

## Resource model

```
User
 └─ ContainerCP User
     └─ Site
         ├─ Domain
         ├─ AccessUser  (new)
         │    ├─ username
         │    ├─ password (hashed, stored)
         │    ├─ provider (sftp/ftp/webdav)
         │    └─ path → /srv/containercp/sites/<domain>/public
         ├─ PHP Version
         ├─ Database
         ├─ Backup
         ├─ SSL
         └─ Mail (placeholder)
```

## Scope

- AccessUser resource (inherits Resource)
- AccessProvider abstraction (interface)
- LocalSftpProvider placeholder (no real SFTP setup)
- Full CLI for access user CRUD
- Per-site access path mapping
- SFTP implementation document for future sprints

## Out of scope

- Real SFTP server configuration
- Real FTP server configuration
- Real WebDAV configuration
- Linux user creation
- sshd_config modification
- Docker container changes
- Password hashing (future)

## Definition of done

1. `access user create <user> <domain>` creates an AccessUser record
2. `access user list`, `show`, `disable`, `enable`, `remove` all work
3. AccessUser is persisted via Storage
4. AccessProvider abstract interface in place
5. LocalSftpProvider compiles and returns placeholder results
6. All existing commands continue working
