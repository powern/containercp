# BUG-003: Version mismatch across CLI and API

## Severity
Major

## Description
Version was hardcoded as "0.1.0" in two separate locations:
`libs/cli/CommandDispatcher.cpp` and `libs/api/ApiServer.cpp`.
These values are now inconsistent with the product version v0.5.0-rc1.

## Reproduction steps
1. Run `containercp --version` → returns "0.1.0"
2. Run `curl /api/version` → returns "0.1.0"

## Expected behavior
Both should return "0.5.0-rc1"

## Root cause
No centralized version constant existed. Each subsystem had its own copy.

## Implementation plan
Create `libs/core/Version.h` with a single `containercp::core::VERSION` constant.
Both CLI and API now reference this constant.

## Fix commit
This commit

## Status
Resolved
