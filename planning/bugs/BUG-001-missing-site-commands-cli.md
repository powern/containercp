# BUG-001: Site start/stop/status commands advertised but not wired in CLI

## Severity
Critical

## Description
The CLI help text advertises `site start`, `site stop`, and `site status`
commands, but the `run()` method in `CommandDispatcher.cpp` does not have
handlers for them. Users who try these commands will get "unknown command".

## Reproduction steps
1. Run `./build/containercp site start example.com`
2. Observe "Error: unknown command"

## Expected behavior
The command should send `site-start|example.com` to the daemon.

## Actual behavior
The command is not recognized.

## Root cause
The `run()` method is missing handler blocks for site start/stop/status.
These commands were implemented in the daemon but never wired in the thin CLI.

## Implementation plan
Add send_command() calls for site-start, site-stop, site-status in the
CLI's CommandDispatcher::run() method.

## Resolution
Fixed by adding daemon handlers for site-start, site-stop, site-status
and wiring them in the thin CLI. Also removed duplicate backup-list handler.

## Status
Resolved
