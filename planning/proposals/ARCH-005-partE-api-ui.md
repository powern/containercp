## REST API

### Design principles

- All endpoints under `/ssl/`.
- Consistent JSON envelope `{"success": true, "data": ...}`.
- Sites with no SSL resource return `HTTP_ONLY` state.
- SSL never blocks site operations.

### Endpoints

```
GET    /ssl
       → list SSL state for ALL sites (including HTTP_ONLY)
       → {"success":true, "data": [
           {"domain": "site1.local", "status": "active", "expires_at": "...", ...},
           {"domain": "site2.local", "status": "http_only", ...},
           {"domain": "site3.local", "status": "error", "last_error": "...", ...}
         ]}

GET    /ssl/<domain>
       → SSL details for one domain
       → {"success":true, "data": {
           "domain": "example.com",
           "provider": "letsencrypt",
           "status": "active",
           "issued_at": "2025-07-08T12:00:00Z",
           "expires_at": "2025-10-06T12:00:00Z",
           "days_remaining": 90,
           "auto_renew": true,
           "https_enabled": true,
           "redirect_enabled": false,
           "last_error": ""
         }}

POST   /ssl/<domain>/issue
       → issue new certificate via ACME HTTP-01
       → site stays HTTP during issuance
       → {"success":true, "data": {"domain": "...", "status": "issuing"}}
       → on failure: {"success":false, "error": "ACME challenge failed: ...",
          "data": {"domain": "...", "status": "error", "last_error": "..."}}

POST   /ssl/<domain>/renew
       → force renew existing certificate
       → old cert kept until new one is issued
       → same response format as /issue

POST   /ssl/<domain>/enable
       → enable HTTPS for a domain that has a valid certificate
       → attaches cert to proxy, reloads nginx
       → {"success":true, "data": {"domain": "...", "https_enabled": true}}
       → on failure: {"success":false, "error": "No valid certificate for domain"}

POST   /ssl/<domain>/disable
       → disable HTTPS, revert to HTTP-only
       → certificate files kept on disk
       → {"success":true, "data": {"domain": "...", "https_enabled": false}}

POST   /ssl/<domain>/redirect/enable
       → enable HTTP → HTTPS redirect
       → only valid when HTTPS is enabled
       → {"success":true, "data": {"redirect_enabled": true}}

POST   /ssl/<domain>/redirect/disable
       → disable HTTP → HTTPS redirect
       → both HTTP and HTTPS serve content
       → {"success":true, "data": {"redirect_enabled": false}}

GET    /ssl/<domain>/status
       → quick status check
       → {"success":true, "data": {
           "status": "active",
           "https_enabled": true,
           "days_remaining": 90,
           "last_error": ""
         }}

GET    /ssl/providers
       → list available certificate providers
       → {"success":true, "data": [
           {"name": "letsencrypt", "supports_dns": false, "description": "Let's Encrypt ACME HTTP-01"},
           {"name": "custom", "supports_dns": false, "description": "Custom PEM certificates"}
         ]}
```

### HTTP status codes

| Code | Meaning |
|------|---------|
| 200 | Success |
| 400 | Validation error (invalid domain, bad state transition) |
| 404 | Domain not found |
| 409 | Conflict (already issuing, wrong state) |
| 500 | ACME server error or internal error |

### Error responses (consistent)

```json
{
    "success": false,
    "error": "Human-readable error message",
    "data": {
        "domain": "example.com",
        "status": "error",
        "last_error": "ACME challenge failed: could not connect to ..."
    }
}
```

## Web UI

### Design principle

**Every site is visible on the SSL page, regardless of SSL state.**
HTTP-only sites are shown with clear state indication.

### SSL page (table view)

The SSL page lists every site on the system, not just sites with
certificates.

| Column | Content |
|--------|---------|
| Domain | Clickable link to detail |
| State | Badge with color: HTTP_ONLY (gray), ISSUING (blue), ACTIVE (green), ERROR (red), DISABLED (gray) |
| HTTPS | On/Off badge or "N/A" for HTTP_ONLY |
| Expiration | Date + days remaining, or "—" for HTTP_ONLY/ERROR/DISABLED |
| Actions | Context-dependent buttons |

### State-dependent actions

| State | Available actions |
|-------|------------------|
| `HTTP_ONLY` | [Issue Certificate] |
| `ISSUING` | (spinner, no actions) |
| `ACTIVE` | [Disable HTTPS] [Renew] [Toggle Redirect] [View Details] |
| `ERROR` | [Retry Issue] [View Error] |
| `DISABLED` | [Enable HTTPS] [Issue New Certificate] |

### Per-domain detail view

Opens when clicking a domain name. Shows:

- Domain name
- State badge (color-coded)
- Provider name
- Issued date (or "—")
- Expiration date (or "—")
- Days remaining with color coding:
  - Green: >30 days
  - Yellow: 7–30 days
  - Red: <7 days or expired
- HTTPS toggle (Enable/Disable)
- HTTP→HTTPS redirect toggle (only when HTTPS is enabled)
- Auto Renew toggle
- Last error message (if any), with red background
- Certificate file paths (for debugging)

### Site detail page integration

The Site detail page already has an SSL tab. It shows the same
state-dependent card as the SSL detail view, with the same actions.

### Dashboard card

New dashboard card "SSL":

| Metric | Color |
|--------|-------|
| Total sites | default |
| HTTPS active | green |
| Expiring within 30 days | yellow |
| Errors | red |

### Failure display

SSL errors are displayed prominently:
- Red badge in the SSL table row
- Red error box in the detail view with the exact error message
- Dashboard shows error count
- Errors never block site operations — the site continues serving HTTP

## CLI

### Commands

```
ssl list
    → list SSL state for ALL sites
    → color-coded status

ssl show <domain>
    → show SSL details for one domain
    → includes all metadata, days remaining, error messages

ssl issue <domain>
    → issue certificate for domain
    → async: returns immediately, shows "ISSUING" state
    → use `ssl show` to check result

ssl renew <domain>
    → force renew certificate
    → keeps old cert until new one is issued

ssl enable <domain>
    → enable HTTPS

ssl disable <domain>
    → disable HTTPS, keep certificate files

ssl redirect enable <domain>
    → enable HTTP→HTTPS redirect

ssl redirect disable <domain>
    → disable HTTP→HTTPS redirect

ssl status <domain>
    → quick status check
```

### Output format

```
$ containercp ssl show example.com
Domain:        example.com
State:         ACTIVE
Provider:      Let's Encrypt
Issued:        2025-07-08
Expires:       2025-10-06 (90 days)
Auto Renew:    yes
HTTPS:         enabled
Redirect:      disabled
Last Error:    (none)
```

```
$ containercp ssl list
Domain                    State       HTTPS   Expires
────────────────────────────────────────────────────────
example.com               ACTIVE      on      2025-10-06 (90d)
test.local                HTTP_ONLY   —       —
broken.local              ERROR       off     ACME challenge failed: ...
```


---
Back to [ARCH-005 index](ARCH-005-SSL-Management.md)
