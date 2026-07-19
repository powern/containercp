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
- splits stdin into a MariaDB defaults file and SQL file;
- sets both files to `0600`;
- runs `mariadb --defaults-extra-file=<temp-client-file> < <temp-sql-file>`;
- removes the temporary directory through `trap` cleanup.

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

Before rotating a password, the saga must check whether the same MariaDB username is used by more than the intended account scope. A shared user can affect multiple databases or applications. The provider exposes a shared-user detection query boundary; WP-5 must consume that signal before mutation and fail closed or require explicit operator handling.

## Failure Handling

Provider failures are redacted and generic. The rotation saga owns compensation and manual recovery state. The provider must not claim a rotation succeeded; it can only report whether an individual MariaDB operation completed.

## Validation Status

Current validation is fake-runner unit coverage only. No production MariaDB service, production database, production credential, or `wp-config.php` file has been touched.
