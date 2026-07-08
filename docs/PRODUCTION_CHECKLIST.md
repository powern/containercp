# ContainerCP Production Validation Checklist

> This is the official release validation checklist for ContainerCP.
>
> Every Release Candidate must pass all applicable items before
> shipping. The checklist covers the complete hosting lifecycle:
> system, networking, proxy, SSL, storage, GUI, logging, and recovery.
>
> Status legend:
> - [ ] NOT TESTED — not yet verified
> - [x] PASS — verified and working
> - [!] FAIL — known issue, must be fixed before release
> - [-] NOT APPLICABLE — not included in this version

---

## 1. System

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 1.1 | `./scripts/update.sh` completes without error | [ ] | |
| 1.2 | `containercpd` binary exists at `/usr/local/bin/` | [ ] | |
| 1.3 | `containercp` binary exists at `/usr/local/bin/` | [ ] | |
| 1.4 | Daemon starts via systemd: `systemctl start containercpd` | [ ] | |
| 1.5 | Daemon status is active: `systemctl is-active containercpd` | [ ] | |
| 1.6 | PID file created at `/srv/containercp/containercpd.pid` | [ ] | |
| 1.7 | Second daemon instance is prevented (exits with message) | [ ] | |
| 1.8 | Health endpoint returns OK: `curl http://127.0.0.1:8080/api/health` | [ ] | Returns `{"success":true,"data":{"status":"ok"}}` |
| 1.9 | Version endpoint returns version: `curl http://127.0.0.1:8080/api/version` | [ ] | |
| 1.10 | Scheduler starts and logs "Renewal check complete" | [ ] | Check `journalctl -u containercpd` |
| 1.11 | Zero compiler warnings in Debug build | [ ] | |
| 1.12 | Zero compiler warnings in Release build | [ ] | |
| 1.13 | All unit tests pass: `ctest` | [ ] | |

## 2. Networking

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 2.1 | Public domain resolves to server IP: `dig +short example.com` | [ ] | |
| 2.2 | Port 80 is reachable from internet: `curl -I http://example.com/` | [ ] | HTTP 200 |
| 2.3 | Port 443 is reachable from internet: `curl -I https://example.com/` | [ ] | TLS handshake + HTTP 200 |
| 2.4 | REST API is NOT exposed on external port 8081 | [ ] | `curl http://<server>:8081/api/health` returns 403 |
| 2.5 | Web UI is accessible on external port 8081 | [ ] | `curl http://<server>:8081/` returns HTML |
| 2.6 | UNIX socket exists at `/srv/containercp/containercpd.sock` | [ ] | |
| 2.7 | CLI connects via socket: `containercp node list` | [ ] | |

## 3. Proxy

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 3.1 | Central proxy container running: `docker ps --filter name=containercp-proxy` | [ ] | |
| 3.2 | `containercp-public` Docker network exists | [ ] | |
| 3.3 | Proxy container attached to `containercp-public` | [ ] | |
| 3.4 | Proxy publishes port 80 and 443 | [ ] | |
| 3.5 | Default config serves 404 on unknown domains | [ ] | |
| 3.6 | HTTP routing works: `curl -H "Host: example.com" http://127.0.0.1/` | [ ] | HTTP 200 |
| 3.7 | HTTPS routing works (after SSL enabled): `curl -H "Host: example.com" https://127.0.0.1/` | [ ] | TLS + HTTP 200 |
| 3.8 | HTTP→HTTPS redirect works (when enabled): `curl -I http://example.com/` | [ ] | HTTP 301 to HTTPS |
| 3.9 | `/.well-known/acme-challenge/` is served on port 80 | [ ] | |
| 3.10 | Nginx config validates: `docker exec containercp-proxy nginx -t` | [ ] | |
| 3.11 | Transactional config rollback: writing invalid config does NOT break running sites | [ ] | |
| 3.12 | Proxy survives daemon restart (no downtime) | [ ] | |
| 3.13 | Multiple sites with independent proxy configs | [ ] | |
| 3.14 | Site containers do NOT publish host ports | [ ] | `docker ps` shows no `0.0.0.0:PORT->PORT` |

## 4. SSL / HTTPS

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 4.1 | Site created as HTTP_ONLY — no SSL resources created | [ ] | GET /api/ssl shows status "HTTP_ONLY" |
| 4.2 | HTTP-only site serves content on port 80 | [ ] | |
| 4.3 | Issue certificate: POST /api/ssl/<domain>/issue | [ ] | Returns job_id + status "pending" |
| 4.4 | Certificate files created at `/srv/containercp/ssl/<site-id>/versions/<n>/` | [ ] | fullchain.pem, privkey.pem, chain.pem |
| 4.5 | `current` symlink points to latest version | [ ] | `ls -la /srv/containercp/ssl/<site-id>/current` |
| 4.6 | metadata.json contains all fields | [ ] | version, site_id, provider_id, status, domains[], issued_at, expires_at, renew_after, https_enabled, redirect_enabled, auto_renew, challenge_type, last_error, fingerprint_sha256, issuer, subject |
| 4.7 | Enable HTTPS: POST /api/ssl/<domain>/enable | [ ] | |
| 4.8 | HTTPS works after enable: `curl -k https://example.com/` | [ ] | HTTP 200 |
| 4.9 | Enable redirect: POST /api/ssl/<domain>/redirect/enable | [ ] | |
| 4.10 | HTTP redirects to HTTPS after enable | [ ] | `curl -I http://example.com/` → 301 |
| 4.11 | Disable redirect: POST /api/ssl/<domain>/redirect/disable | [ ] | Both HTTP and HTTPS serve content |
| 4.12 | Disable HTTPS: POST /api/ssl/<domain>/disable | [ ] | |
| 4.13 | HTTP fallback works after disable (cert files remain) | [ ] | `curl http://example.com/` → HTTP 200 |
| 4.14 | Certificate files still on disk after disable | [ ] | |
| 4.15 | Re-enable HTTPS after disable works instantly | [ ] | No new ACME request needed |
| 4.16 | Renew certificate: POST /api/ssl/<domain>/renew | [ ] | Returns job_id |
| 4.17 | Renewal creates new version directory | [ ] | versions/<n+1>/ created, symlink updated |
| 4.18 | Old versions cleaned up (keep current + 1 backup) | [ ] | |
| 4.19 | Enable without valid certificate returns 409 | [ ] | |
| 4.20 | Redirect enable without HTTPS returns 409 | [ ] | |
| 4.21 | Issue for invalid domain (localhost, .local, .test) returns error | [ ] | |
| 4.22 | CLI: `containercp ssl list` shows all sites | [ ] | |
| 4.23 | CLI: `containercp ssl show <domain>` shows metadata | [ ] | |
| 4.24 | CLI: `containercp ssl issue <domain>` queues job | [ ] | |
| 4.25 | Private key permissions: 0600 | [ ] | `stat -c %a /srv/containercp/ssl/<site-id>/current/privkey.pem` |
| 4.26 | Private key path/content never exposed via API | [ ] | |
| 4.27 | Certificate files permissions: 0644 | [ ] | |

## 5. Storage

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 5.1 | metadata.json is valid JSON | [ ] | |
| 5.2 | metadata.json version field is 1 | [ ] | |
| 5.3 | Atomic writes: no `.tmp` files remain after operation | [ ] | |
| 5.4 | Atomic symlink swap: interrupted write leaves old version intact | [ ] | |
| 5.5 | Directory permissions: 0700 | [ ] | `stat -c %a /srv/containercp/ssl/<site-id>/` |
| 5.6 | Version directory permissions: 0700 | [ ] | |
| 5.7 | Unsupported metadata version returns UNSUPPORTED_VERSION error | [ ] | |
| 5.8 | Corrupted metadata.json returns INVALID_JSON error | [ ] | |
| 5.9 | Missing metadata returns NOT_FOUND, not crash | [ ] | |
| 5.10 | site_id mismatch in metadata returns INVALID_SCHEMA | [ ] | |
| 5.11 | `enumerate()` returns only numeric directories (ignores .staging-) | [ ] | |
| 5.12 | SSL storage on disk matches database records after restart | [ ] | |

## 6. GUI (Web UI)

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 6.1 | SSL page loads all sites (including HTTP_ONLY) | [ ] | |
| 6.2 | Status badge colors: HTTP_ONLY (info), ACTIVE (green), ERROR (red), DISABLED (gray), ISSUING (yellow) | [ ] | |
| 6.3 | HTTPS column shows ON/OFF correctly | [ ] | |
| 6.4 | Expiration date displayed (or — for HTTP_ONLY) | [ ] | |
| 6.5 | Auto Renew shows Yes/No | [ ] | |
| 6.6 | [Issue Certificate] button visible for HTTP_ONLY and ERROR | [ ] | |
| 6.7 | [Issue] triggers POST /api/ssl/<domain>/issue | [ ] | |
| 6.8 | [Enable HTTPS] button visible for ACTIVE + HTTPS OFF | [ ] | |
| 6.9 | [Disable HTTPS] button visible for ACTIVE + HTTPS ON | [ ] | |
| 6.10 | [Renew] button visible for ACTIVE | [ ] | |
| 6.11 | [Redirect] button visible for ACTIVE + HTTPS ON | [ ] | |
| 6.12 | Toast notification on success | [ ] | |
| 6.13 | Toast notification on error with message | [ ] | |
| 6.14 | SSL status updates after action (page refreshes) | [ ] | |
| 6.15 | Site detail SSL card shows correct count and state | [ ] | |
| 6.16 | Site detail SSL card navigates to SSL page on click | [ ] | |
| 6.17 | Dashboard SSL card shows metrics | [ ] | |

## 7. Logging

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 7.1 | Daemon logs to journald: `journalctl -u containercpd -f` | [ ] | |
| 7.2 | Category tags visible: [SYSTEM], [DOCKER], [PROXY], [ACME], [HTTP-01], [LetsEncrypt], [Renewal] | [ ] | |
| 7.3 | SSL operation logs appear: "CertificateStore", "LetsEncrypt" | [ ] | |
| 7.4 | Renewal scheduler logs: "Renewal check complete: X checked, Y renewed, Z skipped, W failed" | [ ] | |
| 7.5 | Renewal events: scheduled / started / succeeded / failed / skipped | [ ] | |
| 7.6 | No ERROR messages during normal operation | [ ] | |
| 7.7 | Clean shutdown log: "Scheduler stopped" | [ ] | |
| 7.8 | API access not logged (to avoid log noise) | [ ] | |

## 8. Recovery

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 8.1 | Daemon restart: `systemctl restart containercpd` | [ ] | |
| 8.2 | SSL state preserved after daemon restart | [ ] | metadata.json on disk matches API |
| 8.3 | Proxy container NOT removed on daemon shutdown | [ ] | `docker ps` after stop |
| 8.4 | Sites remain reachable after daemon restart | [ ] | |
| 8.5 | Docker restart: `systemctl restart docker` | [ ] | |
| 8.6 | Central proxy auto-recovers after Docker restart | [ ] | `ensure_central_proxy()` on daemon start |
| 8.7 | Full server reboot | [ ] | |
| 8.8 | Daemon auto-starts after reboot (systemd enable) | [ ] | |
| 8.9 | All SSL state intact after reboot | [ ] | |
| 8.10 | Stale PID file cleanup: remove pid, restart works | [ ] | |

## 9. Renewal Scheduler

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 9.1 | Scheduler starts on daemon init | [ ] | Log: "Scheduler started (interval: 24h)" |
| 9.2 | Scheduler scans certificates on check cycle | [ ] | |
| 9.3 | HTTP_ONLY sites are skipped | [ ] | |
| 9.4 | ERROR sites are skipped | [ ] | |
| 9.5 | DISABLED sites are skipped | [ ] | |
| 9.6 | `auto_renew=false` sites are skipped | [ ] | |
| 9.7 | Providers without `supports_auto_renew()` are skipped | [ ] | |
| 9.8 | Due certificates are renewed via JobExecutor | [ ] | |
| 9.9 | Exponential backoff on failure (1h, 2h, 4h, ..., 24h) | [ ] | Check `renew_attempts` in metadata |
| 9.10 | After 7 failures, status → ERROR | [ ] | |
| 9.11 | Scheduler persists `next_attempt` in metadata | [ ] | |
| 9.12 | Scheduler stops on daemon shutdown | [ ] | Log: "Scheduler stopped" |
| 9.13 | Scheduler survives daemon restart (interval resets) | [ ] | |

## 10. Job System

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 10.1 | JobExecutor starts with configured worker count | [ ] | |
| 10.2 | Jobs are created with status "pending" | [ ] | |
| 10.3 | Jobs transition through "running" → "completed" / "failed" | [ ] | |
| 10.4 | Job progress updates are visible via GET /api/jobs | [ ] | |
| 10.5 | Task queue bounded (returns false when full) | [ ] | |
| 10.6 | Graceful shutdown: running jobs finish, pending cancelled | [ ] | |
| 10.7 | Exceptions in worker tasks are caught and reported | [ ] | |

---

## Summary

| Section | Total | Pass | Fail | Not Tested |
|---------|-------|------|------|------------|
| 1. System | 13 | 0 | 0 | 13 |
| 2. Networking | 7 | 0 | 0 | 7 |
| 3. Proxy | 14 | 0 | 0 | 14 |
| 4. SSL / HTTPS | 27 | 0 | 0 | 27 |
| 5. Storage | 12 | 0 | 0 | 12 |
| 6. GUI | 17 | 0 | 0 | 17 |
| 7. Logging | 8 | 0 | 0 | 8 |
| 8. Recovery | 10 | 0 | 0 | 10 |
| 9. Renewal Scheduler | 13 | 0 | 0 | 13 |
| 10. Job System | 7 | 0 | 0 | 7 |
| **Total** | **128** | **0** | **0** | **128** |

---

## How to validate

```bash
# 1. Build and install
./scripts/update.sh

# 2. Start daemon
systemctl start containercpd

# 3. Follow logs
journalctl -u containercpd -f

# 4. Check health
curl http://127.0.0.1:8080/api/health
curl http://127.0.0.1:8080/api/version

# 5. Create a test site
containercp site create admin test-validation.local

# 6. Check SSL state
curl http://127.0.0.1:8080/api/ssl

# 7. For real Let's Encrypt validation:
#    - Point a real domain to the server
#    - Use Let's Encrypt staging first:
#      LETSENCRYPT_STAGING=1 containercpd
#    - Issue certificate via API or CLI

# 8. Run all unit tests
cd /opt/containercp/build-release && ctest

# 9. Check logs for errors
journalctl -u containercpd --no-pager | grep -i error
```
