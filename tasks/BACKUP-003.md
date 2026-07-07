# BACKUP-003: Backup file management

## Description

Manage backup files on disk.

## Requirements

### Storage directory

Backups are stored at:

/srv/containercp/backups/

Each backup file is named:

<domain>-<timestamp>.tar.gz

### Backup resource update

Add to Backup struct:

- file_path (full path to .tar.gz, e.g., /srv/containercp/backups/example.com-20240101.tar.gz)
- compression (string, default "gzip")
- Keep existing fields

### Storage format update

Update backups.db format to include file_path and compression:
id|site_id|owner_id|filename|type|size|created_at|status|file_path|compression
