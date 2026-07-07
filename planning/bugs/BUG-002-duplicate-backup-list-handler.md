# BUG-002: Duplicate backup-list handler in daemon

## Severity
Minor

## Description
DaemonApp.cpp contains two handlers for `backup-list`. The first handler
(now removed) was a simple listing. The second handler (preserved) provides
more detail including backup size. The first handler would catch all requests,
making the second handler dead code.

## Root cause
The handler was duplicated during implementation of the backup subsystem.

## Resolution
Removed the first duplicate handler. The second handler with full detail remains.

## Status
Resolved
