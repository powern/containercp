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

## Validation VM

Every Release Candidate must be validated on a clean Debian 13 (Trixie)
virtual machine as described in `planning/TEST_ENVIRONMENT.md`.

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 1 | Fresh clone from git on clean VM | [ ] | |
| 2 | No pre-existing `/srv/containercp/` or runtime data | [ ] | |
| 3 | Docker daemon running and accessible | [ ] | |

---

## Installation

Validation: ContainerCP can be built and installed on a clean Debian 13 (Trixie) system.

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 4 | Clean Debian 13 (Trixie) installation | [ ] | |
| 5 | Required packages installed (git, cmake, ninja, g++, docker) | [ ] | |
| 6 | `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release` succeeds | [ ] | |
| 7 | `cmake --build build-release` completes with zero warnings | [ ] | |
| 8 | `containercpd` binary exists | [ ] | |
| 9 | `containercp` binary exists | [ ] | |
| 10 | Daemon starts without error | [ ] | |
| 11 | Daemon logs confirm listening on API port | [ ] | |
| 12 | Daemon logs confirm listening on UNIX socket | [ ] | |
| 13 | UNIX socket file created at `/srv/containercp/containercpd.sock` | [ ] | |

## Configuration

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 14 | `/srv/containercp/database/` exists with all .db files | [ ] | |
| 15 | Default admin user seeded | [ ] | |
| 16 | Default node "local" seeded | [ ] | |
| 17 | PHP versions 8.2, 8.3, 8.4 seeded | [ ] | |
| 18 | Template profiles seeded | [ ] | |
| 19 | Web UI files accessible at `/opt/containercp/web/` | [ ] | |

## REST API

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 20 | `GET /api/version` returns valid JSON | [ ] | |
| 21 | `GET /api/health` returns `{"status":"ok"}` | [ ] | |
| 22 | `GET /api/sites` returns empty array | [ ] | |
| 23 | `GET /api/users` returns admin user | [ ] | |
| 24 | `GET /api/nodes` returns local node | [ ] | |
| 25 | `GET /api/php` returns 3 versions | [ ] | |
| 26 | All GET endpoints return `{"success":true,...}` envelope | [ ] | |
| 27 | Unknown endpoint returns 404 | [ ] | |

## Web UI

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 28 | `http://127.0.0.1:8080/` loads without error | [ ] | |
| 29 | Dashboard shows 8 resource cards | [ ] | |
| 30 | Sidebar shows all navigation links | [ ] | |
| 31 | Status indicator shows "Connected" | [ ] | |
| 32 | Theme toggle switches dark/light | [ ] | |
| 33 | All resource pages load without JS errors | [ ] | |
| 34 | Search filter works on tables | [ ] | |

| 35 | `http://0.0.0.0:8081/` loads (external Web UI) | [ ] | |
| 36 | Port 8081 rejects `/api/*` with 403 | [ ] | |


## CLI

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 37 | `containercp --help` lists all commands | [ ] | |
| 38 | `containercp --version` shows version | [ ] | |
| 39 | `containercp node list` shows "local" | [ ] | |
| 40 | `containercp user list` shows "admin" | [ ] | |
| 41 | CLI connects to daemon via UNIX socket | [ ] | |
| 42 | CLI shows friendly error when daemon is not running | [ ] | |

## Site Management

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 43 | `containercp site create admin demo.local` succeeds | [ ] | |
| 44 | Site appears in `site list` | [ ] | |
| 45 | Domain appears in `domain list` | [ ] | |
| 46 | Database appears in `database list` | [ ] | |
| 47 | Second `site create` with same domain fails with "already exists" | [ ] | |
| 48 | Site directory created at `/srv/containercp/sites/demo.local/` | [ ] | |
| 49 | Reverse proxy record created in `proxy list` | [ ] | |
| 50 | Multiple sites can coexist | [ ] | |
| 51 | Site creation validates domain format | [ ] | |
| 52 | Site creation validates owner format | [ ] | |

## Docker Compose

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 53 | `docker-compose.yml` generated in site directory | [ ] | |
| 54 | `.env` file contains database credentials | [ ] | |
| 55 | nginx container starts | [ ] | |
| 56 | php container starts | [ ] | |
| 57 | mariadb container starts | [ ] | |
| 58 | redis container starts | [ ] | |
| 59 | All containers reach "healthy" status | [ ] | |
| 60 | `containercp site stop demo.local` stops containers | [ ] | |
| 61 | `containercp site start demo.local` starts containers | [ ] | |
| 62 | `containercp site status demo.local` shows container state | [ ] | |

## Web Server

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 63 | Nginx config generated at `config/nginx/default.conf` | [ ] | |
| 64 | Proxy config generated at `/srv/containercp/proxy/sites/demo.local.conf` | [ ] | |
| 65 | PHP upstream points to `php:9000` | [ ] | |
| 66 | Web root points to `/var/www/html` | [ ] | |
| 67 | HTTP request returns valid response | [ ] | |

## SSL

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 68 | `containercp ssl request demo.local` creates certificate | [ ] | |
| 69 | Certificate appears in `ssl list` | [ ] | |
| 70 | Certificate status is "active" | [ ] | |
| 71 | `ssl show demo.local` shows certificate details | [ ] | |
| 72 | `ssl renew demo.local` updates certificate | [ ] | |
| 73 | `ssl revoke demo.local` removes certificate | [ ] | |

## Access

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 74 | `containercp access user create developer demo.local` succeeds | [ ] | |
| 75 | User appears in `access user list` | [ ] | |
| 76 | Grant record created for the site | [ ] | |
| 77 | `access user disable developer` marks user disabled | [ ] | |
| 78 | `access user enable developer` marks user enabled | [ ] | |
| 79 | `access user remove developer` removes user and grants | [ ] | |

## Database

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 80 | Database record has unique credentials | [ ] | |
| 81 | `.env` DB_NAME matches database record | [ ] | |
| 82 | `.env` DB_USER matches database record | [ ] | |
| 83 | `.env` DB_PASSWORD matches database record | [ ] | |

## Backup and Restore

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 84 | `containercp backup create demo.local` creates backup file | [ ] | |
| 85 | Backup file exists at `/srv/containercp/backups/` | [ ] | |
| 86 | Backup record appears in `backup list` | [ ] | |
| 87 | `backup show <id>` shows correct metadata | [ ] | |
| 88 | `backup restore <id>` restores files | [ ] | |
| 89 | `backup remove <id>` removes file and record | [ ] | |

## Template Profiles

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 90 | `template list` shows 5 web server profiles | [ ] | |
| 91 | `template show nginx-php-default` shows profile details | [ ] | |
| 92 | `template default` shows nginx-php-default | [ ] | |
| 93 | `template path` returns `/etc/containercp/templates/web/` | [ ] | |
| 94 | `template validate nginx-php-default` returns "valid" | [ ] | |
| 95 | `template reload` succeeds without error | [ ] | |

## Site Removal

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 96 | `containercp site remove demo.local --force` succeeds | [ ] | |
| 97 | Site directory is deleted | [ ] | |
| 98 | Site record removed from `site list` | [ ] | |
| 99 | Domain records removed | [ ] | |
| 100 | Database records removed | [ ] | |
| 101 | Reverse proxy record removed | [ ] | |
| 102 | Backup files removed | [ ] | |
| 103 | Docker containers stopped and removed | [ ] | |
| 104 | No orphan resources remain | [ ] | |
| 105 | `site remove non-existent` returns "not found" | [ ] | |

## Web UI Operations

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 106 | Dashboard shows correct site count after creation | [ ] | |
| 107 | Dashboard shows correct site count after removal | [ ] | |
| 108 | Sites page lists all sites | [ ] | |
| 109 | Clicking site domain opens detail page | [ ] | |
| 110 | Site detail shows correct metadata | [ ] | |
| 111 | Backups page shows created backup | [ ] | |
| 112 | Create Site modal validates inputs | [ ] | |
| 113 | Web UI recovers from daemon restart | [ ] | |

## Stability

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 114 | Daemon runs continuously for 24 hours | [ ] | |
| 115 | No memory leaks detected (stable RSS) | [ ] | |
| 116 | No zombie processes | [ ] | |
| 117 | API responds within 500ms for all endpoints | [ ] | |
| 118 | No orphan files in `/srv/containercp/` | [ ] | |
| 119 | No orphan Docker containers | [ ] | |
| 120 | No orphan Docker volumes | [ ] | |
| 121 | Daemon logs contain no ERROR messages during normal operation | [ ] | |
| 122 | Clean shutdown (SIGTERM) | [ ] | |

## Regression

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 123 | All unit tests pass (`ctest`) | [ ] | |
| 124 | Zero compiler warnings (Debug) | [ ] | |
| 125 | Zero compiler warnings (Release) | [ ] | |
| 126 | No new warnings introduced | [ ] | |

---

## Summary

| Section | Total | Pass | Fail | Not Tested |
|---------|-------|------|------|------------|
| Validation VM | 3 | 0 | 0 | 3 |
| Installation | 10 | 0 | 0 | 10 |
| Configuration | 6 | 0 | 0 | 6 |
| REST API | 8 | 0 | 0 | 8 |
| Web UI | 9 | 0 | 0 | 9 |
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
| Web UI Operations | 8 | 0 | 0 | 8 |
| Stability | 9 | 0 | 0 | 9 |
| Regression | 4 | 0 | 0 | 4 |
| **Total** | **126** | **0** | **0** | **126** |

---

## How to use this document

1. Before creating a Release Candidate, copy this document to a
   release-specific file (e.g., `validation-v0.5.0-rc1.md`)

2. Deploy the RC to a clean Validation VM (see `planning/TEST_ENVIRONMENT.md`)

3. Run each check and mark the result

4. If a check FAILS, file an issue or create a fix task

5. A Release Candidate must have zero FAIL entries

6. After all checks pass on the Validation VM, the release is ready

7. Update this master document if new validation items are needed
