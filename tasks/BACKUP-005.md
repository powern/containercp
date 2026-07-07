# BACKUP-005: CLI commands

## Description

Add backup CLI commands to daemon and thin client.

## Commands

```
containercp backup create <domain>
containercp backup restore <id>
containercp backup list
containercp backup show <id>
containercp backup remove <id>
```

### backup create <domain>

1. Verify site exists
2. Generate timestamp
3. Create tar.gz via BackupProvider
4. Record Backup resource
5. Print success with file info

### backup restore <id>

1. Find Backup resource by id
2. Verify backup file exists
3. Restore via BackupProvider
4. Update status

### backup list

List all backups with id, domain, date, size

### backup show <id>

Show detailed backup info

### backup remove <id>

1. Find Backup resource
2. Remove backup file via BackupProvider
3. Remove Backup resource
4. Save
