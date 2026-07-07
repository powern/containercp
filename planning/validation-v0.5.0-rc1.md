# Validation Run: v0.5.0-rc1

Date: 2024-07-07
Environment: Container (Dev) — not clean Debian 12

## Status summary

| Section | Total | Pass | Fail | Not Tested |
|---------|-------|------|------|------------|
| Installation | 10 | 0 | 0 | 10 |
| Configuration | 6 | 4 | 0 | 2 |
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
| Regression | 4 | 4 | 0 | 0 |

## Regression checks (PASS)

All 4 regression checks pass in the current environment:
- Zero compiler warnings (Debug)
- Zero compiler warnings (Release)
- All unit tests pass
- Both binaries build successfully

## Configuration checks (PASS)

4 of 6 configuration checks pass:
- Default admin user is seeded
- Default node "local" is seeded
- PHP versions 8.2, 8.3, 8.4 are seeded
- Template profiles are seeded

## Checks requiring Debian 12

90 checks require a clean Debian 12 installation with Docker:
- Installation (10)
- REST API (8) — requires daemon to run
- Web UI (7) — requires daemon
- CLI (6) — requires daemon
- Site Management (10) — requires Docker
- Docker Compose (10) — requires Docker
- Web Server (5) — requires Docker
- SSL (6) — requires daemon
- Access (6) — requires daemon
- Database (4) — requires daemon
- Backup/Restore (6) — requires Docker
- Templates (6) — requires daemon
- Site Removal (10) — requires Docker
- Web UI Ops (7) — requires daemon
- Stability (9) — requires 24h runtime

## Next steps

1. Deploy on clean Debian 12 VM
2. Start daemon, run all API/CLI/Web UI checks
3. Install Docker, run site creation and Docker checks
4. Document all discovered bugs
5. Fix, repeat until all 114 pass
