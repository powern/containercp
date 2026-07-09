## New Resources

No new resource types. The existing `SslCertificate` resource is
extended:

### Extended fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `issued_at` | string (ISO-8601) | `""` | Date of certificate issuance |
| `expires_at` | string (ISO-8601) | `""` | Certificate expiration date |
| `renew_after` | string (ISO-8601) | `""` | Date when renewal should trigger |
| `last_error` | string | `""` | Last ACME error message |
| `https_enabled` | bool | `false` | Whether HTTPS is currently active |
| `redirect_enabled` | bool | `false` | Whether HTTP→HTTPS redirect is active |
| `domains` | string (comma-separated) | `""` | All domains covered by certificate (SAN) |
| `challenge_type` | string | `""` | Challenge used: "http-01" or "dns-01" |
| `last_validation` | string (ISO-8601) | `""` | Last successful preflight validation |
| `renew_attempts` | int | `0` | Consecutive failed renewal attempts |
| `version` | int | `1` | Metadata schema version for forward compat |

### Status values

| Status | Meaning |
|--------|---------|
| `http_only` | No certificate, no SSL resource exists. Implicit state. |
| `issuing` | ACME challenge in progress |
| `active` | Certificate issued, HTTPS enabled |
| `error` | Last ACME operation failed |
| `disabled` | HTTPS disabled by user, certificate may or may not exist |

Note: `HTTP_ONLY` is an **implicit state** — no SslCertificate record
exists for the site. The REST API and GUI derive this state by absence.

## Managers

### SslCertificateManager (extended)

Existing methods remain unchanged. New methods:

| Method | Description |
|--------|-------------|
| `find_by_domain(domain)` | Find cert by domain (already exists) |
| `update_status(id, status)` | Update certificate status |
| `update_https(id, enabled, redirect)` | Update HTTPS + redirect flags |
| `set_metadata(id, issued_at, expires_at, renew_after)` | Set dates after issue/renew |
| `set_error(id, error_msg)` | Set last error and set status to ERROR |
| `find_due_for_renewal()` | Find ACTIVE certs where renew_after <= now |
| `find_expiring_before(date)` | Find ACTIVE certs expiring before date |
| `find_all_with_status()` | Return all certs grouped by status |
| `get_or_create(domain_id, domain)` | Return existing cert or create HTTP_ONLY placeholder |

### RenewalScheduler (new)

| Method | Description |
|--------|-------------|
| `start()` | Start background thread |
| `stop()` | Stop background thread |
| `check()` | Scan all certs, renew due ones |

The scheduler runs inside the daemon process. It checks every 24 hours.
No external cron dependency. The daemon is already long-running, so the
scheduler is a simple `std::thread` with a `sleep_until` loop.

Scheduler logic:
1. Enumerate all certificates with status `active` and `auto_renew == true`.
2. For each, compare current date with `renew_after`.
3. If due, call `CertificateProvider::renew(domain)`.
4. On success: update metadata, reset `renew_attempts` to 0.
5. On failure: increment `renew_attempts`, set `last_error`, log warning.
   Old certificate is kept and continues working.
6. If `renew_attempts >= 7` (one week of daily failures), transition
   status to `error` and disable HTTPS. Site reverts to HTTP-only.
7. Log summary: "Renewal check complete: N active, M renewed, F failed".

## Storage

### Database: `ssl_certificates.db`

Extended pipe-delimited format:

```
id|domain_id|domain|provider|certificate_path|key_path|chain_path|issued_at|expires_at|renew_after|status|auto_renew|https_enabled|redirect_enabled|domains|challenge_type|last_error|last_validation|renew_attempts|version
```

### Disk storage: `/srv/containercp/ssl/<site-id>/`

Storage is keyed by Site ID, not domain name. This allows future
domain renames without moving certificate storage. The current domain
is stored in metadata only.

```
/srv/containercp/ssl/
├── account.pem                        — ACME account private key
└── <site-id>/
    ├── metadata.json                  — single source of truth
    ├── fullchain.pem                  — full certificate chain (0644)
    ├── privkey.pem                    — private key (0600)
    └── chain.pem                      — CA chain only (0644)
```

### StorageManager: `CertificateStore`

A dedicated `CertificateStore` class handles all disk I/O. Providers
never manipulate files directly. Only `CertificateStore` performs:

- `save_metadata(site_id, metadata)` — atomic write with fsync + rename
- `load_metadata(site_id)` — parse JSON, return Metadata struct
- `save_fullchain / save_privkey / save_chain` — atomic PEM writes
- `save_all(site_id, ...)` — batch write all files
- `load_fullchain / load_privkey / load_chain` — read PEM files
- `remove_all(site_id)` — delete all files and directory
- `enumerate()` — scan ssl_root for numeric directories
- `validate(site_id)` — check files, permissions, metadata integrity

### metadata.json format

```json
{
    "version": 1,
    "site_id": 1,
    "provider_id": "letsencrypt",
    "certificate_type": "pem",
    "status": "active",
    "domains": ["example.com", "www.example.com"],
    "issued_at": "2025-07-08T12:00:00Z",
    "expires_at": "2025-10-06T12:00:00Z",
    "renew_after": "2025-09-06T12:00:00Z",
    "https_enabled": true,
    "redirect_enabled": false,
    "auto_renew": true,
    "challenge_type": "http-01",
    "last_validation": "2025-07-08T11:55:00Z",
    "last_error": "",
    "renew_attempts": 0,
    "fingerprint_sha256": "abc123...",
    "serial_number": "04:AB:CD...",
    "issuer": "CN=R3, O=Let's Encrypt, C=US",
    "subject": "CN=example.com",
    "created_at": "2025-07-08T12:00:00Z",
    "updated_at": "2025-07-08T12:00:00Z"
}
```

The `version` field enables forward-compatible schema migrations.
Future readers must tolerate unknown fields. Writers always include
all known fields. Timestamps use ISO-8601 UTC.


---
Back to [ARCH-005 index](ARCH-005-SSL-Management.md)
