# Database-Aware Backups Operator Guide

DB-5 makes Site backups database-aware through the existing Backups subsystem only.

## Scope

Supported in DB-5:

- MariaDB only.
- One enabled managed application database per Site.
- `ownership_state=managed` with stored managed credentials.
- database runtime `Running`.
- ContainerCP service-account credentials available and verifiable.
- Existing local tar archive provider.

Not supported in DB-5:

- Imported or ownership-uncertain databases.
- Multiple managed databases per Site.
- PostgreSQL, Redis, SQLite, or physical MariaDB backups.
- Backup controls on the Databases page.
- Retention policies or scheduled backups.

## Backup Flow

1. Open `Backups`.
2. Choose a Site and create a backup.
3. ContainerCP queues a `backup-create` job.
4. The job validates the Site and managed database relation.
5. `DatabaseDumpService` exports the logical SQL dump into backup-owned staging.
6. `BackupService` stages Site files and writes a manifest.
7. `TarBackupProvider` creates an owner-only archive.
8. The backup record is persisted only after archive validation.

Archive layout:

```text
backup-root/manifest.json
backup-root/site/...
backup-root/database/managed.sql
backup-root/database/metadata.json
```

## Restore Modes

- `full`: restore Site files, then import the managed database dump.
- `files_only`: restore Site files only.
- `database_only`: import the managed database dump only.

Full restore requires typing the exact target Site domain. Database-only restore accepts the exact target Site domain or database name. Generic confirmations such as `yes`, `true`, `restore`, or `confirm` are rejected.

Before destructive restore steps, ContainerCP creates a pre-restore recovery backup. If restore fails and automatic recovery cannot fully compensate, the job reports `manual_recovery_required`.

## Security Notes

The Backups API returns safe metadata only. It does not expose filesystem paths, staging paths, database passwords, `.env` contents, SQL contents, command output, or temporary option-file paths.

The backup archive includes Site files. If the Site directory contains `.env`, that file is part of the archive and must be protected as secret material. Archive files are created with owner-only permissions by the local provider.
