# BUG-009: Failed site create leaves site record in storage

## Severity
Critical

## Description
When `site create` fails, the rollback in `SiteCreateOperation` cleans
up in-memory records (site, domain, database) and filesystem, but the
daemon handler does not call `s.save()` after rollback. The storage
files still contain the stale records from before the creation attempt.

On the next daemon restart or CLI invocation, the stale records cause
`site list` to show the failed site.

## Root cause
The daemon's `site-create` handler calls `s.save()` only on success.
On failure, the cleaned in-memory state is never persisted to disk.
Same issue exists in the REST API handler.

## Fix
Added `s.save()` call in both the daemon `site-create` handler and the
API `POST /api/sites/create` handler after failed site creation, so the
rolled-back state is persisted.

## Files changed
- libs/daemon/DaemonApp.cpp: added s.save() after failed site-create
- libs/api/ApiServer.cpp: added s.save() after failed POST /api/sites/create

## Regression test
Added SiteManager remove cleans state test to test_managers.cpp.

## Fix commit
This commit

## Status
Resolved
