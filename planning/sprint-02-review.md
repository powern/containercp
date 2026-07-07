# Sprint 2 Review — PHP Hosting MVP

## What was improved

### Architecture
- **Config path helpers** — Added `database_dir()`, `sites_dir()`, `templates_dir()` to `Config`. All hardcoded path concatenations now use these methods, eliminating magic strings across 4 modules.
- **Reduced coupling** — `SiteCreateOperation` no longer depends on `ResourceManager` (unused parameter removed). The operation now only depends on `SiteManager` and `HostingProvider`.
- **Deduplicated runtime code** — `DockerRuntime::start_site()` now delegates to `create_site_stack()`. `DockerRuntime::status()` uses `run_command()` like all other methods instead of duplicating the check/system/logic pattern.

### Code quality
- **constexpr** — Added `constexpr` to directory arrays (`SiteLayout.cpp`), password lengths (`EnvGenerator.cpp`), character table, and Docker check command string.
- **Include hygiene** — Removed unnecessary includes (`<cstdint>` from Storage.cpp, `<string>` from TemplateEngine.cpp and DockerRuntime.cpp).
- **Release build** — Builds with `-Wall -Wextra -Wpedantic` at Release level with zero warnings.

## Remaining technical debt

1. **No automated tests** — The project has no test framework or test suite.
2. **Error handling is minimal** — `std::system()` errors are passed through but not recovered from.
3. **No input validation beyond empty checks** — Domain format and owner format are not validated.
4. **Docker check runs on every command** — The `check_docker()` call is not cached.
5. **Site remove not implemented** — No `site remove` CLI command.
6. **All state is in memory first** — Partial failures can leave stale in-memory state if Docker is unavailable.
7. **No clang-format available** — Coding style is consistent but not enforced by a formatter.
8. **Single executable** — Control Plane and Node are not yet separable processes.

## Recommendations for Sprint 3

1. Add a test framework (Catch2 or doctest).
2. Implement `site remove` command with directory cleanup.
3. Cache the Docker availability check result per process.
4. Add domain name validation (basic regex).
5. Consider per-site `.env` overwrite protection.
6. Add a `--dry-run` flag to `site create`.
7. Set up clang-format and a pre-commit hook.
8. Split the `DockerRuntime::run_command` helper into smaller units for testability.
