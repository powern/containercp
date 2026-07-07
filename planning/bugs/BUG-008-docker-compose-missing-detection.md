# BUG-008: Docker Compose plugin missing detection

## Severity
Critical

## Description
On Debian 13, `docker` may be installed but `docker compose` plugin
may not be. Site creation fails with exit code 127 after creating the
filesystem and records, leaving orphan data.

## Root cause
No separate check for Docker Compose availability existed. The code
only checked for `docker --version`. Compose was only detected at
execution time inside `run_command()`, after filesystem mutation.

## Fix
1. Added `check_compose()` method to `Runtime` interface and
   `DockerRuntime` implementation with caching
2. `check_compose()` tries `docker compose version` first, falls back
   to `docker-compose --version`
3. `DockerComposeProvider::create_site()` now calls
   `rt_.check_compose()` before any filesystem mutation
4. Updated INSTALL.md with detailed Debian 13 Docker Compose
   installation documentation (three methods)

## Fix commit
This commit

## Status
Resolved
