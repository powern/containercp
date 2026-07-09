# RC2 Stability & Production Foundation

Detailed changelog entries for RC2.

---

## 2025-07-08 | `4d51f53` | RC2 complete — validated on real Debian 13

### RC2: All items validated on real Debian 13 server

| Item | Status |
|------|--------|
| systemd daemon | ✅ |
| install.sh | ✅ |
| update.sh | ✅ |
| Apache backend | ✅ |
| Nginx backend | ✅ |
| Multi-site Docker networking | ✅ |
| Central proxy recovery | ✅ |
| Web UI operations | ✅ |
| Real-time deployment progress | ✅ |
| Rollback cleanup on failure | ✅ |
| journald logging (std::endl flush) | ✅ |
| Startup recovery | ✅ |

### Next Epic: SSL/HTTPS Management (ARCH-005)
- Real ACME HTTP-01 implementation
- Automatic issue and renewal
- HTTP → HTTPS redirect
- REST API and full Web UI
- Future-proof provider interface (DNS-01, Cloudflare, Route53, custom)

---

## 2025-07-08 | `4d51f53` | Rollback validation

### Validated: Site creation rollback cleans up all resources on failure
- Tested on clean Debian 13 VM: `containercp site create admin rollback_bad.local`
- Validation rejected with: "Label contains invalid character: _"
- After failed creation, verified no orphan resources remain:
  - No site record in database
  - No Docker containers running
  - No Docker networks left behind
  - No site directory on disk
  - No proxy config files
- Rollback cleanup confirmed working for all resource types

### Files changed
- `CHANGELOG.md` — this entry

### Validation
- Tested on clean Debian 13 Validation VM

---

## 2025-07-08 | `(this commit)` | Fix update script service restart flow

### Fixed: update.sh binary overwrite while running (`scripts/update.sh`)
- Stop `containercpd` service **before** copying updated binaries to
  `/usr/local/bin/` to prevent "Text file busy" error
- Added health check loop after service start (polls `/api/health` up to 10s)
- Added `systemctl status` output on successful update
- Cleaned up redundant `systemctl daemon-reload` ordering

### Fixed: Logged messages not visible in journald (`libs/logger/Logger.cpp`)
- Changed `"\n"` to `std::endl` in all Logger output methods to force flush
  after every line
- Previously under systemd (when stdout is a pipe, not a TTY), the C++ stream
  buffer was never flushed, hiding application logs from `journalctl -u containercpd`
- Now `[INFO] [SYSTEM] Listening on...` and all category-based log messages
  appear immediately in journald

### Files changed
- `scripts/update.sh` — stop before copy, health check, status output
- `libs/logger/Logger.cpp` — `std::endl` instead of `"\n"` for all output
- `CHANGELOG.md` — this entry

### Validation
- Build: zero compiler warnings
- Tests: 69/69 passed, 289/289 assertions

---


---
Back to [CHANGELOG.md](../../CHANGELOG.md)
