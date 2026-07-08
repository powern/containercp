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
| 6 | `/srv/containercp/database/` directory created on startup | [ ] | |
| 7 | `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release` succeeds | [ ] | |
| 8 | `cmake --build build-release` completes with zero warnings | [ ] | |
| 9 | `containercpd` binary exists | [ ] | |
| 10 | `containercp` binary exists | [ ] | |
| 11 | Daemon starts without error | [ ] | |
| 12 | Daemon logs confirm listening on API port | [ ] | |
| 13 | Daemon logs confirm listening on UNIX socket | [ ] | |
| 14 | UNIX socket file created at `/srv/containercp/containercpd.sock` | [ ] | |

## Configuration

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 15 | `/srv/containercp/database/` exists with all .db files | [ ] | |
| 16 | Default admin user seeded | [ ] | |
| 17 | Default node "local" seeded | [ ] | |
| 18 | PHP versions 8.2, 8.3, 8.4 seeded | [ ] | |
| 19 | Template profiles seeded | [ ] | |
| 20 | Web UI files accessible at `/opt/containercp/web/` | [ ] | |

## REST API

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 21 | `GET /api/version` returns valid JSON | [ ] | |
| 22 | `GET /api/health` returns `{"status":"ok"}` | [ ] | |
| 23 | `GET /api/sites` returns empty array | [ ] | |
| 24 | `GET /api/users` returns admin user | [ ] | |
| 25 | `GET /api/nodes` returns local node | [ ] | |
| 26 | `GET /api/php` returns 3 versions | [ ] | |
| 27 | All GET endpoints return `{"success":true,...}` envelope | [ ] | |
| 28 | Unknown endpoint returns 404 | [ ] | |

## Web UI

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 29 | `http://127.0.0.1:8080/` loads without error | [ ] | |
| 30 | Dashboard shows 8 resource cards | [ ] | |
| 31 | Sidebar shows all navigation links | [ ] | |
| 32 | Status indicator shows "Connected" | [ ] | |
| 33 | Theme toggle switches dark/light | [ ] | |
| 34 | All resource pages load without JS errors | [ ] | |
| 35 | Search filter works on tables | [ ] | |

| 36 | `http://0.0.0.0:8081/` loads (external Web UI) | [ ] | |
| 37 | Port 8081 rejects `/api/*` with 403 | [ ] | |
| 38 | Port 8081 shows login page when unauthenticated | [ ] | |
| 39 | Port 8081 `/ui-api/health` is public (no auth required) | [ ] | |
| 40 | Port 8081 login with admin/temp-password succeeds | [ ] | |
| 41 | First login redirects to change-password page | [ ] | |
| 42 | Password change works and redirects to dashboard | [ ] | |
| 43 | Logout button ends session and returns to login | [ ] | |
| 44 | Auth users persisted in `/srv/containercp/database/auth_users.db` | [ ] | |
| 45 | Password in `/etc/containercp/ui-password` matches stored hash | [ ] | |
| 46 | After password change, daemon restart does NOT reset password | [ ] | |


## CLI

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 47 | `containercp --help` lists all commands | [ ] | |
| 48 | `containercp --version` shows version | [ ] | |
| 49 | `containercp node list` shows "local" | [ ] | |
| 50 | `containercp user list` shows "admin" | [ ] | |
| 51 | CLI connects to daemon via UNIX socket | [ ] | |
| 52 | CLI shows friendly error when daemon is not running | [ ] | |

## Site Management

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 53 | `containercp site create admin demo.local` succeeds | [ ] | |
| 54 | Site appears in `site list` | [ ] | |
| 55 | Domain appears in `domain list` | [ ] | |
| 56 | Database appears in `database list` | [ ] | |
| 57 | Second `site create` with same domain fails with "already exists" | [ ] | |
| 58 | Site directory created at `/srv/containercp/sites/demo.local/` | [ ] | |
| 59 | Reverse proxy record created in `proxy list` | [ ] | |
| 60 | Multiple sites can coexist | [ ] | |
| 61 | Site creation validates domain format | [ ] | |
| 62 | Site creation validates owner format | [ ] | |

## Docker Compose

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 63 | `docker-compose.yml` generated in site directory | [ ] | |
| 64 | `.env` file contains database credentials | [ ] | |
| 65 | nginx container starts | [ ] | |
| 66 | php container starts | [ ] | |
| 67 | mariadb container starts | [ ] | |
| 68 | redis container starts | [ ] | |
| 69 | All containers reach "healthy" status | [ ] | |
| 70 | `containercp site stop demo.local` stops containers | [ ] | |
| 71 | `containercp site start demo.local` starts containers | [ ] | |
| 72 | `containercp site status demo.local` shows container state | [ ] | |

## Web Server

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 73 | Nginx config generated at `config/nginx/default.conf` | [ ] | |
| 74 | Proxy config generated at `/srv/containercp/proxy/sites/demo.local.conf` | [ ] | |
| 75 | PHP upstream points to `php:9000` | [ ] | |
| 76 | Web root points to `/var/www/html` | [ ] | |
| 77 | HTTP request returns valid response | [ ] | |

## SSL

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 78 | `containercp ssl request demo.local` creates certificate | [ ] | |
| 79 | Certificate appears in `ssl list` | [ ] | |
| 80 | Certificate status is "active" | [ ] | |
| 81 | `ssl show demo.local` shows certificate details | [ ] | |
| 82 | `ssl renew demo.local` updates certificate | [ ] | |
| 83 | `ssl revoke demo.local` removes certificate | [ ] | |

## Access

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 84 | `containercp access user create developer demo.local` succeeds | [ ] | |
| 85 | User appears in `access user list` | [ ] | |
| 86 | Grant record created for the site | [ ] | |
| 87 | `access user disable developer` marks user disabled | [ ] | |
| 88 | `access user enable developer` marks user enabled | [ ] | |
| 89 | `access user remove developer` removes user and grants | [ ] | |

## Database

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 90 | Database record has unique credentials | [ ] | |
| 91 | `.env` DB_NAME matches database record | [ ] | |
| 92 | `.env` DB_USER matches database record | [ ] | |
| 93 | `.env` DB_PASSWORD matches database record | [ ] | |

## Backup and Restore

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 94 | `containercp backup create demo.local` creates backup file | [ ] | |
| 95 | Backup file exists at `/srv/containercp/backups/` | [ ] | |
| 96 | Backup record appears in `backup list` | [ ] | |
| 97 | `backup show <id>` shows correct metadata | [ ] | |
| 98 | `backup restore <id>` restores files | [ ] | |
| 99 | `backup remove <id>` removes file and record | [ ] | |
| 100 | Backups survive site removal (files and records preserved) | [ ] | |

## Template Profiles

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 101 | `template list` shows 5 web server profiles | [ ] | |
| 102 | `template show nginx-php-default` shows profile details | [ ] | |
| 103 | `template default` shows nginx-php-default | [ ] | |
| 104 | `template path` returns `/etc/containercp/templates/web/` | [ ] | |
| 105 | `template validate nginx-php-default` returns "valid" | [ ] | |
| 106 | `template reload` succeeds without error | [ ] | |

## Site Removal

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 107 | `containercp site remove demo.local --force` succeeds | [ ] | |
| 108 | Site directory is deleted | [ ] | |
| 109 | Site record removed from `site list` | [ ] | |
| 110 | Domain records removed | [ ] | |
| 111 | Database records removed | [ ] | |
| 112 | Reverse proxy record removed | [ ] | |
| 113 | Backup files removed | [ ] | |
| 114 | Docker containers stopped and removed | [ ] | |
| 115 | No orphan resources remain | [ ] | |
| 116 | `site remove non-existent` returns "not found" | [ ] | |

## Web UI Operations

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 117 | Dashboard shows correct site count after creation | [ ] | |
| 118 | Dashboard shows correct site count after removal | [ ] | |
| 119 | Sites page lists all sites | [ ] | |
| 120 | Clicking site domain opens detail page | [ ] | |
| 121 | Site detail shows correct metadata | [ ] | |
| 122 | Backups page shows created backup | [ ] | |
| 123 | Create Site modal validates inputs | [ ] | |
| 124 | Web UI recovers from daemon restart | [ ] | |

## Multi-Site Hosting

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 125 | Create first site (multi-one.local) | [ ] | |
| 126 | Create second site (multi-two.local) — succeeds without port conflict | [ ] | |
| 127 | Site containers do NOT publish host ports in `docker ps` | [ ] | |
| 128 | Central reverse proxy container running, publishes host 80/443 | [ ] | |
| 129 | `containercp-public` network exists with proxy + all site web containers | [ ] | |
| 130 | Per-site private network exists (backend services only) | [ ] | |
| 131 | `curl -H "Host: multi-one.local" http://127.0.0.1/` returns site content | [ ] | |
| 132 | `curl -H "Host: multi-two.local" http://127.0.0.1/` returns site content | [ ] | |
| 133 | Both sites survive daemon restart (proxy stays up) | [ ] | |
| 134 | Removing first site does not affect second site | [ ] | |
| 135 | Proxy config and private network cleaned up after site removal | [ ] | |

## Stability

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 136 | Daemon runs continuously for 24 hours | [ ] | |
| 137 | No memory leaks detected (stable RSS) | [ ] | |
| 138 | No zombie processes | [ ] | |
| 139 | API responds within 500ms for all endpoints | [ ] | |
| 140 | No orphan files in `/srv/containercp/` | [ ] | |
| 141 | No orphan Docker containers | [ ] | |
| 142 | No orphan Docker volumes | [ ] | |
| 143 | Daemon logs contain no ERROR messages during normal operation | [ ] | |
| 144 | Clean shutdown (SIGTERM) | [ ] | |

## Regression

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 145 | All unit tests pass (`ctest`) | [ ] | |
| 146 | Zero compiler warnings (Debug) | [ ] | |
| 147 | Zero compiler warnings (Release) | [ ] | |
| 148 | No new warnings introduced | [ ] | |

---

## Summary

| Section | Total | Pass | Fail | Not Tested |
|---------|-------|------|------|------------|
| Validation VM | 3 | 0 | 0 | 3 |
| Installation | 11 | 0 | 0 | 11 |
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
| Backup and Restore | 7 | 0 | 0 | 7 |
| Template Profiles | 6 | 0 | 0 | 6 |
| Site Removal | 10 | 0 | 0 | 10 |
| Web UI Operations | 8 | 0 | 0 | 8 |
| Multi-Site Hosting | 11 | 0 | 0 | 11 |
| Stability | 9 | 0 | 0 | 9 |
| Regression | 4 | 0 | 0 | 4 |
| **Total** | **148** | **0** | **0** | **148** |

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
