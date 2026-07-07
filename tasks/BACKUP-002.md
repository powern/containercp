# BACKUP-002: TarBackupProvider

## Description

Implement BackupProvider using tar + gzip.

## Requirements

### Implementation

Use std::system() to run tar commands:

- create: `tar -czf <output_path> -C <site_dir> .`
- restore: `tar -xzf <backup_path> -C <site_dir>`
- remove: delete the backup file
- list: list .tar.gz files in the backup directory

### Location

libs/backup/TarBackupProvider.h
libs/backup/TarBackupProvider.cpp

### Registration

Wired in ServiceRegistry, stored as BackupProvider&

### Error handling

- Check tar exit code
- Return descriptive error messages
- Check that site_dir exists before backup
