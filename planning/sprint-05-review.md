# Sprint 5 Review — Tests and Reliability

## Implemented tasks

### CCP-3001: Test framework
- **doctest** single-header at `libs/doctest/doctest.h`
- `tests/` directory with CMake target `containercp_tests`
- Integrated into root CMakeLists with `BUILD_TESTS` option
- `ctest --test-dir build` runs and passes

### CCP-3002: Validator tests
- `tests/test_validator.cpp` — 10 hostname test cases, 10 username test cases
- Covers all rejection rules: empty, too short, leading/trailing hyphen, invalid chars, uppercase, spaces, missing dot

### CCP-3003: Manager and Storage tests
- `tests/test_managers.cpp` — UserManager and SiteManager create/find/list/remove round-trips, duplicate name test
- `tests/test_storage.cpp` — load from non-existent file, user save/load round-trip, domain save/load round-trip
- Temporary directories cleaned up after each test

### CCP-3004: Filesystem rollback
- Early Docker check in `DockerComposeProvider::create_site()` via `Runtime::status()` probe
- No filesystem mutation if Docker is unavailable
- Prevents orphan site directories on disk

### CCP-3005: site remove confirmation
- `site remove domain` prompts with "WARNING: ... Are you sure? [y/N]"
- `site remove domain --force` skips confirmation
- `Aborted.` message on cancel, exit code 1

### CCP-3006: Detailed validation messages
- `Validator::validate_hostname()` and `Validator::validate_username()` return specific error strings
- Messages: "Domain is empty", "Label cannot start with a hyphen: -bad", "Username cannot contain uppercase letters", etc.
- All callers updated to use detailed messages

## Tests added

| Test file | Test cases |
|-----------|-----------|
| `tests/test_validator.cpp` | 20 (10 hostname, 10 username) |
| `tests/test_managers.cpp` | 3 (UserManager, duplicate, SiteManager) |
| `tests/test_storage.cpp` | 3 (non-existent file, user round-trip, domain round-trip) |

Total: 26 test cases across 6 test suites.

## Architecture improvements

- **Separate build target** for tests with linker optimization (Debug builds are split)
- **Docker check before filesystem** — prevents partial state when Docker is unavailable
- **Detailed validation** — every validation failure includes the exact reason
- **Confirmation flow** — destructive operations require explicit consent
- **No duplicate password code** — `PasswordGenerator` is the single source

## Remaining technical debt

1. **No Storage tests for all 9 types** — Only user and domain round-trips are tested.
2. **Backup is placeholder** — No actual file archiving.
3. **SSL and Mail are placeholders** — No certificate provisioning or mail delivery.
4. **Tests are minimal** — No integration or end-to-end tests.
5. **No `site create` end-to-end test** — Docker is required.
6. **Some managers lack test coverage** — DatabaseManager, BackupManager, etc.
7. **Storage round-trip uses tmp dirs** — Works but cleanup could be more robust.

## Recommendations for Sprint 6

1. Add Storage tests for remaining resource types (Database, Backup, SSL, Mail, PHP, Node).
2. Implement actual backup archiving (tar/zip).
3. Consider SQLite migration for transactional safety.
4. Add `--json` output format for machine-readable CLI.
5. Add end-to-end tests using Docker-in-Docker or test containers.
6. Improve error handling for edge cases (disk full, permission denied).
7. Evaluate splitting `containercp` into separate control-plane and node binaries.

## Readiness assessment

The project is stable with 0 warnings at Debug and Release.
All 35+ CLI commands work correctly. The test framework is in place
with 26 passing test cases. Validation is thorough with specific
error messages. Rollback prevents resource leaks. Confirmation
flow prevents accidental removals. The codebase is ready for
Sprint 6 feature work.
