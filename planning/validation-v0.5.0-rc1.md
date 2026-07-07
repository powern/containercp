# Validation Run: v0.5.0-rc1

Date: 2025-07-07
Environment: Debian 13 (Trixie) — clean Validation VM

## Status summary

Core lifecycle validation passed on Debian 13.

| Section | Total | Pass | Fail | Not Tested |
|---------|-------|------|------|------------|
| Validation VM | 3 | 3 | 0 | 0 |
| Installation | 11 | 11 | 0 | 0 |
| Configuration | 6 | 6 | 0 | 0 |
| REST API | 8 | 8 | 0 | 0 |
| Web UI | 18 | 18 | 0 | 0 |
| CLI | 6 | 6 | 0 | 0 |
| Site Management | 10 | 10 | 0 | 0 |
| Docker Compose | 10 | 10 | 0 | 0 |
| Web Server | 5 | 5 | 0 | 0 |
| SSL | 6 | 6 | 0 | 0 |
| Access | 6 | 6 | 0 | 0 |
| Database | 4 | 4 | 0 | 0 |
| Backup and Restore | 7 | 7 | 0 | 0 |
| Template Profiles | 6 | 6 | 0 | 0 |
| Site Removal | 10 | 10 | 0 | 0 |
| Web UI Operations | 8 | 8 | 0 | 0 |
| Stability | 9 | 0 | 0 | 9 |
| Regression | 4 | 4 | 0 | 0 |
| **Total** | **137** | **128** | **0** | **9** |

## Verified core lifecycle

The following user-facing operations were validated end-to-end:

- [x] Build from source on clean Debian 13
- [x] Daemon starts with both listeners (API on 127.0.0.1:8080, Web UI on 0.0.0.0:8081)
- [x] Web UI login with generated admin password
- [x] First login forces password change
- [x] Password change persists after daemon restart
- [x] Web UI version matches backend (0.5.0-rc1)
- [x] Site creation from Web UI
- [x] Docker stack starts (nginx, php, mariadb, redis — all healthy)
- [x] HTTP/PHP returns 200
- [x] Backup creation from Web UI
- [x] Backup creation from CLI
- [x] Backup restore from Web UI
- [x] Backup delete from Web UI
- [x] Site removal from Web UI
- [x] Site removal preserves backup archives
- [x] No containers remain after site removal
- [x] Daemon restart preserves admin password and session

## Bugs discovered and fixed during RC1

| Bug | Description | Fix commit |
|-----|-------------|------------|
| BUG-003 | /api/version returns 404 | Fixed in earlier sprint |
| BUG-011 | Login returns 401 with wrong error message | c0dfa12, a89e5e0, 46437ad, 5c6e651, 54f1e91 |
| BUG-011 | JSON field offset off by 1 (username includes quote) | 5c6e651 |
| BUG-011 | Password file never written to disk | 46437ad |
| BUG-011 | Auth database directory not created | 4ee2428 |
| BUG-012 | Created site missing nginx config file | d5963b8 |
| BUG-013 | Backup restore button has no feedback | 76beb4c |
| BUG-013 | Site removal deletes backup archives | 76beb4c |
| — | Backup delete missing API endpoint | 6531e46 |
| — | Backup restore missing API endpoint | 7860e1d |

## Not yet tested (Stability)

9 stability checks require 24-hour runtime and were not executed
during this validation cycle. These include:
- 24-hour continuous runtime
- Memory leak detection (stable RSS)
- No zombie processes
- API response time < 500ms for all endpoints
- No orphan files, containers, or volumes
- No ERROR log messages during normal operation
- Clean shutdown (SIGTERM)

## Next steps

1. Run 24-hour stability test
2. Close RC1 validation
3. Begin Version 0.6 planning (DNS and Mail)
