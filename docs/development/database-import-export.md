# Database Import And Export Operator Guide

DB-4 provides job-backed logical SQL export and import for one managed MariaDB application database per Site.

## Supported Targets

Allowed:

- `ownership_state=managed`
- `runtime_status=Running`
- `connection_status=verified`
- `credential_state=available`
- MariaDB only
- The Site's single managed application database only

Blocked:

- Imported databases
- Ownership-uncertain databases
- Credential-unavailable databases
- Multiple database records for one Site
- PostgreSQL, MySQL, SQLite, Redis, and other engines

## Export

1. Open the Databases page.
2. Select a managed, healthy database.
3. Click `Export SQL`.
4. Watch the job timeline.
5. After completion, click `Download Export`.
6. Revoke the artifact when it is no longer needed.

Exports are uncompressed `.sql` files. They are stored outside web roots under ContainerCP-controlled artifact storage, have owner-only permissions, and expire after 24 hours.

## Download

Downloads use an opaque artifact ID, not a filesystem path. The browser receives a sanitized filename and `Content-Type: application/sql`. Server paths, command lines, credentials, option-file paths, and SQL contents are not returned in metadata responses.

## Import

DB-4 import mode is `execute SQL into the existing managed database`. It is not called restore because MariaDB DDL cannot be rolled back transactionally.

1. Select the target managed database.
2. Click `Import SQL`.
3. Select a supported `.sql` file.
4. Type the exact database name or Site domain.
5. Submit the import.
6. Watch the job timeline.
7. Run DB Verify after completion if desired.

Before import, ContainerCP creates a recovery export. If import fails mid-file, the target may be partially modified and the job reports manual recovery required.

## Format And Size Limits

- Format: uncompressed `.sql`
- Source: ContainerCP-generated DB-4 exports only
- Upload model: bounded raw `application/sql`
- Maximum upload size: 5 MiB
- Compression: not supported in DB-4
- Multipart upload: not supported until a streaming multipart parser exists

## Rejected SQL Policy

DB-4 does not attempt to parse arbitrary SQL. It rejects known escape or account-management constructs, including `CREATE DATABASE`, `DROP DATABASE`, `USE`, `GRANT`, `CREATE USER`, `ALTER USER`, `DEFINER`, and `LOAD DATA LOCAL INFILE`.

## Failure States

- `failed_before_changes`: validation, runtime, credential, or recovery-export failure prevented import execution.
- `failed_target_may_be_partial`: MariaDB started import execution and then failed, or post-import verification failed.
- `manual_recovery_required`: automatic rollback is not claimed; use the recovery export and operator judgment.
- `artifact_unavailable`: artifact is expired, revoked, missing, or failed safety checks.

## Security Notes

No database password is exposed in API responses, Web UI state, job messages, filenames, artifact metadata, HTTP headers, process argv, or logs. Runtime execution uses MariaDB option files and argument vectors, not shell redirection.
