# ContainerCP v0.6.0-rc1 Validation Plan

**Status:** Not started
**Target:** Clean Debian 13 (Trixie) VM
**Version:** `0.6.0-rc1` (see `libs/core/Version.h`)

---

## Build and startup

| # | Item | Result | Notes |
|---|------|--------|-------|
| 1 | Clean Release build (`cmake --build build-release`) | | |
| 2 | Zero compiler warnings (Debug + Release) | | |
| 3 | `containercp --version` reports `0.6.0-rc1` | | |
| 4 | `GET /api/version` reports `0.6.0-rc1` | | |
| 5 | Daemon starts on `127.0.0.1:8080` (API) | | |
| 6 | Daemon starts on `DockerGateway:8081` (Web UI) | | |
| 7 | UNIX socket at `/srv/containercp/containercpd.sock` | | |
| 8 | Deterministic test suite passes (242 tests) | | |
| 9 | Live-DNS integration suite (15 tests, optional) | | |

## Regression — normal site operations

| # | Item | Result | Notes |
|---|------|--------|-------|
| 10 | Create Apache-based site | | |
| 11 | Create Nginx-based site | | |
| 12 | Site appears in Sites list | | |
| 13 | Domain records created | | |
| 14 | Database records created | | |
| 15 | Docker stack starts (compose up) | | |
| 16 | Proxy config generated | | |
| 17 | Site detail page loads | | |
| 18 | SSL issue works (staging) | | |
| 19 | Backup create/restore works | | |
| 20 | Site removal cleans up all resources | | |

## Mail

| # | Item | Result | Notes |
|---|------|--------|-------|
| 21 | Mail module activates successfully | | |
| 22 | MailDomain created for existing domain | | |
| 23 | Mailbox created with password | | |
| 24 | SMTP port 25 accessible | | |
| 25 | IMAP port 143/993 accessible | | |
| 26 | Local delivery works (send → receive) | | |
| 27 | Mail alias routes correctly | | |
| 28 | DKIM key generated via API | | |
| 29 | DKIM DNS record correct format | | |
| 30 | Postfix health ok | | |
| 31 | Dovecot health ok | | |
| 32 | Rspamd health ok | | |
| 33 | External-relay mode (if configured) | | |
| 34 | Split-M365 mode (if configured) | | |
| 35 | PHP Mail works (if site has mail enabled) | | |

## DNS Diagnostics

| # | Item | Result | Notes |
|---|------|--------|-------|
| 36 | Domain list shows DNS status badges | | |
| 37 | Domain detail Overview tab renders | | |
| 38 | DNS Records tab shows Configured vs Published | | |
| 39 | A record comparison works | | |
| 40 | AAAA record comparison works | | |
| 41 | MX record comparison works | | |
| 42 | SPF record comparison works | | |
| 43 | DKIM record comparison works | | |
| 44 | DMARC record comparison works | | |
| 45 | CAA record check works | | |
| 46 | Security tab DMARC Wizard (3 policies) | | |
| 47 | Evidence/Why panel opens and shows details | | |
| 48 | Evidence Dismiss closes panel | | |
| 49 | Only one evidence panel open at a time | | |
| 50 | Check Again refreshes DNS and closes evidence | | |
| 51 | Health Score calculated and displayed | | |
| 52 | Health Score: mail domain present = correct weights | | |
| 53 | Health Score: no mail domain = mail excluded | | |
| 54 | Health Score: DKIM missing = reduced | | |
| 55 | Health Score: all N/A = N/A grade | | |
| 56 | Cache: second request returns cached=true | | |
| 57 | Cache: refresh=1 returns cached=false | | |
| 58 | Loading states show during DNS resolution | | |
| 59 | Error states show message with retry | | |

## Admin panel (site_id=0)

| # | Item | Result | Notes |
|---|------|--------|-------|
| 60 | Admin panel appears in Domains list | | |
| 61 | Type shows "system" | | |
| 62 | site_id=0, site_name="ContainerCP Admin" | | |
| 63 | Runtime shows N/A | | |
| 64 | SSL status correct (Active/Disabled) | | |
| 65 | No Remove button visible | | |
| 66 | No MailDomain ID-0 collision | | |
| 67 | Domain Detail: Mail shows "Not configured" | | |
| 68 | Domain Detail: no unrelated MX/SPF/DKIM errors | | |
| 69 | Proxy management card visible | | |
| 70 | SSL management card visible | | |
| 71 | Admin panel appears in Sites list | | |
| 72 | Web status shows Available/Not verified | | |
| 73 | PHP shows N/A | | |
| 74 | Site Detail: system layout (no runtime/PHP/databases/backups) | | |
| 75 | No `/api/runtime/0` call generated | | |
| 76 | No `/api/sites/0/mail-status` call generated | | |
| 77 | System Site cannot be removed (403) | | |
| 78 | System Domain cannot be removed (403) | | |

## Safety and confirmations

| # | Item | Result | Notes |
|---|------|--------|-------|
| 79 | Proxy Reload shows confirmation dialog | | |
| 80 | Proxy Sync shows confirmation dialog | | |
| 81 | SSL Renew shows confirmation dialog | | |
| 82 | SSL Issue shows confirmation dialog | | |
| 83 | Buttons disabled during execution | | |
| 84 | Errors shown in toast notifications | | |
| 85 | No unhandled promise rejections (browser console) | | |
| 86 | Normal domains can still be removed | | |
| 87 | Normal sites can still be removed | | |
| 88 | Normal SSL operations work for non-admin domains | | |

## Stability

| # | Item | Result | Notes |
|---|------|--------|-------|
| 89 | Service restart: daemon stops and starts cleanly | | |
| 90 | Repeated UI navigation: no duplicate event handlers | | |
| 91 | Repeated Check Again: no accumulating API calls | | |
| 92 | No unexpected 4xx/5xx in API responses | | |
| 93 | Browser console clean (no exceptions) | | |
| 94 | 24-hour stability test | | Optional RC2 criterion |

## Verification key

| Symbol | Meaning |
|--------|---------|
| ✅ | PASS |
| ❌ | FAIL |
| ⏭️ | SKIPPED (with reason) |
| 🟡 | IN PROGRESS |
| ⛔ | NOT APPLICABLE |
