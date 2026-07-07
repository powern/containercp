# Sprint 5 — Tests and Reliability

**Goal:** Make ContainerCP testable and eliminate the remaining
reliability gaps identified in Sprint 4.

After this sprint, the project must have:
- A test framework with unit tests for core logic
- Complete filesystem rollback on creation failure
- Safe `site remove` with confirmation flow
- Clear, specific validation error messages

## Scope

- Test framework setup (doctest single-header)
- Unit tests for Validator (domain, username)
- Unit tests for managers and Storage load/save round-trip
- Filesystem rollback when Docker is unavailable
- `--force` flag for `site remove` with confirmation prompt
- Specific validation error messages

## Out of scope

- Integration tests with real Docker
- SSL certificate provisioning
- Mail server setup
- Reverse proxy configuration
- Web UI
- Multi-node support
- SQLite migration

## Definition of done

1. `cmake --build build && ctest` passes
2. Validator has tests for all rejection cases
3. Storage round-trip tests for every resource type
4. Manager create/find/remove tests for at least User, Site, Domain
5. Failed `site create` leaves no filesystem artifacts
6. `site remove` without `--force` prompts for confirmation
7. Validation errors include the specific reason
8. All existing commands continue working
