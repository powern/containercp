# BUG-005: site create CLI command broken

## Severity
Critical

## Description
The help text advertises `containercp site create <owner> <domain>`
but the command returns "Error: unknown command".

## Reproduction steps
1. Run `containercp site create admin test.local` → "Error: unknown command"

## Expected behavior
The command should create a site via the daemon and return success.

## Root cause
The CLI thin client had no handler for the `site create` command.
The daemon had no handler for `site-create`.
The operation was only available through the REST API.

## Implementation plan
Add site-create handler to daemon with validation (username, hostname).
Wire it in the CLI thin client.
Add help text.

## Fix commit
This commit

## Status
Resolved
