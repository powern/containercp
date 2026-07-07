# Sprint 3 Review — Infrastructure Compatibility

## Implemented resources

| Resource | Module | Manager | Storage file |
|----------|--------|---------|-------------|
| User | `libs/user/` | `UserManager` | `users.db` |
| Domain | `libs/domain/` | `DomainManager` | `domains.db` |
| PHP Version | `libs/php/` | `PhpVersionManager` | `php_versions.db` |
| Database | `libs/database/` | `DatabaseManager` | `databases.db` |
| Backup | `libs/backup/` | `BackupManager` | `backups.db` |
| SSL | `libs/ssl/` | `SslCertificateManager` | `ssl_certificates.db` |
| Mail | `libs/mail/` | `MailDomainManager` | `mail_domains.db` |

Every resource inherits from `core::Resource`, is persisted via pipe-delimited
text files in `/srv/containercp/database/`, and is registered in
`ServiceRegistry` with load/save lifecycle.

## CLI commands added

**User:** `create`, `list`, `show`, `remove`
**Domain:** `list`, `show`, `remove`
**PHP:** `list`, `show`, `default`
**Database:** `list`, `show`, `remove`
**Backup:** `create`, `list`, `show`, `remove`
**SSL:** `list`, `show`, `enable`, `disable`
**Mail:** `list`, `show`, `enable`, `disable`

## Files persisted

All under `/srv/containercp/database/`:

- `nodes.db` — Node resource
- `sites.db` — Site resource
- `users.db` — User resource
- `domains.db` — Domain resource
- `php_versions.db` — PHP version catalog (seeded: 8.2, 8.3, 8.4)
- `databases.db` — Database resource
- `backups.db` — Backup resource
- `ssl_certificates.db` — SSL certificate resource
- `mail_domains.db` — Mail domain resource

## Architecture changes

- `Config` gained path helpers (`database_dir`, `sites_dir`, `templates_dir`, `users_dir`)
- `Storage` handles 9 resource types
- `ServiceRegistry` manages 9 service objects
- `SiteCreateOperation` auto-creates Domain and Database resources
- `DockerComposeProvider` reads PHP version from `PhpVersionManager`
- `ComposeGenerator` template uses `{{PHP_IMAGE}}` placeholder instead of hardcoded image

## Remaining technical debt

1. **No test suite** — Project has no test framework.
2. **Partial site creation** — If Docker fails mid-creation, site record + domain + database are created in memory but not persisted (save is skipped on failure).
3. **Static `.env` credentials** — Database resource stores generated passwords but `.env` still uses static `site_db`/`site_user`.
4. **No `site remove` command** — Created sites cannot be removed via CLI.
5. **No input validation** — Domain format, owner name, and username are not validated beyond empty checks.
6. **Backup is placeholder** — No actual file archiving.
7. **SSL and Mail are placeholders** — No certificate provisioning or mail delivery.
8. **Docker check runs on every command** — Not cached.
9. **Contains duplicate password generation code** — `EnvGenerator` and `SiteCreateOperation` both implement `generate_password` with identical logic.

## Recommendations for Sprint 4

1. Implement `site remove` with full cleanup (directory, compose down, database removal).
2. Add a test framework (Catch2 or doctest) and basic unit tests for managers.
3. Extract duplicate `generate_password` into a shared utility.
4. Add domain format validation.
5. Cache Docker availability check per process.
6. Integrate Database resource credentials into `.env` generation.
7. Add `--force` flag to `site create` for recovery from partial state.
8. Consider switching from pipe-delimited text to SQLite for transactional safety.
