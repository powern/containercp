# BUG-007: site remove --force command broken

## Severity
Critical

## Description
`containercp site remove <domain> --force` returns "unknown command".
Only the help text existed; no handler was wired in CLI or daemon.

## Root cause
CLI thin client had no handler for `site remove`. Daemon had no
`site-remove` or `site-remove-force` handler. The operation was
only available through the REST API.

## Fix
Added daemon handlers for `site-remove` and `site-remove-force`.
Wired in CLI thin client for both `argc == 4` (without --force) and
`argc == 5` with `--force`. Updated help text.

## Fix commit
This commit

## Status
Resolved
