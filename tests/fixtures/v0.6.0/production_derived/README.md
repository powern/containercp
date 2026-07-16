# Production-Derived Fixture

Anonymized dataset based on a real ContainerCP v0.6.0 production
installation at `web2.softico.ua` (2026-07-16 snapshot).

## Origin

- **Source:** Read-only copy of `/srv/containercp/database/*.db`
- **Server:** `web2.softico.ua` (ContainerCP v0.6.0)
- **Snapshot date:** 2026-07-16
- **Verified:** SHA-256 checksums match between source and local copy
- **Source preserved:** `/tmp/containercp-production-storage-20260716T123951Z/raw/`
  (not part of this repository)

## Anonymization

| Field | Treatment |
|-------|-----------|
| Domains | Deterministic mapping: `test-gui-*.local` → `site-*.anonymized.local`; `*.softico.ua` → `*.customer-a.example.com`; `*.softi.co` → `*.customer-b.example.net`; `web2.softico.ua` → `admin.anonymized.example.com` |
| Usernames | `admin` preserved (generic) |
| Passwords (DB) | Replaced with `ANON_PLACEHOLDER_32CHARS_...` |
| Password hashes (auth) | Replaced with `ANON_SHA256_PLACEHOLDER_64CHARS_...` |
| Password hashes (mail) | Replaced with `{SHA512-CRYPT}$6$ANON_PLACEHOLDER_...` |
| DKIM public keys | Replaced with placeholder strings (public info) |
| Smarthost password | Not present (smarthost is disabled) |
| File paths | Domain components anonymized |
| IDs | Preserved exactly |
| Relationships | Preserved exactly |
| Timestamps | Preserved (not sensitive) |

## Edge cases covered

1. **Non-contiguous site IDs:** 1,2,3,4,8,9,10,11 (no 5,6,7)
2. **owner_id=0 for all domains** (system/admin sentinel)
3. **Mail domain with domain_id=0** (external domain not in Domain table)
4. **Reverse proxy with site_id=0** (admin panel)
5. **Empty resource files:** ssl_certificates, access_users, access_grants, mail_aliases (all 0 bytes)
6. **Mail module active** with one mailbox (password hash present)
7. **Smarthost disabled** (`0||587||`)
8. **Backup referencing existing site** (no orphan backup)
9. **Sites all current format** (6-field, all 5 pipes)
10. **PHP versions all migrated** to ContainerCP image

## Security

- No real credentials are included.
- No real password hashes are included.
- No real DKIM private keys are included (only public DNS records, replaced with placeholders).
- The raw production snapshot is stored outside this repository.
- Only the anonymized copy is committed.
