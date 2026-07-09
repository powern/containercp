# Development Rules

## Architecture order

Every subsystem must be designed in this order:

```
Core → Resource → Manager → Storage → Provider → Daemon → REST API → Web UI → CLI → Tests
```

## API first

Every operation must be available through the REST API before any
other interface. CLI and Web UI are only clients of the API.

## Core principles

1. **API first** — REST API before CLI or Web UI
2. **Daemon owns business logic** — thin clients only
3. **Everything is a Resource** — uniform entities, managers, storage
4. **Providers isolate external systems** — never depend directly on tools
5. **Configuration is editable without recompilation** — templates on disk
6. **Production before optimization** — simple over clever
7. **Backward compatibility** — never break existing commands
8. **One responsibility per subsystem** — clean module boundaries
9. **Resources are always linked** — site_id foreign keys throughout
10. **Validation-driven development** — VM validation is the primary quality gate

## Validation-driven development

Validation on the official VM is the primary quality gate. Unit tests
and integration tests are necessary but no longer sufficient. Real
deployment and real usage determine whether an Epic is complete.

See `planning/TEST_ENVIRONMENT.md` for VM setup.

## Related documents

- `AGENTS.md` — main entry point with safety rules and commit workflow
- `docs/development/single-source-of-truth.md` — SSOT rules
- `planning/TEST_ENVIRONMENT.md` — validation VM setup
- `docs/ADR/` — architecture decisions
