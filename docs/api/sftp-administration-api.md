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

### Generate key pair

`POST /api/access/sftp/users/{id}/keys/gen`

```json
{
  "type": "ed25519",
  "comment": "operator@example.test",
  "enabled": true
}
```

The supported types are `ed25519` and `rsa`. `publicKey` is not required for
this operation. ContainerCP generates the pair in a private temporary
directory, validates the generated OpenSSH public key with the same validator
used by imports, persists only the public key, and rebuilds `authorized_keys`.
The private key is returned only in this response and is not stored or logged.

The generated response includes `id`, `keyType`, `fingerprint`, `comment`,
`enabled`, `publicKey`, and `privateKey`. A failed `authorized_keys` rebuild
rolls back the new `access_keys` record.

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

The response also includes a `reconciliation` array with one record for each
startup reconciliation unit:

```json
{
  "runtimeState": "degraded",
  "recordsInspected": 2,
  "recordsFixed": 1,
  "recordsFailed": 1,
  "reconciliation": [
    {
      "phase": "user",
      "item": "system account lifecycle reconciliation",
      "state": "failed",
      "error": "reconcile_users:1 failures ...",
      "recoveryAction": "Inspect system_accounts mapping, Linux user/group/home state, then retry SFTP reconciliation"
    }
  ]
}
```

### Trigger reconciliation

`POST /api/access/sftp/reconcile`

Invokes provider retry_reconciliation(). Returns 409 if concurrently busy and
returns the bounded reconciliation diagnostic when recovery still fails.

`GET /api/health` now includes a `modules.sftp` report. A `degraded` or
`failed` SFTP provider changes the aggregate health status instead of being
hidden by the mail-only health report.

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
| `sftp_user_provision_failed` | Provisioning failed; `details` identifies whether the state is recoverable |
| `unmanaged_account_conflict` | Linux username exists without a persisted ContainerCP ownership proof; no adoption or deletion is attempted |
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
- `error` — Lifecycle failure; the `lastError` and reconciliation detail identify whether retry is safe

## Safe Provisioning Recovery

ContainerCP persists the `system_accounts` mapping before creating the Linux
identity. If a later provisioning step fails, the AccessUser is retained as
disabled and the mapping remains in `provisioning` or `error` state. Startup or
explicit reconciliation may complete the transaction only when the observed
UID, primary GID, managed home, shell, and configured managed ranges match the
persisted mapping.

An existing Linux account with the derived `au-` name is never adopted based on
its name alone. When no mapping proves ownership, the API returns
`unmanaged_account_conflict` with the observed identity and a suggested
read-only inspection path. Foreign accounts are never overwritten or deleted.
