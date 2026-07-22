# ARCH-009: SQL Console Authentication Model

**Status:** Approved
**Target Version:** v0.8.0
**Date:** 2026-07-22
**Scope:** Design-review addendum for Database GUI + Adminer Integration

**Implementation note:** Phase 1 was implemented as the generic
SQL Console domain/session foundation only. It adds in-memory launch
sessions, public-safe serialization, and audit formatting. It does not
add Adminer, MariaDB temporary users, persistence, API routes, or proxy
routing.

---

## 1. Executive Summary

ContainerCP will use **temporary MariaDB users** as the production
authentication model for interactive SQL Console sessions, including the
initial Adminer provider.

The existing application database user must not be reused for Adminer
launch sessions. Reusing it is simpler, but it moves a long-lived
application credential into a browser-facing database administration
surface, weakens revocation, complicates future RBAC, and makes session
boundaries harder to prove.

The approved model is:

```text
ContainerCP admin session
    -> SQL Console launch session
    -> temporary MariaDB user for selected database
    -> Adminer receives temporary credentials over internal SSO only
    -> expiry/revoke drops the temporary user
```

This document compares the two reviewed models and records the decision.

---

## 2. Problem

The Database GUI + Adminer proposal requires one unresolved architectural
choice: how Adminer should authenticate to MariaDB after ContainerCP has
authenticated the administrator.

Two models were reviewed:

- **Option A:** ContainerCP creates a temporary MariaDB user per SQL
  Console session.
- **Option B:** ContainerCP reuses the existing application database user
  for the selected Site/database.

The chosen model must satisfy these requirements:

- ContainerCP remains the primary database management interface.
- Adminer is only an advanced SQL Console provider.
- Administrators never see, type, or copy database passwords.
- Database passwords never appear in URLs, browser storage, logs, or API
  responses.
- SQL Console access is short-lived, revocable, auditable, and compatible
  with future RBAC.
- Arbitrary database access is prevented.

---

## 3. Current Architecture Context

ContainerCP already has database lifecycle and credential boundaries:

- `DatabaseManager` owns database metadata.
- `DatabaseViewService` owns public-safe inventory/detail views.
- `DatabaseLifecycleService` and `DatabaseLifecycleJobService` own
  managed MariaDB create, verify, drop, and metadata recovery.
- `DatabaseDumpService` and `DatabaseDumpJobService` own SQL export and
  import artifacts.
- `DatabaseCredentialRotationService` owns WordPress database credential
  rotation.
- `MariaDBProvider` owns MariaDB operations and already transports
  credentials through temporary option files rather than command-line
  passwords.
- `MariaDBCredentialProvider` owns credential verification, password
  changes, and shared-user assessment.

Current application database credentials are persisted in database
metadata and site runtime files for compatibility. The `db_password`
field is documented as sensitive plaintext technical debt and must not be
exposed through API responses, Web UI, logs, or audit output.

Current UI authentication uses `AuthService` and `SessionManager` for
ContainerCP admin sessions. Frontend API requests send `X-Session-Token`.
The future SQL Console launch flow must not expose database credentials
to the browser or bind database access to JavaScript-visible state.

---

## 4. Option A: Temporary MariaDB User Per Session

### 4.1 Flow

```text
ContainerCP validates admin session
    -> validates database eligibility and RBAC
    -> creates SQL Console launch session
    -> creates temporary MariaDB user
    -> grants selected database privileges
    -> launches Adminer through internal SSO
    -> expires/revokes session
    -> revokes grants and drops temporary user
```

### 4.2 Strengths

- Limits credential lifetime to the SQL Console session.
- Avoids handing the persistent application database password to Adminer.
- Provides explicit revocation independent of the application runtime.
- Supports future RBAC by changing temporary grants per role or policy.
- Supports future read-only or restricted SQL Console modes.
- Creates a distinct database identity for audit and incident response.
- Reduces blast radius if the Adminer PHP session or container is
  compromised.
- Allows ContainerCP to disable SQL Console without changing application
  database credentials.
- Keeps application credentials reserved for application runtime and
  ContainerCP-owned backend operations.

### 4.3 Weaknesses

- Requires MariaDB service-account privileges to create, grant, revoke,
  and drop users.
- Adds cleanup requirements for expiry, logout, daemon restart, and
  failed launches.
- Adds runtime work at launch time.
- Requires persistent metadata or careful startup reconciliation to avoid
  orphaned temporary users.
- Requires additional provider methods and tests.

### 4.4 Compatibility Notes

ContainerCP already creates and manages MariaDB users for database
lifecycle operations. The existing provider layer has patterns for:

- identifier validation,
- `CREATE USER`, `ALTER USER`, `GRANT`, `REVOKE`, and `DROP USER`,
- secure temporary option-file credential transport,
- safe command execution,
- sanitized diagnostics.

Therefore, Option A extends existing architecture rather than introducing
a new kind of capability.

---

## 5. Option B: Existing Application Database User

### 5.1 Flow

```text
ContainerCP validates admin session
    -> validates database eligibility and RBAC
    -> resolves existing application database credentials
    -> launches Adminer through internal SSO
    -> Adminer logs in as application database user
    -> no MariaDB user cleanup required
```

### 5.2 Strengths

- Simpler implementation.
- No temporary MariaDB user lifecycle.
- Lower launch latency.
- Fewer MariaDB privilege requirements.
- Works even if service-account user management privileges are missing.
- No risk of orphaned temporary users.

### 5.3 Weaknesses

- Exposes a long-lived application credential to a browser-facing admin
  tool runtime.
- Revocation is weak: killing the SQL Console session does not invalidate
  the database credential.
- Incident response may require rotating the application database
  password and updating application configuration.
- Future RBAC is hard because the application user usually has one fixed
  privilege set.
- Read-only SQL Console modes are not possible unless a separate user is
  introduced later.
- Query audit cannot easily distinguish normal application activity from
  administrator SQL Console activity.
- Adminer session compromise has access for as long as the application
  password remains valid.
- It increases pressure to pass persistent secrets through internal APIs,
  PHP sessions, or files.
- It conflicts with the product direction that ContainerCP owns database
  management and Adminer is replaceable.

---

## 6. Technical Comparison

| Criterion | Option A: Temporary DB User | Option B: Existing App User | Decision Impact |
|-----------|-----------------------------|------------------------------|-----------------|
| Security | Strongest boundary. Temporary, revocable, per-session identity. Persistent app password stays out of Adminer. | Weaker boundary. Long-lived app credential enters Adminer runtime. Revocation requires app credential rotation. | Option A wins. |
| Implementation complexity | Higher. Requires session metadata, user lifecycle, cleanup, provider methods, tests. | Lower. Mostly credential resolution and Adminer SSO. | Option B wins on simplicity only. |
| Runtime overhead | Extra SQL operations at launch/revoke. Usually small relative to Adminer startup. | Minimal. No user create/drop. | Option B wins, but overhead is acceptable. |
| Cleanup complexity | Requires expiry sweeps, startup cleanup, failed-launch compensation. | Low. Only Adminer/PHP session cleanup. | Option B wins operationally. |
| MariaDB compatibility | Requires CREATE USER/GRANT/REVOKE/DROP USER privileges. Compatible with existing managed lifecycle model. | Works with any valid application user. | Option B wins for minimal privilege environments. |
| Scalability | More database user churn. Needs bounded active sessions and cleanup. | Scales with fewer MariaDB operations. | Option B is lighter, but Option A is acceptable for admin-console usage. |
| Auditability | Strong. Temp username can encode/associate launch session and admin identity. | Weak. DB sees normal app user. | Option A wins. |
| Future RBAC | Strong. Grants can reflect role, read-only mode, destructive-action policy, database scope. | Weak. App user's grants are fixed and application-oriented. | Option A wins. |
| Disaster recovery | Needs orphan cleanup after daemon crash. Can be reconciled by metadata and naming convention. | Simpler state recovery. But compromised app password requires application credential rotation. | Option A wins for incident containment; Option B wins for simple restarts. |
| Operational maintenance | More moving parts. Requires monitoring cleanup health and service-account privilege validation. | Fewer moving parts but higher credential-risk burden. | Option A is safer for production despite maintenance cost. |
| Production readiness | Better security posture when cleanup and validation are implemented. | Faster to ship, but weaker isolation and harder to certify against stated requirements. | Option A is approved. |

---

## 7. Recommended Decision

Use **Option A: temporary MariaDB user per SQL Console session** as the
only production authentication model for Adminer-backed SQL Console.

Do not use Option B for Adminer launch sessions.

If ContainerCP cannot create a temporary SQL Console user for a database,
the API should return a safe unavailable state such as:

```json
{
  "can_sql_console": false,
  "sql_console_block_reason": "temporary_database_user_unavailable"
}
```

The frontend should display the backend reason and must not fall back to
application credentials.

---

## 8. Hybrid Model Review

A limited hybrid model is useful only if the boundary is defined
carefully.

Approved hybrid boundary:

- Interactive, browser-facing SQL Console sessions use temporary MariaDB
  users.
- Existing application database users remain available only for
  ContainerCP-owned backend operations that already require them, such as
  verification, export/import, and credential rotation workflows.
- Future native SQL editor may reuse the same temporary-user policy for
  interactive query execution.

Rejected hybrid boundary:

- Do not implement automatic fallback from temporary SQL Console users to
  the application database user.
- Do not add a hidden Adminer mode that silently logs in as the app user.
- Do not expose application credentials to Adminer because temporary user
  creation failed.

Rationale:

- A fallback to Option B would be activated exactly when the safer model
  is unavailable, creating the highest-risk behavior during degraded
  states.
- It would complicate testing and user expectations.
- It would weaken the security claim that Adminer never receives
  persistent application credentials.

---

## 9. Required Implementation Controls For Option A

### 9.1 Session Metadata

SQL Console session metadata should include:

- launch/session ID,
- database ID,
- site ID,
- admin username,
- admin role or future RBAC subject,
- temporary MariaDB username,
- created timestamp,
- expiry timestamp,
- redeemed timestamp,
- revoked timestamp,
- cleanup status,
- failure code if cleanup fails.

Temporary database passwords must not be persisted unless a later design
explicitly approves encrypted secret storage. The preferred model is to
create the credential, deliver it to Adminer through internal SSO, and
allow Adminer to keep it only in its own short-lived session state.

### 9.2 Temporary User Naming

Temporary usernames should be recognizable and bounded, for example:

```text
ccp_sql_<database_id>_<random_suffix>
```

The exact format must satisfy MariaDB username length limits and
ContainerCP identifier validation.

### 9.3 Grants

Initial Adminer grants should be scoped to the selected database only.
The grant set should be explicit. It should not include global privileges
or access to unrelated databases.

The first version may grant normal application-style privileges for the
selected database if the product intentionally provides an advanced SQL
Console. Future RBAC can reduce grants for read-only roles.

### 9.4 Expiry And Cleanup

Cleanup must run on:

- explicit revoke,
- normal logout if supported,
- launch expiry,
- idle expiry if tracked,
- Adminer heartbeat timeout if implemented,
- daemon startup,
- failed launch compensation.

Cleanup must revoke database grants and drop the temporary user. Cleanup
failures must be logged as safe audit events and retried.

### 9.5 Provider Boundary

MariaDB temporary user lifecycle must live in database provider/service
code, not API handlers, Web UI, or Adminer wrapper code.

Provider methods should reuse existing patterns:

- argument-vector execution,
- secure option files,
- strict identifier validation,
- redacted diagnostics,
- no passwords in argv,
- no raw SQL/passwords in logs.

### 9.6 API Contract

Database views should expose capability fields only:

- `can_sql_console`,
- `sql_console_block_reason`,
- `sql_console_provider`,
- `sql_console_active_sessions` if implemented.

Launch API responses must return only opaque IDs, launch URLs, status,
and expiry metadata. They must never return database credentials.

---

## 10. Production Readiness Requirements

Option A is production-ready only after these controls are validated:

- Temporary user create/grant/verify/drop works on managed MariaDB stacks.
- SQL Console launch fails closed if temporary user creation fails.
- No fallback to application DB credentials exists.
- No database password appears in URL, API JSON, browser localStorage,
  Nginx logs, daemon logs, Docker environment, or command arguments.
- Expired/revoked sessions remove temporary users.
- Daemon restart reconciles previous temporary users from metadata.
- Adminer cannot list or access unrelated ContainerCP-managed databases.
- Adminer is reachable only under the admin-panel domain through the
  approved route.
- Session launch/redeem/revoke/cleanup audit events are safe and useful.
- Future RBAC permission checks can be inserted before launch without
  redesigning Adminer authentication.

---

## 11. Rejected Alternative

Option B, direct reuse of the application database user, is rejected for
interactive Adminer sessions.

Reason:

- It optimizes implementation speed at the expense of credential
  lifetime, revocation, auditability, future RBAC, and incident response.
- It gives Adminer a persistent application secret that survives beyond
  the SQL Console session.
- It makes it harder to prove that SQL Console access is temporary and
  bound to a ContainerCP admin action.

Option B remains acceptable only for existing backend-owned operations
where ContainerCP itself performs the action through approved provider
boundaries and never exposes credentials to the browser or third-party UI
runtime.

---

## 12. Final Decision

The approved authentication model for Adminer / SQL Console is:

```text
Temporary MariaDB user per interactive SQL Console session.
No application database user fallback for Adminer.
Existing application credentials remain limited to backend-owned operations.
```

This decision aligns with ContainerCP's security-first direction and keeps
Adminer replaceable by a future native SQL editor using the same temporary
credential/session model.
