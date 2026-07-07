# Sprint 4 Review — Production Ready PHP Hosting

## Implemented functionality

### CCP-2001 Shared utility module
- `PasswordGenerator` — deduplicated password generation from `EnvGenerator` and `SiteCreateOperation`
- `StringUtils` — `sanitize`, `trim`, `bool_to_string`, `string_to_bool`
- `Validator` — `is_valid_hostname`, `is_valid_username`, `normalize_hostname`

### CCP-2002 Domain validation
- RFC 1123 compliant hostname validation
- Rejects empty, over 253 chars, labels over 63 chars, labels with leading/trailing hyphens, missing dots
- Integrated into `SiteCreateOperation::execute()`

### CCP-2003 Username validation
- Allows lowercase, digits, hyphens, underscores (3–32 chars)
- Rejects uppercase, special chars, leading digit/hyphen, trailing hyphen/underscore
- Integrated into `handle_user_create()` and `SiteCreateOperation::execute()`

### CCP-2004 Site remove
- `SiteRemoveOperation` with full cleanup: Docker compose down, filesystem removal, domain/database/backup/SSL/mail resource cleanup, site record removal
- CLI: `containercp site remove <domain>`

### CCP-2005 .env improvements
- `EnvGenerator` accepts DB credentials from the Database resource
- `.env` contains per-site `DB_NAME`, `DB_USER`, `DB_PASSWORD`

### CCP-2006 Dry-run mode
- `--dry-run` flag for `site create` — validates and prints plan without mutation
- Zero filesystem changes, no resource creation, no runtime calls

### CCP-2007 Docker availability cache
- `DockerRuntime::check_docker()` caches result after first `std::system()` call
- `std::system("docker --version")` runs at most once per process

### CCP-2008 Recovery support
- If `provider_.create_site()` fails, `SiteCreateOperation` rolls back all created resources (database, domain, site records)
- Error message indicates rollback occurred

## Architecture improvements

- **libs/utils/** — shared utilities reduce duplication and centralize validation
- **DockerRuntime cache** — eliminates redundant subprocess calls
- **Rollback** — no orphan in-memory resources after failed creation
- **SiteRemoveOperation** — single class for complete site teardown
- **Dry-run** — safe validation path independent of mutation

## Technical debt remaining

1. **No test suite** — Project has no test framework. All verification is manual.
2. **Filesystem rollback gap** — If Docker is unavailable, the site directory is created before the failure and not cleaned up by rollback (filesystem is created inside `DockerComposeProvider`, which is outside `SiteCreateOperation`'s rollback scope).
3. **Backup is placeholder** — No actual file archiving.
4. **SSL and Mail are placeholders** — No certificate provisioning or mail delivery.
5. **site remove confirmation** — No `--force` flag or user prompt yet.
6. **DB fields not persisted on Site** — `Site.db_name/db_user/db_password` are carried in memory only during creation.
7. **No error detail in validation** — "Invalid domain" / "Invalid username" could include the specific reason.

## Recommendations for Sprint 5

1. Add a test framework (Catch2 or doctest) with unit tests for managers and validators.
2. Fix filesystem rollback — move filesystem creation into SiteCreateOperation or have the provider expose a cleanup method.
3. Implement `--force` flag for `site remove` to skip confirmation.
4. Enhance validation error messages with specific reasons.
5. Consider upgrading Storage to support the Site DB fields.
6. Add a `--json` output flag for machine-readable CLI responses.
7. Evaluate SQLite for transactional safety instead of pipe-delimited files.

## Readiness for Sprint 5

The codebase is stable with 0 warnings at both Debug and Release
configurations. All existing 35+ CLI commands continue working.
Validation, rollback, dry-run, and site remove provide a reliable
foundation for adding user management, SSL provisioning, mail
setup, and reverse proxy integration in Sprint 5.
