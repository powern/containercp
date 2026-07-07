# BACKUP-007: SiteRemoveOperation integration

## Description

When a site is removed, also remove all backup files and records.

## Requirements

In SiteRemoveOperation::execute():

1. Find all Backup records for the site
2. For each backup, call BackupProvider::remove_backup() to delete the file
3. Remove each Backup record
4. Continue with remaining cleanup
