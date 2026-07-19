# MariaDB Credential Provider

## Purpose

`MariaDBCredentialProvider` is the provider boundary used by the WordPress database credential rotation foundation. It performs physical MariaDB credential checks and password changes for per-site MariaDB services without exposing passwords through argv, logs, API responses, CLI output, job messages, or Web UI state.

## Current Scope

- MariaDB only.
- Per-site Docker Compose MariaDB service only.
- Provider boundary only; no API, CLI, GUI, job, or production rotation is enabled by this provider alone.
- Imported WordPress databases and future ContainerCP-managed databases use the same `user@host` identity model.

## Secret Transport

The provider writes a protected host-side stdin bundle with mode `0600`, passes it through `CommandExecutor::run_with_stdin_file()`, and runs a fixed in-container script that:

- creates a private temporary directory;
- reads a length-framed `CONTAINERCP-MARIADB-FRAME-V1` payload;
- splits the framed payload into separate MariaDB defaults and SQL files without delimiter parsing;
- sets both files to `0600`;
- runs `mariadb --batch --raw --skip-column-names --defaults-extra-file=<temp-client-file> < <temp-sql-file>`;
- removes the temporary directory through `trap` cleanup.

The framed host payload format is:

```text
CONTAINERCP-MARIADB-FRAME-V1\n
<defaults-byte-length>\n
<sql-byte-length>\n
<defaults-bytes><sql-bytes>
```

The old `--CONTAINERCP-SQL--` delimiter is no longer used. Payload splitting is length-framed, so delimiter-like text inside a credential is data rather than syntax.

Provider values are accepted only when they match the reviewed transport contract:

- MariaDB identity values (`User`, `Host`, client `user`, client `host`) must be 1 to 256 bytes and use `A-Z`, `a-z`, `0-9`, `_`, `-`, `.`, `@`, `%`, `:`, `$`.
- Password values may be 0 to 256 bytes. Empty imported passwords are supported for verification and rollback.
- Password values may contain spaces, quotes, backslashes, `#`, `;`, `=`, brackets, tabs, newlines, carriage returns, and non-ASCII bytes.
- Password values reject NUL, unsupported control bytes, DEL, and overlong values because those cannot be represented safely across the MariaDB client option file and SQL literal paths.

Client option-file values are escaped before being written to the defaults payload. SQL password literals are escaped separately before `ALTER USER`. Values outside the contract fail closed with a generic `credential_transport_invalid` error before any secret bundle is written.

Passwords are intentionally absent from:

- command argv;
- shell script text;
- provider result messages;
- logs;
- future job/API/CLI/UI surfaces.

## Minimum Grant Model

The preferred operational model is a dedicated local MariaDB administration account inside each site database service. Root is bootstrap/break-glass only and must not become the normal runtime credential once a narrower account exists.

The narrow account must be able to:

- authenticate locally inside the MariaDB container;
- verify a target account by allowing the provider to run a harmless `SELECT 1` as that account;
- change one approved target account password with `ALTER USER '<user>'@'<host>' IDENTIFIED BY '<password>'`;
- run `FLUSH PRIVILEGES` where required by MariaDB version/configuration;
- query `mysql.user` for the target `User` value to detect shared-user risk before rotation.

Recommended grant direction for a future managed bootstrap account:

```sql
CREATE USER 'containercp_credential_admin'@'localhost' IDENTIFIED BY '<generated-secret>';
GRANT SELECT ON mysql.user TO 'containercp_credential_admin'@'localhost';
GRANT CREATE USER ON *.* TO 'containercp_credential_admin'@'localhost';
GRANT RELOAD ON *.* TO 'containercp_credential_admin'@'localhost';
```

MariaDB privilege behavior for `ALTER USER` varies by version and server configuration. Integration validation must confirm whether the narrow account additionally requires broader account-management privileges on the target deployment. If a deployment cannot support a narrow account, use root only as an explicit operator-approved compatibility path and record the residual risk.

## Shared-User Risk

Before rotating a password, the saga must check whether the exact MariaDB `User` + `Host` identity is shared beyond the intended account scope. A shared user can affect multiple databases or applications.

`MariaDBCredentialProvider::detect_shared_user()` returns a structured assessment rather than a boolean-only result:

- `not_shared` — exactly one `User` + `Host` identity exists, the username has no other host identities, and the exact identity has one schema grant.
- `shared` — the exact identity appears reusable across multiple schema grants.
- `unknown` — command output is missing, malformed, ambiguous, or otherwise insufficient.
- `identity_missing` — the exact identity does not exist.
- `multiple_host_identities` — the same username exists under other `Host` values or the exact host is missing while other hosts exist.
- `metadata_conflict` — ContainerCP metadata/runtime comparison found conflicting identity ownership; the rotation adapter produces this before the low-level provider query when more than one database metadata record in the same site references the target MariaDB user.

The provider queries machine-readable tab-delimited output and parses it strictly. Empty, malformed, duplicate, missing, unexpected, or command-failure output fails closed as `unknown`; it never silently defaults to `not_shared`. Before this provider query, the rotation adapter also checks ContainerCP database metadata and blocks duplicate same-site metadata references to the same MariaDB user as `metadata_conflict`. The rotation saga blocks every state except `not_shared` before password generation or mutation.

## Failure Handling

Provider failures are redacted and generic. The rotation saga owns compensation and manual recovery state. The provider must not claim a rotation succeeded; it can only report whether an individual MariaDB operation completed.

## Validation Status

Current validation is fake-runner unit coverage only. No production MariaDB service, production database, production credential, or `wp-config.php` file has been touched.
