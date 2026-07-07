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
| 37 | Port 8081 shows login page when unauthenticated | [ ] | |
| 38 | Port 8081 `/ui-api/health` is public (no auth required) | [ ] | |
| 39 | Port 8081 login with admin/temp-password succeeds | [ ] | |
| 40 | First login redirects to change-password page | [ ] | |
| 41 | Password change works and redirects to dashboard | [ ] | |
| 42 | Logout button ends session and returns to login | [ ] | |
| 43 | Auth users persisted in `/srv/containercp/database/auth_users.db` | [ ] | |
| 44 | Password in `/etc/containercp/ui-password` matches stored hash | [ ] | |


## CLI

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 45 | `containercp --help` lists all commands | [ ] | |
| 46 | `containercp --version` shows version | [ ] | |
| 47 | `containercp node list` shows "local" | [ ] | |
| 48 | `containercp user list` shows "admin" | [ ] | |
| 49 | CLI connects to daemon via UNIX socket | [ ] | |
| 50 | CLI shows friendly error when daemon is not running | [ ] | |

## Site Management

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 51 | `containercp site create admin demo.local` succeeds | [ ] | |
| 52 | Site appears in `site list` | [ ] | |
| 53 | Domain appears in `domain list` | [ ] | |
| 54 | Database appears in `database list` | [ ] | |
| 55 | Second `site create` with same domain fails with "already exists" | [ ] | |
| 56 | Site directory created at `/srv/containercp/sites/demo.local/` | [ ] | |
| 57 | Reverse proxy record created in `proxy list` | [ ] | |
| 58 | Multiple sites can coexist | [ ] | |
| 59 | Site creation validates domain format | [ ] | |
| 60 | Site creation validates owner format | [ ] | |

## Docker Compose

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 61 | `docker-compose.yml` generated in site directory | [ ] | |
| 62 | `.env` file contains database credentials | [ ] | |
| 63 | nginx container starts | [ ] | |
| 64 | php container starts | [ ] | |
| 65 | mariadb container starts | [ ] | |
| 66 | redis container starts | [ ] | |
| 67 | All containers reach "healthy" status | [ ] | |
| 68 | `containercp site stop demo.local` stops containers | [ ] | |
| 69 | `containercp site start demo.local` starts containers | [ ] | |
| 70 | `containercp site status demo.local` shows container state | [ ] | |

## Web Server

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 71 | Nginx config generated at `config/nginx/default.conf` | [ ] | |
| 72 | Proxy config generated at `/srv/containercp/proxy/sites/demo.local.conf` | [ ] | |
| 73 | PHP upstream points to `php:9000` | [ ] | |
| 74 | Web root points to `/var/www/html` | [ ] | |
| 75 | HTTP request returns valid response | [ ] | |

## SSL

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 76 | `containercp ssl request demo.local` creates certificate | [ ] | |
| 77 | Certificate appears in `ssl list` | [ ] | |
| 78 | Certificate status is "active" | [ ] | |
| 79 | `ssl show demo.local` shows certificate details | [ ] | |
| 80 | `ssl renew demo.local` updates certificate | [ ] | |
| 81 | `ssl revoke demo.local` removes certificate | [ ] | |

## Access

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 82 | `containercp access user create developer demo.local` succeeds | [ ] | |
| 83 | User appears in `access user list` | [ ] | |
| 84 | Grant record created for the site | [ ] | |
| 85 | `access user disable developer` marks user disabled | [ ] | |
| 86 | `access user enable developer` marks user enabled | [ ] | |
| 87 | `access user remove developer` removes user and grants | [ ] | |

## Database

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 88 | Database record has unique credentials | [ ] | |
| 89 | `.env` DB_NAME matches database record | [ ] | |
| 90 | `.env` DB_USER matches database record | [ ] | |
| 91 | `.env` DB_PASSWORD matches database record | [ ] | |

## Backup and Restore

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 92 | `containercp backup create demo.local` creates backup file | [ ] | |
| 93 | Backup file exists at `/srv/containercp/backups/` | [ ] | |
| 94 | Backup record appears in `backup list` | [ ] | |
| 95 | `backup show <id>` shows correct metadata | [ ] | |
| 96 | `backup restore <id>` restores files | [ ] | |
| 97 | `backup remove <id>` removes file and record | [ ] | |

## Template Profiles

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 98 | `template list` shows 5 web server profiles | [ ] | |
| 99 | `template show nginx-php-default` shows profile details | [ ] | |
| 100 | `template default` shows nginx-php-default | [ ] | |
| 101 | `template path` returns `/etc/containercp/templates/web/` | [ ] | |
| 102 | `template validate nginx-php-default` returns "valid" | [ ] | |
| 103 | `template reload` succeeds without error | [ ] | |

## Site Removal

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 104 | `containercp site remove demo.local --force` succeeds | [ ] | |
| 105 | Site directory is deleted | [ ] | |
| 106 | Site record removed from `site list` | [ ] | |
| 107 | Domain records removed | [ ] | |
| 108 | Database records removed | [ ] | |
| 109 | Reverse proxy record removed | [ ] | |
| 110 | Backup files removed | [ ] | |
| 111 | Docker containers stopped and removed | [ ] | |
| 112 | No orphan resources remain | [ ] | |
| 113 | `site remove non-existent` returns "not found" | [ ] | |

## Web UI Operations

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 114 | Dashboard shows correct site count after creation | [ ] | |
| 115 | Dashboard shows correct site count after removal | [ ] | |
| 116 | Sites page lists all sites | [ ] | |
| 117 | Clicking site domain opens detail page | [ ] | |
| 118 | Site detail shows correct metadata | [ ] | |
| 119 | Backups page shows created backup | [ ] | |
| 120 | Create Site modal validates inputs | [ ] | |
| 121 | Web UI recovers from daemon restart | [ ] | |

## Stability

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 122 | Daemon runs continuously for 24 hours | [ ] | |
| 123 | No memory leaks detected (stable RSS) | [ ] | |
| 124 | No zombie processes | [ ] | |
| 125 | API responds within 500ms for all endpoints | [ ] | |
| 126 | No orphan files in `/srv/containercp/` | [ ] | |
| 127 | No orphan Docker containers | [ ] | |
| 128 | No orphan Docker volumes | [ ] | |
| 129 | Daemon logs contain no ERROR messages during normal operation | [ ] | |
| 130 | Clean shutdown (SIGTERM) | [ ] | |

## Regression

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 131 | All unit tests pass (`ctest`) | [ ] | |
| 132 | Zero compiler warnings (Debug) | [ ] | |
| 133 | Zero compiler warnings (Release) | [ ] | |
| 134 | No new warnings introduced | [ ] | |

---

## Summary

| Section | Total | Pass | Fail | Not Tested |
|---------|-------|------|------|------------|
| Validation VM | 3 | 0 | 0 | 3 |
| Installation | 10 | 0 | 0 | 10 |
| Configuration | 6 | 0 | 0 | 6 |
| REST API | 8 | 0 | 0 | 8 |
| Web UI | 17 | 0 | 0 | 17 |
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
| **Total** | **134** | **0** | **0** | **134** |

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
