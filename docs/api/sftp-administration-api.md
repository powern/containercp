# SFTP Administration API — ARCH-010

Base URL: `/api/access/sftp`

All endpoints require administrator authentication.

Error responses use the envelope `{"success":false,"error":"<code>","details":"<message>"}`.

---

## Users

### List users

`GET /api/access/sftp/users`

Response:

```json
{
  "success": true,
  "data": [
    {"id": 1, "username": "backup-bot", "enabled": true},
    {"id": 2, "username": "deploy-user", "enabled": false}
  ]
}
```

### Get user

`GET /api/access/sftp/users/{id}`

Response includes Linux username, lifecycle state, key/grant counts, and last error.

### Create user

`POST /api/access/sftp/users`

```json
{"username": "backup-bot", "enabled": true}
```

### Update user

`PATCH /api/access/sftp/users/{id}`

```json
{"enabled": false}
```

### Delete user

`DELETE /api/access/sftp/users/{id}`

Removes the managed Linux account and cleans up SSH configuration.

### Retry user reconciliation

`POST /api/access/sftp/users/{id}/retry`

Triggers full provider reconciliation for all users.

---

## SSH Keys

### List keys

`GET /api/access/sftp/users/{id}/keys`

### Get key

`GET /api/access/sftp/users/{id}/keys/{key_id}`

### Add key

`POST /api/access/sftp/users/{id}/keys`

```json
{
  "publicKey": "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAA...",
  "comment": "Administrator laptop",
  "enabled": true
}
```

Validates the key, checks for duplicates, persists, and rebuilds authorized_keys.

### Update key

`PATCH /api/access/sftp/users/{id}/keys/{key_id}`

```json
{"enabled": false}
```

### Delete key

`DELETE /api/access/sftp/users/{id}/keys/{key_id}`

Removes the key and rebuilds authorized_keys.

### Rebuild authorized_keys

`POST /api/access/sftp/users/{id}/keys/rebuild`

Re-renders the authorized_keys file from persisted key records.

---

## Site Grants

### List grants

`GET /api/access/sftp/users/{id}/grants`

### Get grant

`GET /api/access/sftp/users/{id}/grants/{site_id}`

### Create grant

`POST /api/access/sftp/users/{id}/grants`

```json
{"siteId": 1, "permission": "rw"}
```

Permissions: `ro` (read-only) or `rw` (read-write).

### Update grant

`PATCH /api/access/sftp/users/{id}/grants/{site_id}`

```json
{"permission": "ro"}
```

Revokes old permission, applies new one.

### Revoke grant

`DELETE /api/access/sftp/users/{id}/grants/{site_id}`

Removes the grant and unmounts bind mount.

### Retry grant

`POST /api/access/sftp/users/{id}/grants/{site_id}/retry`

Retries the grant application after a failure.

---

## Status and Reconciliation

### Global status

`GET /api/access/sftp/status`

Returns runtime state, reconciliation record counts, and bounded errors.

### Trigger reconciliation

`POST /api/access/sftp/reconcile`

Invokes provider retry_reconciliation(). Returns 409 if concurrently busy.

---

## Error Codes

| Code | Description |
|------|-------------|
| `sftp_user_not_found` | Requested access user does not exist |
| `sftp_user_invalid` | Invalid user data in request |
| `sftp_user_duplicate` | Username already exists |
| `sftp_key_invalid` | SSH public key validation failed |
| `sftp_key_duplicate` | Key fingerprint already registered for this user |
| `sftp_key_not_found` | Requested key does not exist |
| `sftp_grant_invalid` | Invalid grant data (bad permission, missing siteId) |
| `sftp_grant_not_found` | Grant does not exist |
| `sftp_grant_conflict` | Grant already exists for this user and site |
| `sftp_site_not_found` | Requested Site does not exist |
| `sftp_runtime_degraded` | Provider is in Degraded state |
| `sftp_reconciliation_busy` | Reconciliation already in progress |
| `sftp_backend_failure` | Internal lifecycle operation failed |

## HTTP Status Codes

| Code | Usage |
|------|-------|
| 200 | Success |
| 401 | Unauthenticated |
| 403 | Unauthorized |
| 404 | Resource not found |
| 409 | Conflict or lifecycle busy |
| 422 | Validation error |
| 500 | Backend failure |

## Lifecycle States

- `none` — User exists in the access database but has no Linux account yet
- `provisioning` — Linux account creation in progress or incomplete
- `active` — Linux account provisioned and ready
- `removing` — Account removal in progress
- `error` — Non-recoverable lifecycle failure, requires operator intervention
