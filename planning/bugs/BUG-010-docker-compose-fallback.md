# BUG-010: Docker Compose detection does not fall back to docker-compose

## Severity
Critical

## Description
On Debian 13, the `docker compose` plugin is not available. Only
`docker-compose` (standalone binary) is provided by the distribution.
ContainerCP's `check_compose()` tested `docker compose version` first
and `docker-compose --version` second, but the `run_command()` method
had redundant detection logic that didn't share state with
`check_compose()`. The error message said "Docker Compose plugin is
not installed" even when `docker-compose` was available.

## Root cause
Two separate detection mechanisms existed:
1. `check_compose()` — tested `docker compose version` then
   `docker-compose --version` but didn't cache which variant was found.
2. `run_command()` — had its own `static` detection that could disagree
   with `check_compose()`.

Additionally, the compose commands used `docker compose` subcommand
format (e.g., `docker compose up -d`) which differs from the
`docker-compose` binary format (e.g., `docker-compose up -d`).

## Fix
1. `check_compose()` now caches which variant was detected
   (`use_docker_compose_` member variable)
2. `run_command()` now uses the cached variant instead of re-detecting
3. All compose commands are split: base (`docker compose` or
   `docker-compose`) is determined once, then the subcommand
   (`up -d`, `stop`, `down`, `ps`) is appended
4. Error message changed from "Docker Compose plugin is not installed"
   to "Docker Compose is not installed" (more accurate)
5. INSTALL.md updated with correct Debian 13 and Debian 12 instructions

## Fix commit
This commit

## Status
Resolved
