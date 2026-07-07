# BUG-006: Site create rollback incomplete and docker compose detection missing

## Severity
Critical

## Description
When `site create` fails (e.g., Docker unavailable), the rollback:
1. Does not remove the filesystem directory
2. Does not remove proxy records
The site record and related resources remain in memory.

Also, DockerRuntime only tries `docker compose` without falling back
to `docker-compose`, causing failures on systems where only the
standalone binary is available.

## Root cause
SiteCreateOperation did not have access to Filesystem or Config,
so it could not remove the site directory on rollback.
DockerRuntime had no fallback for `docker-compose` binary.

## Fix
1. Added filesystem::Filesystem& and config::Config& to SiteCreateOperation
2. Rollback now removes site directory, proxy records, and all in-memory records
3. DockerRuntime now detects: try `docker compose` → if fail, try `docker-compose`
4. Added `#include <sys/wait.h>` and `WEXITSTATUS` for proper exit code reporting
5. Added dry-run support via site-create-dry-run daemon handler

## Fix commit
This commit

## Status
Resolved
