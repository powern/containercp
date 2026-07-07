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
| 45 | After password change, daemon restart does NOT reset password | [ ] | |


## CLI

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 46 | `containercp --help` lists all commands | [ ] | |
| 47 | `containercp --version` shows version | [ ] | |
| 48 | `containercp node list` shows "local" | [ ] | |
| 49 | `containercp user list` shows "admin" | [ ] | |
| 50 | CLI connects to daemon via UNIX socket | [ ] | |
| 51 | CLI shows friendly error when daemon is not running | [ ] | |

## Site Management

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 52 | `containercp site create admin demo.local` succeeds | [ ] | |
| 53 | Site appears in `site list` | [ ] | |
| 54 | Domain appears in `domain list` | [ ] | |
| 55 | Database appears in `database list` | [ ] | |
| 56 | Second `site create` with same domain fails with "already exists" | [ ] | |
| 57 | Site directory created at `/srv/containercp/sites/demo.local/` | [ ] | |
| 58 | Reverse proxy record created in `proxy list` | [ ] | |
| 59 | Multiple sites can coexist | [ ] | |
| 60 | Site creation validates domain format | [ ] | |
| 61 | Site creation validates owner format | [ ] | |

## Docker Compose

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 62 | `docker-compose.yml` generated in site directory | [ ] | |
| 63 | `.env` file contains database credentials | [ ] | |
| 64 | nginx container starts | [ ] | |
| 65 | php container starts | [ ] | |
| 66 | mariadb container starts | [ ] | |
| 67 | redis container starts | [ ] | |
| 68 | All containers reach "healthy" status | [ ] | |
| 69 | `containercp site stop demo.local` stops containers | [ ] | |
| 70 | `containercp site start demo.local` starts containers | [ ] | |
| 71 | `containercp site status demo.local` shows container state | [ ] | |

## Web Server

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 72 | Nginx config generated at `config/nginx/default.conf` | [ ] | |
| 73 | Proxy config generated at `/srv/containercp/proxy/sites/demo.local.conf` | [ ] | |
| 74 | PHP upstream points to `php:9000` | [ ] | |
| 75 | Web root points to `/var/www/html` | [ ] | |
| 76 | HTTP request returns valid response | [ ] | |

## SSL

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 77 | `containercp ssl request demo.local` creates certificate | [ ] | |
| 78 | Certificate appears in `ssl list` | [ ] | |
| 79 | Certificate status is "active" | [ ] | |
| 80 | `ssl show demo.local` shows certificate details | [ ] | |
| 81 | `ssl renew demo.local` updates certificate | [ ] | |
| 82 | `ssl revoke demo.local` removes certificate | [ ] | |

## Access

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 83 | `containercp access user create developer demo.local` succeeds | [ ] | |
| 84 | User appears in `access user list` | [ ] | |
| 85 | Grant record created for the site | [ ] | |
| 86 | `access user disable developer` marks user disabled | [ ] | |
| 87 | `access user enable developer` marks user enabled | [ ] | |
| 88 | `access user remove developer` removes user and grants | [ ] | |

## Database

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 89 | Database record has unique credentials | [ ] | |
| 90 | `.env` DB_NAME matches database record | [ ] | |
| 91 | `.env` DB_USER matches database record | [ ] | |
| 92 | `.env` DB_PASSWORD matches database record | [ ] | |

## Backup and Restore

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 93 | `containercp backup create demo.local` creates backup file | [ ] | |
| 94 | Backup file exists at `/srv/containercp/backups/` | [ ] | |
| 95 | Backup record appears in `backup list` | [ ] | |
| 96 | `backup show <id>` shows correct metadata | [ ] | |
| 97 | `backup restore <id>` restores files | [ ] | |
| 98 | `backup remove <id>` removes file and record | [ ] | |

## Template Profiles

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 99 | `template list` shows 5 web server profiles | [ ] | |
| 100 | `template show nginx-php-default` shows profile details | [ ] | |
| 101 | `template default` shows nginx-php-default | [ ] | |
| 102 | `template path` returns `/etc/containercp/templates/web/` | [ ] | |
| 103 | `template validate nginx-php-default` returns "valid" | [ ] | |
| 104 | `template reload` succeeds without error | [ ] | |

## Site Removal

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 105 | `containercp site remove demo.local --force` succeeds | [ ] | |
| 106 | Site directory is deleted | [ ] | |
| 107 | Site record removed from `site list` | [ ] | |
| 108 | Domain records removed | [ ] | |
| 109 | Database records removed | [ ] | |
| 110 | Reverse proxy record removed | [ ] | |
| 111 | Backup files removed | [ ] | |
| 112 | Docker containers stopped and removed | [ ] | |
| 113 | No orphan resources remain | [ ] | |
| 114 | `site remove non-existent` returns "not found" | [ ] | |

## Web UI Operations

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 115 | Dashboard shows correct site count after creation | [ ] | |
| 116 | Dashboard shows correct site count after removal | [ ] | |
| 117 | Sites page lists all sites | [ ] | |
| 118 | Clicking site domain opens detail page | [ ] | |
| 119 | Site detail shows correct metadata | [ ] | |
| 120 | Backups page shows created backup | [ ] | |
| 121 | Create Site modal validates inputs | [ ] | |
| 122 | Web UI recovers from daemon restart | [ ] | |

## Stability

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 123 | Daemon runs continuously for 24 hours | [ ] | |
| 124 | No memory leaks detected (stable RSS) | [ ] | |
| 125 | No zombie processes | [ ] | |
| 126 | API responds within 500ms for all endpoints | [ ] | |
| 127 | No orphan files in `/srv/containercp/` | [ ] | |
| 128 | No orphan Docker containers | [ ] | |
| 129 | No orphan Docker volumes | [ ] | |
| 130 | Daemon logs contain no ERROR messages during normal operation | [ ] | |
| 131 | Clean shutdown (SIGTERM) | [ ] | |

## Regression

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 132 | All unit tests pass (`ctest`) | [ ] | |
| 133 | Zero compiler warnings (Debug) | [ ] | |
| 134 | Zero compiler warnings (Release) | [ ] | |
| 135 | No new warnings introduced | [ ] | |

---

## Summary

| Section | Total | Pass | Fail | Not Tested |
|---------|-------|------|------|------------|
| Validation VM | 3 | 0 | 0 | 3 |
| Installation | 10 | 0 | 0 | 10 |
| Configuration | 6 | 0 | 0 | 6 |
| REST API | 8 | 0 | 0 | 8 |
| Web UI | 18 | 0 | 0 | 18 |
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
| **Total** | **135** | **0** | **0** | **135** |

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
