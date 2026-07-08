# Epic: Backup and Restore

Status: Completed

**Goal:** Implement actual file backup and restore for hosted sites.

## Scope

- BackupProvider abstraction
- TarBackupProvider (tar + gzip)
- Backup resource extended with file_path
- CLI backup create/restore/list/show/remove
- REST API endpoint
- Integration with SiteRemoveOperation
- Backup file management

## Affected subsystems

- Backup (libs/backup/) — new provider, extended resource
- Storage (libs/storage/) — updated format
- CLI (libs/cli/) — new commands
- Daemon (libs/daemon/) — new command handlers
- API (libs/api/) — new endpoint
- SiteRemove (libs/operations/) — backup cleanup

## Architecture impact

Adds `BackupProvider` interface following the same pattern as
`HostingProvider`, `CertificateProvider`, and `AccessProvider`.

## Risks

- Tar must be available on the system
- Large sites may take time to backup (not an issue for MVP)
- Backup storage may consume disk space (future: rotation)

## Dependencies

- Filesystem must support read/write to /srv/containercp/backups/
- tar command must be available
