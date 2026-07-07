# Epic Review: Backup and Restore

## Implemented functionality

- **BACKUP-001**: `BackupProvider` abstract interface (create_backup, restore_backup, remove_backup)
- **BACKUP-002**: `TarBackupProvider` — creates tar.gz archives, restores from tar.gz, removes files
- **BACKUP-003/004**: `Backup` resource extended with `file_path` and `compression` fields. Manager updated with `find_by_site()`.
- **BACKUP-005**: CLI commands — `backup create <domain>`, `backup restore <id>`, `backup list`, `backup show <id>`, `backup remove <id>`
- **BACKUP-006**: `GET /api/backups` — returns all backups as JSON
- **BACKUP-007**: SiteRemoveOperation removes backup files and records when a site is removed
- **BACKUP-008**: Tests — BackupManager create/find/list/remove, find_by_site, BackupProvider interface compilation

## Architecture improvements

- BackupProvider follows the same pattern as HostingProvider, CertificateProvider, AccessProvider
- TarBackupProvider uses standard tar command (no external dependencies)
- Backup files stored at `/srv/containercp/backups/` (not mixed with database)
- Storage format updated to include file_path and compression

## Technical debt

1. TarBackupProvider uses blocking std::system() — no progress reporting
2. No backup scheduling (cron/systemd timer)
3. No backup rotation (old backups fill disk)
4. No restore verification (checksum)
5. No encrypted backups
6. No incremental backups (full only)

## Recommendations

- Add backup rotation (keep last N backups)
- Add restore verification
- Implement backup scheduling via systemd timer
- Add progress reporting for large backups
- Consider off-site backup support

## Architecture Roadmap

```
Core               ████████████████████ 100%
REST API           ████████████████░░░░ 80%
Web UI             ████████░░░░░░░░░░░░ 40%
Reverse Proxy      ████████████░░░░░░░ 70%
SSL                ███████████░░░░░░░░ 65%
Access             ███████████░░░░░░░░ 65%
Backup             ██████████████░░░░░ 70%
DNS                ░░░░░░░░░░░░░░░░░░░  0%
Mail               ░░░░░░░░░░░░░░░░░░░  0%
Cluster            ░░░░░░░░░░░░░░░░░░░  0%
```

## Proposed next epics

1. **DNS Management** (priority: high, effort: medium)
   - DNS resource and provider
   - Integration with common DNS APIs
   - Estimated: 8-10 tasks

2. **Web UI CRUD Operations** (priority: high, effort: medium)
   - Create/edit/delete resources from the UI
   - Site create form, user management
   - Estimated: 6-8 tasks

3. **Mail Server Integration** (priority: medium, effort: large)
   - Mail resource implementation
   - Postfix/Dovecot provider
   - Estimated: 10-12 tasks
