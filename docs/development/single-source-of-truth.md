# Single Source of Truth

Every type of information in ContainerCP has exactly one owner module.
Other modules consume that module's public API — they never reimplement
or duplicate the logic.

## Rules

1. **One owner per capability** — no module may implement something
   that another module already owns.
2. **Consume, don't copy** — if you need data or functionality from
   another module, call its public API. Do not read its files, do not
   duplicate its logic, do not bypass its abstraction.
3. **Refactor the owner if needed** — if an existing module cannot
   provide what you need, extend or refactor that module. Do not create
   a second implementation elsewhere.

## Ownership map

| Capability | Owner module | Consumers |
|------------|-------------|-----------|
| Runtime synchronization | `RuntimeSynchronizer` (`libs/runtime/`) | Mail, future DNS/Proxy |
| SSL/HTTPS status | `CertificateStore` (`libs/ssl/`) | Sites page, SSL page |
| Container runtime actions | `RuntimeActionExecutor` (`libs/runtime/`) | Sites, future Databases/Redis |
| Container runtime status | `RuntimeActionExecutor::service_status()` | SiteRuntimeManager |
| Service roles | `ServiceRole` (`libs/runtime/`) | SiteRuntimeManager, future modules |
| Docker Compose execution | `CommandExecutor` + `RuntimeActionExecutor` | All runtime operations |
| Site data | `SiteManager` (`libs/site/`) | API, SSL, Runtime bridges |
| Backups | `BackupManager` (`libs/backup/`) | API, CLI |
| Proxy configuration | `ReverseProxyManager` + `NginxProxyProvider` | SSL, Sites |
| Database management | `DatabaseManager` (future) | API, CLI |
| Jobs / async execution | `JobExecutor` (`libs/jobs/`) | SSL issue/renew, runtime actions |
| SQL Console launch sessions | `SqlConsoleSessionManager` (`libs/sqlconsole/`) | future API, future Adminer provider, future native SQL editor |
| SQL Console temporary MariaDB users | `MariaDBProvider` (`libs/database/`) | `DatabaseSqlConsoleService` |
| SQL Console restart-cleanup metadata | `SqlConsoleSessionStore` (`libs/sqlconsole/`) | `DatabaseSqlConsoleService` |
| SQL Console tool providers | `SqlConsoleProvider` (`libs/sqlconsole/`) | Adminer, future native SQL editor |
| Adminer SQL Console runtime | `AdminerSqlConsoleProvider` (`libs/sqlconsole/`) | future proxy route |

## Examples

**Correct:** The API handler for `GET /api/runtime/<id>` calls
`CertificateStore::load_metadata()` + `CertificateStore::https_display_status()`
to get HTTPS status. The HTTPS logic lives in the SSL module.

**Correct:** `SiteRuntimeManager::get_status()` calls
`RuntimeActionExecutor::service_status()` for each service. The Docker
inspection logic lives in the runtime module.

**Wrong:** `SiteRuntimeManager` reading `metadata.json` directly from disk
(bypassing `CertificateStore`). This was fixed in Phase 1 cleanup.

**Wrong:** The UI implementing Docker commands in JavaScript. The UI
only calls the REST API.

## Related documents

- `docs/runtime-architecture.md` — runtime subsystem ownership
- `docs/development/sql-console.md` — SQL Console session ownership and future provider boundary
- `docs/development/coding-rules.md` — development rules
- `docs/ADR/` — architecture decisions
