# ContainerCP

**Container-native hosting control panel — v0.5 Release Candidate 2**

ContainerCP is a modern, open-source hosting control panel built around
containers. Every website runs as an isolated Docker Compose stack with
its own PHP, database, cache, and web server.

## Features

- **Docker Compose PHP hosting** — Apache2 or Nginx, PHP-FPM (8.2/8.3/8.4),
  MariaDB, Redis per site
- **Daemon architecture** — `containercpd` owns all business logic,
  storage, and providers; runs as a systemd service
- **REST API** — JSON API for all resources, enables automation
- **Web UI** — dark-themed admin panel with dashboard, site management,
  resource CRUD, and real-time deployment progress
- **CLI client** — `containercp` thin client communicates with daemon
  over UNIX socket
- **Reverse proxy** — automatic nginx config generation per site via
  Docker networking (no host port allocation)
- **SSL certificates** — Let's Encrypt integration architecture
  (placeholder implementation)
- **Backup and restore** — tar+gzip backups via CLI and Web UI
- **Template profiles** — nginx/Apache, PHP/WordPress/Laravel config
  templates on disk, editable without recompilation
- **Job tracking** — background operations with real-time progress
- **Single instance** — PID file prevents multiple daemon instances
- **Startup recovery** — auto-verifies network, proxy, directories

## Quick Install on Debian 13

```bash
curl -fsSL https://raw.githubusercontent.com/powern/containercp/main/scripts/install.sh | bash
```

This installs everything: dependencies, Docker, builds the project,
installs the systemd service, and starts the daemon.

After install, open `http://<server-ip>:8081/` in your browser.

## Architecture

```
Browser / CLI / curl
        │
        ▼
  ┌─────────────┐     ┌──────────────────┐
  │  containercp │────▶│  containercpd    │
  │  (thin CLI)  │     │  (daemon)        │
  └─────────────┘     │  ┌────────────┐  │
                      │  │ Managers   │  │
  ┌─────────────┐     │  │ Storage    │  │
  │  Browser    │────▶│  │ Providers  │  │
  │  (Web UI)   │     │  │ REST API   │  │
  └─────────────┘     │  │ Jobs       │  │
                      │  └────────────┘  │
                      └──────────────────┘
```

## Project Status

**Version 0.5 Release Candidate 2** — RC2 stability and production
foundation.

| Subsystem | Status | Completeness |
|-----------|--------|-------------|
| Core | Stable | 100% |
| REST API | Active | 90% |
| Daemon | Stable | 95% |
| Web UI | Active | 85% |
| Sites | Stable | 95% |
| Docker/Runtime | Stable | 90% |
| Reverse Proxy | Active | 90% |
| Backup | Active | 85% |
| SSL | Active | 75% |
| Profiles/Templates | Stable | 90% |
| Jobs | Active | 80% |
| Tests | Growing | 60% |

## See Also

- [`INSTALL.md`](INSTALL.md) — Detailed installation guide
- [`scripts/install.sh`](scripts/install.sh) — Automatic installation
- [`scripts/update.sh`](scripts/update.sh) — Automatic update
- [`AGENTS.md`](AGENTS.md) — AI agent rules and navigation
- [`CHANGELOG.md`](CHANGELOG.md) — Release history
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — System architecture
- [`docs/runtime-architecture.md`](docs/runtime-architecture.md) — Runtime subsystem
- [`docs/development/single-source-of-truth.md`](docs/development/single-source-of-truth.md) — SSOT rules
- [`planning/product-roadmap.md`](planning/product-roadmap.md) — Roadmap
- [`planning/product-validation.md`](planning/product-validation.md) — Validation checklist
- [`docs/ADR/`](docs/ADR/) — Architecture Decision Records
