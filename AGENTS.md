# ContainerCP — Agent Rules

## Project identity

ContainerCP is a modern, open-source container-oriented hosting
control panel for system administrators, developers, and hosting providers.

**Current version:** v0.7.0

---

## Mandatory reading order

Before planning or implementing ANY Epic, read these IN ORDER:

1. `planning/PRODUCT_VISION.md` — product vision and principles
2. `planning/product-roadmap.md` — version milestones
3. `planning/product-validation.md` — acceptance checklist
4. `planning/backlog.md` — current priorities
5. Latest Sprint/Epic Reviews — recent progress and debt
6. `docs/ADR/` — architecture decisions

At session start: read `planning/project-status.md` for current task state.

---

## Core development rules

- **API first** — every operation must be available through the REST API
  before CLI or Web UI. CLI and UI are only clients of the API.
  Before any API work, read `docs/api/API_REFERENCE.md` (endpoint index)
  and `docs/development/api-rules.md` (design rules).
- **Architecture order**: `Core → Resource → Manager → Storage → Provider → Daemon → REST API → Web UI → CLI → Tests`
- **Business logic** must never live in CLI handlers or Web UI code.
- **Validation-driven** — VM validation is the primary quality gate
  (see `planning/TEST_ENVIRONMENT.md`).
- **Single Source of Truth** — every type of information has exactly one
  owner module (see `docs/development/single-source-of-truth.md`).
- **Architecture Proposal required** — no Epic starts without one
  (see `planning/proposals/README.md` for template).
- **One task = one logical commit**. Tests required with every Epic.
- **Zero compiler warnings** before commit. All existing tests must pass.
- **Web UI must follow backend changes** — never leave GUI behind.
- **Changelog required** — every task or bug fix adds an entry to `CHANGELOG.md`
  with date, commit hash, summary, files changed, user-visible behavior,
  validation result, and known risks.

---

## Safety rules

Never run destructive commands without explicit confirmation:

- `rm -rf`, `docker system prune`, `docker volume rm`
- Deleting `/srv/containercp` or `/etc/containercp`
- Changing firewall rules, SSH config, or network config

---

## Commit and push

- Every change committed to Git. Commit and push ALWAYS together.
- Push command: `git remote set-url origin "https://powern:$(cat /tmp/github_token)@github.com/powern/containercp.git" && git push origin main`

---

## Key documents

| Purpose | Document |
|---------|----------|
| Product vision | `planning/PRODUCT_VISION.md` |
| Roadmap | `planning/product-roadmap.md` |
| Validation checklist | `planning/product-validation.md` |
| Backlog | `planning/backlog.md` |
| Project status | `planning/project-status.md` |
| Architecture decisions | `docs/ADR/` |
| Runtime architecture | `docs/runtime-architecture.md` |
| Single Source of Truth | `docs/development/single-source-of-truth.md` |
| Development rules | `docs/development/coding-rules.md` |
| API reference | `docs/api/API_REFERENCE.md` |
| API design rules | `docs/development/api-rules.md` |
| Testing | `planning/TEST_ENVIRONMENT.md` |
| Changelog | `CHANGELOG.md` |

---

## Product release lifecycle

```
Planning → Architecture Review → Implementation → Code Review →
Stabilization → Integration Validation → Release Candidate →
Product Release → Next Epic
```

## Epic lifecycle

```
Architecture Proposal → Implementation → Unit Tests → Integration Tests →
Git Commit → Git Push → Deploy to Validation VM → Real Product Validation →
Architecture Review → Bug Fixes → Repeat until stable → Epic Closed
```
