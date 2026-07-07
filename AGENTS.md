# ContainerCP — Agent Rules

## Product Vision

ContainerCP is a modern, open-source container-oriented hosting
control panel designed for system administrators, developers, and
hosting providers.

Read `planning/PRODUCT_VISION.md` before making any architectural
decision.

## Planning workflow

Before planning ANY Epic, read these documents IN ORDER:

1. `planning/PRODUCT_VISION.md` — product vision and principles
2. `planning/product-roadmap.md` — version milestones
3. `planning/product-validation.md` — acceptance checklist
4. `planning/backlog.md` — current priorities
5. Latest Sprint/Epic Reviews — recent progress and debt
6. `docs/ADR/` — architecture decisions

## Architecture Proposal stage

No Epic may start with implementation.

Every Epic must first begin with an Architecture Proposal.

Create `planning/proposals/ARCH-XXX-ShortName.md` following the
template in `planning/proposals/README.md`.

Architecture decisions must be validated before implementation begins.
This prevents unnecessary refactoring.

## Architecture rule

Every subsystem must be designed in this order:

```
Core → Resource → Manager → Storage → Provider → Daemon → REST API → Web UI → CLI → Tests
```

Business logic must never live inside CLI handlers or Web UI code.
CLI and Web UI are only presentation layers over the same service layer.

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

## Safety rules

Never run destructive commands without explicit confirmation.

Forbidden without confirmation:
- `rm -rf`, `docker system prune`, `docker volume rm`
- Deleting `/srv/containercp` or `/etc/containercp`
- Changing firewall rules, SSH config, or network config

## Development rules

- Every change must be committed to Git
- One task = one logical change
- Tests must be added with every Epic
- Zero compiler warnings required before commit
- All existing tests must pass before commit

## Product release lifecycle

Every major version follows this lifecycle:

1. Planning
2. Architecture Review
3. Implementation
4. Code Review
5. Stabilization
6. Integration Validation
7. Release Candidate (rc1, rc2, rc3...)
8. Product Release
9. Next Epic

Release Candidates must pass the product validation checklist
in `planning/product-validation.md` before shipping.

## Current product stage

ContainerCP is approaching Version 0.5 (Web Administration).
The project has evolved from a CLI utility to a hosting platform
with daemon architecture, REST API, Web UI, and growing provider
ecosystem.

The next major milestone is the First Production Validation on a
clean Debian 12 system.
