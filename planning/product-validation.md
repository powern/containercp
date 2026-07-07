# ContainerCP Product Validation

This document is the permanent acceptance checklist for ContainerCP
product releases. Every Release Candidate must pass all applicable
items before the version can be shipped.

## Status legend

- [ ] NOT TESTED — not yet verified in this release cycle
- [x] PASS — verified and working
- [!] FAIL — known issue, must be fixed before release
- [-] NOT APPLICABLE — feature not included in this version

---

## Installation

Validation: ContainerCP can be built and installed on a clean Debian 12 system.

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 1 | Clean Debian 12 installation | [ ] | |
| 2 | Required packages installed (git, cmake, ninja, g++, docker) | [ ] | |
| 3 | `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release` succeeds | [ ] | |
| 4 | `cmake --build build-release` completes with zero warnings | [ ] | |
| 5 | `containercpd` binary exists | [ ] | |
| 6 | `containercp` binary exists | [ ] | |
| 7 | Daemon starts without error | [ ] | |
| 8 | Daemon logs confirm listening on API port | [ ] | |
| 9 | Daemon logs confirm listening on UNIX socket | [ ] | |
| 10 | UNIX socket file created at `/srv/containercp/containercpd.sock` | [ ] | |

## Configuration

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 11 | `/srv/containercp/database/` exists with all .db files | [ ] | |
| 12 | Default admin user seeded | [ ] | |
| 13 | Default node "local" seeded | [ ] | |
| 14 | PHP versions 8.2, 8.3, 8.4 seeded | [ ] | |
| 15 | Template profiles seeded | [ ] | |
| 16 | Web UI files accessible at `/opt/containercp/web/` | [ ] | |

## REST API

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 17 | `GET /api/version` returns valid JSON | [ ] | |
| 18 | `GET /api/health` returns `{"status":"ok"}` | [ ] | |
| 19 | `GET /api/sites` returns empty array | [ ] | |
| 20 | `GET /api/users` returns admin user | [ ] | |
| 21 | `GET /api/nodes` returns local node | [ ] | |
| 22 | `GET /api/php` returns 3 versions | [ ] | |
| 23 | All GET endpoints return `{"success":true,...}` envelope | [ ] | |
| 24 | Unknown endpoint returns 404 | [ ] | |

## Web UI

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 25 | `http://127.0.0.1:8080/` loads without error | [ ] | |
| 26 | Dashboard shows 8 resource cards | [ ] | |
| 27 | Sidebar shows all navigation links | [ ] | |
| 28 | Status indicator shows "Connected" | [ ] | |
| 29 | Theme toggle switches dark/light | [ ] | |
| 30 | All resource pages load without JS errors | [ ] | |
| 31 | Search filter works on tables | [ ] | |

## CLI

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 32 | `containercp --help` lists all commands | [ ] | |
| 33 | `containercp --version` shows version | [ ] | |
| 34 | `containercp node list` shows "local" | [ ] | |
| 35 | `containercp user list` shows "admin" | [ ] | |
| 36 | CLI connects to daemon via UNIX socket | [ ] | |
| 37 | CLI shows friendly error when daemon is not running | [ ] | |

## Site Management

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 38 | `containercp site create admin demo.local` succeeds | [ ] | |
| 39 | Site appears in `site list` | [ ] | |
| 40 | Domain appears in `domain list` | [ ] | |
| 41 | Database appears in `database list` | [ ] | |
| 42 | Second `site create` with same domain fails with "already exists" | [ ] | |
| 43 | Site directory created at `/srv/containercp/sites/demo.local/` | [ ] | |
| 44 | Reverse proxy record created in `proxy list` | [ ] | |
| 45 | Multiple sites can coexist | [ ] | |
| 46 | Site creation validates domain format | [ ] | |
| 47 | Site creation validates owner format | [ ] | |

## Docker Compose

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 48 | `docker-compose.yml` generated in site directory | [ ] | |
| 49 | `.env` file contains database credentials | [ ] | |
| 50 | nginx container starts | [ ] | |
| 51 | php container starts | [ ] | |
| 52 | mariadb container starts | [ ] | |
| 53 | redis container starts | [ ] | |
| 54 | All containers reach "healthy" status | [ ] | |
| 55 | `containercp site stop demo.local` stops containers | [ ] | |
| 56 | `containercp site start demo.local` starts containers | [ ] | |
| 57 | `containercp site status demo.local` shows container state | [ ] | |

## Web Server

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 58 | Nginx config generated at `config/nginx/default.conf` | [ ] | |
| 59 | Proxy config generated at `/srv/containercp/proxy/sites/demo.local.conf` | [ ] | |
| 60 | PHP upstream points to `php:9000` | [ ] | |
| 61 | Web root points to `/var/www/html` | [ ] | |
| 62 | HTTP request returns valid response | [ ] | |

## SSL

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 63 | `containercp ssl request demo.local` creates certificate | [ ] | |
| 64 | Certificate appears in `ssl list` | [ ] | |
| 65 | Certificate status is "active" | [ ] | |
| 66 | `ssl show demo.local` shows certificate details | [ ] | |
| 67 | `ssl renew demo.local` updates certificate | [ ] | |
| 68 | `ssl revoke demo.local` removes certificate | [ ] | |

## Access

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 69 | `containercp access user create developer demo.local` succeeds | [ ] | |
| 70 | User appears in `access user list` | [ ] | |
| 71 | Grant record created for the site | [ ] | |
| 72 | `access user disable developer` marks user disabled | [ ] | |
| 73 | `access user enable developer` marks user enabled | [ ] | |
| 74 | `access user remove developer` removes user and grants | [ ] | |

## Database

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 75 | Database record has unique credentials | [ ] | |
| 76 | `.env` DB_NAME matches database record | [ ] | |
| 77 | `.env` DB_USER matches database record | [ ] | |
| 78 | `.env` DB_PASSWORD matches database record | [ ] | |

## Backup and Restore

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 79 | `containercp backup create demo.local` creates backup file | [ ] | |
| 80 | Backup file exists at `/srv/containercp/backups/` | [ ] | |
| 81 | Backup record appears in `backup list` | [ ] | |
| 82 | `backup show <id>` shows correct metadata | [ ] | |
| 83 | `backup restore <id>` restores files | [ ] | |
| 84 | `backup remove <id>` removes file and record | [ ] | |

## Template Profiles

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 85 | `template list` shows 5 web server profiles | [ ] | |
| 86 | `template show nginx-php-default` shows profile details | [ ] | |
| 87 | `template default` shows nginx-php-default | [ ] | |
| 88 | `template path` returns `/etc/containercp/templates/web/` | [ ] | |
| 89 | `template validate nginx-php-default` returns "valid" | [ ] | |
| 90 | `template reload` succeeds without error | [ ] | |

## Site Removal

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 91 | `containercp site remove demo.local --force` succeeds | [ ] | |
| 92 | Site directory is deleted | [ ] | |
| 93 | Site record removed from `site list` | [ ] | |
| 94 | Domain records removed | [ ] | |
| 95 | Database records removed | [ ] | |
| 96 | Reverse proxy record removed | [ ] | |
| 97 | Backup files removed | [ ] | |
| 98 | Docker containers stopped and removed | [ ] | |
| 99 | No orphan resources remain | [ ] | |
| 100 | `site remove non-existent` returns "not found" | [ ] | |

## Web UI Operations

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 101 | Dashboard shows correct site count after creation | [ ] | |
| 102 | Dashboard shows correct site count after removal | [ ] | |
| 103 | Sites page lists all sites | [ ] | |
| 104 | Clicking site domain opens detail page | [ ] | |
| 105 | Site detail shows correct metadata | [ ] | |
| 106 | Backups page shows created backup | [ ] | |
| 107 | Create Site modal validates inputs | [ ] | |
| 108 | Web UI recovers from daemon restart | [ ] | |

## Stability

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 109 | Daemon runs continuously for 24 hours | [ ] | |
| 110 | No memory leaks detected (stable RSS) | [ ] | |
| 111 | No zombie processes | [ ] | |
| 112 | API responds within 500ms for all endpoints | [ ] | |
| 113 | No orphan files in `/srv/containercp/` | [ ] | |
| 114 | No orphan Docker containers | [ ] | |
| 115 | No orphan Docker volumes | [ ] | |
| 116 | Daemon logs contain no ERROR messages during normal operation | [ ] | |
| 117 | Clean shutdown (SIGTERM) | [ ] | |

## Regression

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 118 | All unit tests pass (`ctest`) | [ ] | |
| 119 | Zero compiler warnings (Debug) | [ ] | |
| 120 | Zero compiler warnings (Release) | [ ] | |
| 121 | No new warnings introduced | [ ] | |

---

## Summary

| Section | Total | Pass | Fail | Not Tested |
|---------|-------|------|------|------------|
| Installation | 10 | 0 | 0 | 10 |
| Configuration | 6 | 0 | 0 | 6 |
| REST API | 8 | 0 | 0 | 8 |
| Web UI | 7 | 0 | 0 | 7 |
| CLI | 6 | 0 | 0 | 6 |
| Site Management | 10 | 0 | 0 | 10 |
| Docker Compose | 10 | 0 | 0 | 10 |
| Web Server | 5 | 0 | 0 | 5 |
| SSL | 6 | 0 | 0 | 6 |
| Access | 6 | 0 | 0 | 6 |
| Database | 4 | 0 | 0 | 4 |
| Backup and Restore | 6 | 0 | 0 | 6 |
| Template Profiles | 6 | 0 | 0 | 6 |
| Site Removal | 10 | 0 | 0 | 10 |
| Web UI Operations | 7 | 0 | 0 | 7 |
| Stability | 9 | 0 | 0 | 9 |
| Regression | 4 | 0 | 0 | 4 |
| **Total** | **114** | **0** | **0** | **114** |

---

## How to use this document

1. Before creating a Release Candidate, copy this document to a
   release-specific file (e.g., `validation-v0.5.0-rc1.md`)

2. Run each check and mark the result

3. If a check FAILS, file an issue or create a fix task

4. A Release Candidate must have zero FAIL entries

5. After all checks pass, the release is ready

6. Update this master document if new validation items are needed
