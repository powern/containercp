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
| 38 | Port 8081 login with admin/temp-password succeeds | [ ] | |
| 39 | First login redirects to change-password page | [ ] | |
| 40 | Password change works and redirects to dashboard | [ ] | |
| 41 | Logout button ends session and returns to login | [ ] | |
| 42 | Auth users persisted in `/srv/containercp/database/auth_users.db` | [ ] | |


## CLI

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 43 | `containercp --help` lists all commands | [ ] | |
| 44 | `containercp --version` shows version | [ ] | |
| 45 | `containercp node list` shows "local" | [ ] | |
| 46 | `containercp user list` shows "admin" | [ ] | |
| 47 | CLI connects to daemon via UNIX socket | [ ] | |
| 48 | CLI shows friendly error when daemon is not running | [ ] | |

## Site Management

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 49 | `containercp site create admin demo.local` succeeds | [ ] | |
| 50 | Site appears in `site list` | [ ] | |
| 51 | Domain appears in `domain list` | [ ] | |
| 52 | Database appears in `database list` | [ ] | |
| 53 | Second `site create` with same domain fails with "already exists" | [ ] | |
| 54 | Site directory created at `/srv/containercp/sites/demo.local/` | [ ] | |
| 55 | Reverse proxy record created in `proxy list` | [ ] | |
| 56 | Multiple sites can coexist | [ ] | |
| 57 | Site creation validates domain format | [ ] | |
| 58 | Site creation validates owner format | [ ] | |

## Docker Compose

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 59 | `docker-compose.yml` generated in site directory | [ ] | |
| 60 | `.env` file contains database credentials | [ ] | |
| 61 | nginx container starts | [ ] | |
| 62 | php container starts | [ ] | |
| 63 | mariadb container starts | [ ] | |
| 64 | redis container starts | [ ] | |
| 65 | All containers reach "healthy" status | [ ] | |
| 66 | `containercp site stop demo.local` stops containers | [ ] | |
| 67 | `containercp site start demo.local` starts containers | [ ] | |
| 68 | `containercp site status demo.local` shows container state | [ ] | |

## Web Server

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 69 | Nginx config generated at `config/nginx/default.conf` | [ ] | |
| 70 | Proxy config generated at `/srv/containercp/proxy/sites/demo.local.conf` | [ ] | |
| 71 | PHP upstream points to `php:9000` | [ ] | |
| 72 | Web root points to `/var/www/html` | [ ] | |
| 73 | HTTP request returns valid response | [ ] | |

## SSL

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 74 | `containercp ssl request demo.local` creates certificate | [ ] | |
| 75 | Certificate appears in `ssl list` | [ ] | |
| 76 | Certificate status is "active" | [ ] | |
| 77 | `ssl show demo.local` shows certificate details | [ ] | |
| 78 | `ssl renew demo.local` updates certificate | [ ] | |
| 79 | `ssl revoke demo.local` removes certificate | [ ] | |

## Access

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 80 | `containercp access user create developer demo.local` succeeds | [ ] | |
| 81 | User appears in `access user list` | [ ] | |
| 82 | Grant record created for the site | [ ] | |
| 83 | `access user disable developer` marks user disabled | [ ] | |
| 84 | `access user enable developer` marks user enabled | [ ] | |
| 85 | `access user remove developer` removes user and grants | [ ] | |

## Database

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 86 | Database record has unique credentials | [ ] | |
| 87 | `.env` DB_NAME matches database record | [ ] | |
| 88 | `.env` DB_USER matches database record | [ ] | |
| 89 | `.env` DB_PASSWORD matches database record | [ ] | |

## Backup and Restore

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 90 | `containercp backup create demo.local` creates backup file | [ ] | |
| 91 | Backup file exists at `/srv/containercp/backups/` | [ ] | |
| 92 | Backup record appears in `backup list` | [ ] | |
| 93 | `backup show <id>` shows correct metadata | [ ] | |
| 94 | `backup restore <id>` restores files | [ ] | |
| 95 | `backup remove <id>` removes file and record | [ ] | |

## Template Profiles

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 96 | `template list` shows 5 web server profiles | [ ] | |
| 97 | `template show nginx-php-default` shows profile details | [ ] | |
| 98 | `template default` shows nginx-php-default | [ ] | |
| 99 | `template path` returns `/etc/containercp/templates/web/` | [ ] | |
| 100 | `template validate nginx-php-default` returns "valid" | [ ] | |
| 101 | `template reload` succeeds without error | [ ] | |

## Site Removal

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 102 | `containercp site remove demo.local --force` succeeds | [ ] | |
| 103 | Site directory is deleted | [ ] | |
| 104 | Site record removed from `site list` | [ ] | |
| 105 | Domain records removed | [ ] | |
| 106 | Database records removed | [ ] | |
| 107 | Reverse proxy record removed | [ ] | |
| 108 | Backup files removed | [ ] | |
| 109 | Docker containers stopped and removed | [ ] | |
| 110 | No orphan resources remain | [ ] | |
| 111 | `site remove non-existent` returns "not found" | [ ] | |

## Web UI Operations

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 112 | Dashboard shows correct site count after creation | [ ] | |
| 113 | Dashboard shows correct site count after removal | [ ] | |
| 114 | Sites page lists all sites | [ ] | |
| 115 | Clicking site domain opens detail page | [ ] | |
| 116 | Site detail shows correct metadata | [ ] | |
| 117 | Backups page shows created backup | [ ] | |
| 118 | Create Site modal validates inputs | [ ] | |
| 119 | Web UI recovers from daemon restart | [ ] | |

## Stability

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 120 | Daemon runs continuously for 24 hours | [ ] | |
| 121 | No memory leaks detected (stable RSS) | [ ] | |
| 122 | No zombie processes | [ ] | |
| 123 | API responds within 500ms for all endpoints | [ ] | |
| 124 | No orphan files in `/srv/containercp/` | [ ] | |
| 125 | No orphan Docker containers | [ ] | |
| 126 | No orphan Docker volumes | [ ] | |
| 127 | Daemon logs contain no ERROR messages during normal operation | [ ] | |
| 128 | Clean shutdown (SIGTERM) | [ ] | |

## Regression

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 129 | All unit tests pass (`ctest`) | [ ] | |
| 130 | Zero compiler warnings (Debug) | [ ] | |
| 131 | Zero compiler warnings (Release) | [ ] | |
| 132 | No new warnings introduced | [ ] | |

---

## Summary

| Section | Total | Pass | Fail | Not Tested |
|---------|-------|------|------|------------|
| Validation VM | 3 | 0 | 0 | 3 |
| Installation | 10 | 0 | 0 | 10 |
| Configuration | 6 | 0 | 0 | 6 |
| REST API | 8 | 0 | 0 | 8 |
| Web UI | 15 | 0 | 0 | 15 |
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
| **Total** | **132** | **0** | **0** | **132** |

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
