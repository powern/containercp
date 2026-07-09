# Changelog

All notable changes to ContainerCP are documented here.

Format: date | commit | summary

---

## 2025-07-09 | Phase 1–5: Runtime management

- Runtime subsystem with `RuntimeActionExecutor`, `ServiceRole`, `CommandExecutor`
- Site Details page redesigned as Website Management Center with Runtime card
- Runtime card shows Frontend/PHP/Database/Redis status + restart actions
- Runtime architecture refactoring: `ServiceRole` abstraction, `ContainerStatus` moved to executor
- Phase 3 fix: restart-all semantics, restart-db/redis actions added
- Phase 5 cleanup: moved container status inspection into `RuntimeActionExecutor`
- Phase 4: Sites UI restart actions (⚡ dropdown), Phase 5: moved to Site Details

See `docs/changelog/runtime-phases.md` for detailed entries with commit hashes,
file changes, validation results, and known risks.

---

## 2025-07-08 | SSL & HTTPS subsystem

- ACME HTTP-01 challenge via Web UI (staging + production)
- Bootstrap simplified (removed SSL step)
- Admin Panel HTTPS on port 443
- Let's Encrypt integration with auto-renewal
- `CertificateStore`, `RenewalScheduler`, `PemCertificateProvider`
- HTTPS status display in Sites Runtime card (consumes `CertificateStore`)

See `docs/changelog/ssl-subsystem.md` for detailed per-commit entries.

---

## 2025-07-08 | RC2 — Stability & Production Foundation

- Daemon architecture, REST API hardening
- Deployment scripts, update mechanism
- Port management refactoring
- Bug fixes for login, site removal, and rollback

See `docs/changelog/rc2-stability.md` for detailed entries.

---

## 2025-07-08 and earlier | Earlier development

- Multi-site Docker networking (ARCH-004)
- Web UI v0.5, PHP hosting, profiles
- CLI tooling, template engine
- Sprint reviews and infrastructure setup

See `docs/changelog/early-development.md` for detailed entries.

---

### Risks (current)

- Existing sites with host-port allocation retain old compose template
- Fresh site creation uses new template (always overwritten on disk)
- Deprecated PortManager not yet removed — cleanup planned
- `RuntimeActionExecutor` requires Docker Compose v2+
- Async jobs cancelled/marked failed if daemon shuts down during execution
