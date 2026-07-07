# BUG-004: Profile CLI commands missing

## Severity
Major

## Description
The product has a Profile subsystem with profile types, managers, and
storage. However, there were no CLI commands to list or inspect profiles.
Only template commands existed.

## Reproduction steps
1. Run `containercp profile list` → "Error: unknown command"

## Expected behavior
List all profiles with name, type, and default indicator.

## Root cause
DaemonApp had no handlers for profile-list, profile-show, or profile-default.
CLI thin client had no corresponding send_command calls.

## Implementation plan
Add daemon handlers for profile-list, profile-show, profile-default.
Wire them in the CLI thin client.
Add help text entries.

## Fix commit
This commit

## Status
Resolved
