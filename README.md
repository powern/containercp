# ContainerCP

**Container-native hosting control panel — v0.5 Release Candidate**

ContainerCP is a modern, open-source hosting control panel built around
containers. Every website runs as an isolated Docker Compose stack with
its own PHP, database, cache, and web server.

Unlike traditional panels (VestaCP, HestiaCP, Plesk) that configure
system packages directly, ContainerCP treats every site as an
independent, reproducible stack. This makes it inherently more secure,
portable, and predictable.

## Features

- **Docker Compose PHP hosting** — nginx, PHP-FPM (8.2/8.3/8.4),
  MariaDB, Redis per site
- **Daemon architecture** — `containercpd` owns all business logic,
  storage, and providers
- **REST API** — JSON API for all resources, enables automation
- **Web UI** — dark-themed admin panel with dashboard, site management,
  resource CRUD, and deployment progress
- **CLI client** — `containercp` thin client communicates with daemon
  over UNIX socket
- **Reverse proxy** — automatic nginx config generation per site
- **SSL certificates** — Let's Encrypt integration architecture
  (placeholder implementation)
- **Backup and restore** — tar+gzip backups via CLI and Web UI
- **Access users** — per-site SFTP user management (placeholder)
- **Template profiles** — nginx/Apache, PHP/WordPress/Laravel config
  templates on disk, editable without recompilation
- **Job tracking** — background operations with progress reporting
- **Validation** — 137-item product validation checklist

## Architecture

```
Browser / CLI / curl
        │
        ▼
  ┌─────────────┐     ┌──────────────────┐
  │  containercp │────▶│  containercpd    │
  │  (thin CLI)  │     │  (daemon)        │
  └─────────────┘     │                  │
                      │  ┌────────────┐  │
  ┌─────────────┐     │  │ Managers   │  │
  │  Browser    │────▶│  │ Storage    │  │
  │  (Web UI)   │     │  │ Providers  │  │
  └─────────────┘     │  │ REST API   │  │
                      │  └────────────┘  │
                      └──────────────────┘
```

## Quick start on Debian 13

```bash
# Install dependencies
apt update
apt install -y git cmake ninja-build g++ curl docker.io docker-compose-v2

# Build
git clone https://github.com/powern/containercp.git
cd containercp
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

# Start daemon
./build-release/containercpd

# Verify
curl http://127.0.0.1:8080/api/health

# Open Web UI (external access)
# http://<server-ip>:8081/

# Create a site (CLI)
./build-release/containercp site create admin example.com

# List sites
./build-release/containercp site list
```

## Build from source

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

Requires: CMake 3.25+, Ninja, g++ (C++20), Linux (Debian 13 (Trixie) recommended).

## Run daemon

```bash
./build-release/containercpd
```

The daemon listens on:
- `http://127.0.0.1:8080` — REST API (localhost only)
- `http://0.0.0.0:8081` — Web UI (external access, with login)
- `/srv/containercp/containercpd.sock` — CLI communication

## Web UI

For external access, open `http://<server-ip>:8081/` in any modern browser.
For local access (no auth), open `http://127.0.0.1:8080/`.

Pages: Dashboard, Sites, Domains, Databases, SSL, Proxy, Access,
Backups, Profiles, Templates, Nodes, Logs, Settings.

## Basic CLI examples

```bash
containercp node list              # List nodes
containercp user list              # List users
containercp site list              # List sites
containercp site create admin d  # Create site
containercp domain list            # List domains
containercp database list          # List databases
containercp backup create d      # Create backup
containercp backup list            # List backups
containercp ssl list               # List SSL certificates
containercp proxy list             # List proxy configs
containercp template list          # List template profiles
```

## File locations

| Path | Purpose |
|------|---------|
| `/opt/containercp/` | Source code |
| `/srv/containercp/` | Site data and persistent storage |
| `/srv/containercp/database/` | Pipe-delimited .db files |
| `/srv/containercp/sites/<domain>/` | Per-site files and configs |
| `/srv/containercp/proxy/sites/` | Reverse proxy configs |
| `/srv/containercp/backups/` | Backup archives (.tar.gz) |
| `/etc/containercp/templates/` | Disk-based config templates |
| `/var/log/containercp/` | Log files |

## Project status

**Version 0.5 Release Candidate** — RC1 validation completed (128/137 pass),
preparing RC2 for 24-hour stability validation.

| Subsystem | Status | Completeness |
|-----------|--------|-------------|
| Core | Stable | 100% |
| REST API | Active | 90% |
| Daemon | Stable | 90% |
| Web UI | Active | 80% |
| Sites | Stable | 90% |
| Docker/Runtime | Stable | 85% |
| Reverse Proxy | Active | 75% |
| Backup | Active | 80% |
| SSL | Active | 70% |
| Access | Active | 70% |
| Profiles/Templates | Stable | 85% |
| Jobs | Active | 70% |
| Tests | Growing | 50% |

> **Warning:** ContainerCP is not yet production-ready. It is undergoing
> First Production Validation. Breaking changes may occur before v1.0.
> Do not use in production environments without thorough testing.

## Roadmap

| Version | Focus | Status |
|---------|-------|--------|
| 0.1–0.4 | Core, Resources, Providers, Daemon, API | Complete |
| 0.5 | Web Administration, CRUD, Background Jobs | Release Candidate |
| 0.6 | DNS Management | Planned |
| 0.7 | Monitoring and Observability | Planned |
| 0.8 | Multi-node and Cluster | Planned |
| 1.0 | Production Ready | Future |

## See also

- [`INSTALL.md`](INSTALL.md) — Detailed installation guide
- [`planning/product-roadmap.md`](planning/product-roadmap.md) — Full roadmap
- [`planning/product-validation.md`](planning/product-validation.md) — Validation checklist
- [`docs/ADR/`](docs/ADR/) — Architecture Decision Records
- [`docs/WEB-UI.md`](docs/WEB-UI.md) — Web UI documentation
- [`docs/TEMPLATES.md`](docs/TEMPLATES.md) — Template system documentation
