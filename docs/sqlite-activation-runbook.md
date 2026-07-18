# SQLite Activation Runbook

This runbook is for operators migrating ContainerCP runtime storage from legacy TXT files to SQLite after Phase 11 validation.

## Scope

- Source backend: legacy TXT files under `/srv/containercp/database`.
- Target backend: SQLite database at `/srv/containercp/database/containercp.db`.
- Activation gate: `/srv/containercp/database/storage-state.json` plus `storage.backend = sqlite`.
- Rollback path: set `storage.backend = legacy` and restart while original TXT files are still present.

## Prerequisites

- ContainerCP is running normally with `storage.backend = legacy` or no explicit storage backend setting.
- Existing TXT database files are readable.
- `/srv/containercp/archive` exists or can be created by the migration command.
- There is enough disk space for the SQLite database plus the immutable legacy archive.
- No one has manually edited `containercp.db` or `storage-state.json`.

## Migration

Run the explicit migration command:

```bash
containercp storage migrate-to-sqlite \
  --source /srv/containercp/database \
  --database /srv/containercp/database/containercp.db \
  --archive-root /srv/containercp/archive \
  --source-version v0.6.0 \
  --target-version v0.7.0 \
  --confirm
```

The command must complete successfully and print:

- a migration ID
- the published SQLite database path
- the immutable archive path
- all migration stages as `OK`
- the `Next steps:` section

Do not set `storage.backend = sqlite` until the migration command succeeds.

## Activation

After a successful migration, set the backend explicitly:

```ini
storage.backend = sqlite
```

Restart `containercpd`.

Startup must fail closed if any validation check fails. A successful SQLite startup logs these `STORAGE` messages:

```text
[INFO] [STORAGE] SQLite backend selected: /srv/containercp/database/containercp.db
[INFO] [STORAGE] SQLite startup validation passed: /srv/containercp/database/containercp.db
[INFO] [STORAGE] SQLite backend ready: /srv/containercp/database/containercp.db
```

## Post-Activation Validation

Check the daemon is running and serving normally:

```bash
containercp node list
containercp site list
containercp domain list
containercp backup list
```

Confirm these files exist and are regular files, not symlinks:

- `/srv/containercp/database/containercp.db`
- `/srv/containercp/database/storage-state.json`

Confirm `storage-state.json` records `active_backend` as `sqlite`, the expected database path, the migration ID, and `verification_result` as `success`.

## Failure Handling

If startup fails after setting `storage.backend = sqlite`:

- Leave `containercp.db` and `storage-state.json` untouched for diagnosis.
- Read the `STORAGE` startup error; it should identify the failing validation step.
- Fix only the reported cause, then restart again.
- Do not delete the legacy TXT files or the archive while diagnosing.

SQLite startup rejects these unsafe states:

- missing activation state
- activation state with a non-`sqlite` backend
- mismatched activation database path
- missing or corrupt SQLite database
- symlinked or non-regular SQLite database path
- symlinked or non-regular activation state path
- failed integrity check or foreign-key check

## Rollback

Rollback is configuration-only while the legacy TXT files remain in place:

```ini
storage.backend = legacy
```

Restart `containercpd`.

After rollback, verify legacy-backed operation with normal CLI reads such as `containercp site list` and `containercp domain list`.

Keep the immutable archive path printed by migration. It is the recovery reference if the original TXT files are damaged later.

## Do Not

- Do not manually edit `containercp.db`.
- Do not manually edit `storage-state.json` except under an explicit recovery procedure.
- Do not replace either file with a symlink.
- Do not enable SQLite before migration succeeds.
- Do not remove the legacy archive until a release-specific retention policy says it is safe.
