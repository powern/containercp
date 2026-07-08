# Validation Test Environment

This document describes the official validation environment for all
ContainerCP Release Candidates. Every RC must pass real deployment
and testing on this environment before it can be accepted.

## Environment specification

| Property | Value |
|----------|-------|
| Operating system | Debian 13 (Trixie) |
| Installation | Minimal, no desktop |
| Architecture | x86_64 (amd64) |
| Memory | Minimum 2 GB (4 GB recommended) |
| Disk | Minimum 10 GB free |
| Network | Internet access (for package downloads, Docker images) |

## Required packages

```
build-essential
cmake
ninja-build
g++
git
curl
docker.io
docker-compose
```

Installation commands:

```bash
apt update
apt install -y build-essential cmake ninja-build g++ git curl docker.io docker-compose
systemctl enable --now docker
```

## Build and deploy

```bash
git clone https://github.com/powern/containercp.git
cd containercp
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

Expected binaries:
- `build-release/containercpd` — daemon (~1.1 MB)
- `build-release/containercp` — CLI client (~100 KB)

## Daemon startup

```bash
./build-release/containercpd
```

The daemon logs:
- UNIX socket at `/srv/containercp/containercpd.sock`
- REST API on `127.0.0.1:8080`
- Web UI on `0.0.0.0:8081`

## Role of validation

The Validation VM is the **primary quality gate** for the project.
Unit tests and integration tests are necessary but no longer
sufficient. Real deployment and real usage determine whether an
Epic is complete.

## Validation checklist

The full 146-item checklist is in `planning/product-validation.md`.

Validation priority order:

1. **Regression** — builds, tests, warnings (automated)
2. **Configuration** — default users, nodes, PHP versions, profiles
3. **REST API** — all GET and POST endpoints
4. **CLI** — all 40+ commands
5. **Site Management** — create, list, start, stop, status, remove
6. **Web UI** — dashboard, pages, CRUD operations
7. **Docker Compose** — stack creation, health checks
8. **Web Server** — nginx config, proxy config
9. **SSL** — request, renew, revoke
10. **Backup** — create, list, restore, remove
11. **Access** — users, grants
12. **Templates and Profiles** — list, show, validate
13. **Stability** — 24-hour runtime, no leaks
14. **Cleanup** — remove all test data, no orphans

## Development lifecycle update

Every Epic now follows this lifecycle:

```
Architecture Proposal
    ↓
Implementation
    ↓
Unit Tests
    ↓
Integration Tests
    ↓
Git Commit
    ↓
Git Push
    ↓
Deploy to Validation VM
    ↓
Real Product Validation
    ↓
Architecture Review
    ↓
Bug Fixes
    ↓
Repeat until stable
    ↓
Epic Closed
```

No Epic is considered complete until it has successfully passed
validation on the official Validation VM.

## Rules

1. The Validation VM is a clean Debian 13 installation.
2. No development tools beyond the listed packages are pre-installed.
3. No runtime data is pre-created.
4. The daemon is started fresh for each validation run.
5. All 146 checklist items must pass.
6. Discovered bugs are documented in `planning/bugs/`.
7. Fixes are committed before the next RC iteration.
