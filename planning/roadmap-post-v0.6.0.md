# ContainerCP Product Roadmap — Post v0.6.0

> **Date:** 2026-07-16 (revised 2026-07-16)
> **Version:** v0.6.0
> **Status:** Analysis document for next phase planning
>
> **Critical self-review conducted:** The original ARCH-008 proposal
> ("Production Security & Access Layer") was reviewed against the
> codebase and found to be too broad. This document now records the
> revised, narrower epic sequence. See section 9 for the rationale.

---

## 1. Repository Analysis Summary

**Переглянуті документи:**
- `README.md`, `AGENTS.md`, `CHANGELOG.md`
- `planning/PRODUCT_VISION.md`, `planning/product-roadmap.md`, `planning/product-validation.md`, `planning/backlog.md`, `planning/project-status.md`, `planning/TEST_ENVIRONMENT.md`
- Усі 7 ADR (`docs/ADR/ADR-0001`–`ADR-007`)
- `docs/ARCHITECTURE.md`, `docs/runtime-architecture.md`, `docs/api/API_REFERENCE.md`
- `docs/development/single-source-of-truth.md`, `docs/development/coding-rules.md`, `docs/development/api-rules.md`
- `docs/release-notes-v0.6.0.md`, `docs/SFTP-PROVIDER.md`
- `planning/proposals/ARCH-007-DNS-GUI-Redesign.md`, `planning/proposals/README.md`
- Усі changelog-файли (`docs/changelog/`)
- Усі вихідні коди (`libs/`, `web/`, `app/`)

**Останні коміти:** 30 комітів у `git log --oneline -30`. Уся історія лінійна (без гілок). Останній коміт — `4eceaf7` (Release: ContainerCP v0.6.0). Попередні 29 комітів — фази 7–10 реалізації ARCH-007 (DNS Diagnostic Center).

**Поточна версія:** v0.6.0 (Stable).

**Статус ARCH-007:** COMPLETED. DNS Diagnostic Center — read-only сервіс діагностики DNS (c-ares, Health Score, DMARC Wizard, Evidence panels, Admin Panel site_id=0). 257 тестів, zero warnings.

**Архітектурні рішення:**
- Control Plane / Node separation (ADR-0001)
- HostingProvider / Runtime separation (ADR-0002)
- Let's Encrypt ACME HTTP-01 (ADR-003)
- Built-in REST API (ADR-004)
- Daemon architecture containercpd + containercp (ADR-005)
- Disk-based template profiles (ADR-006)
- M365 split delivery routing (ADR-007)

**Обмеження архітектури:**
- Pipe-delimited текстовий storage (не SQLite) — без атомарності, без індексів
- REST API без автентифікації (AllowAll middleware)
- Сесії in-memory (не зберігаються після перезапуску)
- Jobs in-memory (не персистентні)
- Відсутня пагінація
- Відсутнє rate limiting
- Відсутня система подій (event system)
- Відсутній audit log
- SFTP provider — заглушка
- Databases — тільки метадані (без реального створення БД)

---

## 2. Current Product State

### Повністю реалізовано (Production Ready)

| Модуль | Ступінь | Інтерфейси |
|--------|---------|------------|
| Core (Application, ServiceRegistry, Config, Resource) | 100% | Внутрішній |
| Storage (pipe-delimited .db files) | 95% | Внутрішній |
| CLI (thin client over UNIX socket) | 90% | CLI |
| Daemon (PID lock, startup recovery, dual-mode) | 95% | systemd |
| REST API (Router, JSON formatting, 50+ endpoints) | 90% | REST API |
| Web UI (SPA, 18 pages, all integrated) | 90% | Web UI |
| Sites (create/remove with rollback, start/stop/status) | 95% | CLI, API, Web UI |
| Docker/Runtime (ComposeGenerator, Executor, ServiceRole) | 90% | API, Web UI |
| Reverse Proxy (NginxProxyProvider, HTTPS config, transactional) | 90% | CLI, API, Web UI |
| SSL (ACME HTTP-01, CertificateStore, auto-renewal) | 90% | CLI, API, Web UI |
| Mail (Postfix+Dovecot+Redis+Rspamd, 4 modes, DKIM, Smarthost) | 85% | API, Web UI |
| DNS Diagnostics (c-ares, Health Score, SPF, DMARC Wizard) | 90% | API, Web UI |
| Profiles/Templates (disk-based, nginx/Apache, 5 profiles) | 90% | CLI, API, Web UI |
| Backup (tar+restore, CLI, API, Web UI) | 85% | CLI, API, Web UI |
| Migration (VestaCP multi-stage import) | 85% | API, CLI, Web UI |
| Auth (Web UI session auth) | 80% | Web UI |

### Частково реалізовано

| Модуль | Ступінь | Деталі |
|--------|---------|--------|
| Access/SFTP | 40% | `AccessUserManager`/`AccessGrantManager` — реальні CRUD. `LocalSftpProvider` — **заглушка** (всі методи no-op). Є детальний план у `docs/SFTP-PROVIDER.md` |
| Databases | 30% | `DatabaseManager` — тільки метадані в пам'яті. Немає реального створення MariaDB/MySQL, немає `CREATE DATABASE`, немає SQL-провайдера |
| Logs | 20% | `GET /api/logs` повертає hardcoded заглушку |
| Jobs | 60% | `JobExecutor` — реальний thread pool з task queue. `JobManager` — **in-memory only**, jobs не зберігаються |
| Nodes | 10% | `Node` — struct з id/name/type. Немає `NodeManager`, немає логіки управління нодами |

### Не реалізовано (0%)

- DNS Zone Management (authoritative)
- Monitoring/Observability
- Alerting/Notifications
- Scheduler/Cron
- Firewall/Security Management
- Plugin System/Marketplace
- Multi-node/Clustering
- High Availability
- Site Migration (to other servers)
- Application Catalog / One-click installers
- Resource Limits/Quotas
- API Tokens
- Audit Log
- Rate Limiting
- Pagination

### Модулі, що пройшли production validation

Жоден модуль не пройшов повну production validation на чистій Debian 13 VM згідно з `planning/product-validation.md` (148 items всі ще в статусі NOT TESTED). v0.6.0 пройшов 94 items у 5 стадіях (A–E) під час RC, але master validation checklist не оновлено. 24-hour stability test deferred.

### Модулі, що потребують стабілізації

- Mail module: SnappyMail image не для всіх архітектур; 24h stability test не завершено
- SSL: ACME DNS-01 (wildcard) не реалізовано
- PortManager: deprecated, не видалено

---

## 3. Existing Major Modules

| Модуль | Призначення | Готовність | CLI | API | Web UI | Runtime | Тести | Борги | Production Ready |
|--------|------------|-----------|-----|-----|--------|---------|-------|-------|-----------------|
| **Core** | Application, ServiceRegistry, Resource | 100% | - | - | - | Так | Ні | Немає | Так |
| **Config** | Конфігурація, шляхи, database_dir | 100% | - | GET/POST | Налаштування | Так | Ні | Немає | Так |
| **Storage** | Pipe-delimited `|` text files | 95% | - | - | - | Так | 5 | Немає атомарності, немає SQLite | Умовно |
| **Daemon** | containercpd, UNIX socket, PID | 95% | Так | - | - | Так | 5 | Немає | Так |
| **CLI** | containercp thin client | 90% | Так | - | - | - | Ні | Деякі команди відсутні | Так |
| **REST API** | HTTP сервер, Router, JSON | 90% | - | Так | - | Так | 33 | AllowAll auth, немає пагінації | Умовно |
| **Web UI** | SPA (app.js 3786 рядків) | 90% | - | - | Так | - | - | Стилі inline в JS | Так |
| **Sites** | SiteManager, CRUD + rollback | 95% | Так | Так | Так | Так | Через test_managers | Немає | Так |
| **Domains** | DomainManager, enriched JSON | 95% | Так | Так | Так | Так | Ні | Не можна створити домен окремо | Так |
| **Docker** | EnvGenerator, ComposeGenerator | 90% | - | - | - | Так | Ні | Немає | Так |
| **Runtime** | RuntimeActionExecutor, ServiceRole, Synchronizer, Health | 90% | - | Так | Так | Так | 25 | SiteRuntimeManager специфічний для Sites | Так |
| **Reverse Proxy** | NginxProxyProvider, HTTPS transactional | 90% | Так | Так | Так | Так | 6 | std::system() | Так |
| **SSL** | ACME v02, CertificateStore, auto-renewal | 90% | Так | Так | Так | Так | 30 | Немає DNS-01 | Так |
| **Mail** | Postfix+Dovecot+Redis+Rspamd, DKIM, Smarthost | 85% | - | Так | Так | Так | Через test_managers | SnappyMail image, 24h stability | Умовно |
| **DNS Diagnostics** | c-ares, Health Score, SPF, DMARC | 90% | - | Так | Так | Так | 69 | Read-only | Так |
| **Profiles** | ProfileManager, ProfileType | 90% | Так | Так | Так | - | 11 | Немає | Так |
| **Templates** | TemplateProfileManager | 85% | Так | Так | Так | - | Через test_template | Немає | Так |
| **Backup** | TarBackupProvider, create/restore/remove | 85% | Так | Так | Так | Так | 3 | Немає scheduling/rotation | Умовно |
| **Jobs** | JobExecutor thread pool, in-memory | 75% | - | Так | Так | Так | Ні | In-memory only | Умовно |
| **Migration** | VestaSiteImporter (3 stages) | 85% | Так | Так | Так | - | 37 | Тільки VestaCP | Так |
| **Auth** | AuthService, SHA-256, session tokens | 80% | - | Login/Logout | Так | - | Ні | In-memory sessions | Умовно |
| **Access/SFTP** | AccessUser/Grant managers | 40% | Так | Так | Так | - | 5 | **Provider — заглушка** | Ні |
| **Databases** | Database CRUD metadata | 30% | Так | Так | Так | - | Ні | **Немає реального створення БД** | Ні |
| **Nodes** | Node struct only | 10% | Так | Так | Так | - | Ні | **Немає NodeManager** | Ні |
| **Network** | Public IP detection | 90% | - | - | - | Так | Ні | Немає | Так |
| **Users** | User CRUD | 90% | Так | Так | - | - | Через test_managers | Немає | Так |
| **PHP** | PhpVersion | 90% | Так | Так | - | - | Ні | Немає | Так |
| **Utils** | Validator, PasswordGenerator, StringUtils | 95% | - | - | - | - | 2 | Немає | Так |

---

## 4. Functional Coverage of a Hosting Control Panel

| Functional Area | Implemented | Partial | Missing | Notes |
| --------------- | ----------- | ------- | ------- | ----- |
| **Site Management** | ✅ Create, remove, start, stop, status, runtime actions | — | — | Повний lifecycle з rollback |
| **Domain Management** | ✅ CRUD, enriched list, SSL status, mail status | — | — | Не можна створити окремий domain |
| **DNS Management (Authoritative)** | — | — | ❌ | DNS Diagnostics — read-only. Zone editor, DNS provider integration відсутні |
| **DNS Diagnostics** | ✅ c-ares, SPF, DMARC, Health Score | — | — | Read-only, але повноцінний |
| **Web Server** | ✅ nginx + Apache2 templates | — | — | 5 profile templates |
| **PHP Management** | ✅ Multiple PHP versions (8.2/8.3/8.4) | — | — | Per-site PHP version |
| **Database Management** | — | ✅ Metadata CRUD | ❌ Real DB creation | MariaDB створюється через Docker Compose, але немає окремого Database Manager |
| **Mail Server** | ✅ Postfix+Dovecot+Rspamd, 4 modes, DKIM | — | — | ExternalRelay і SplitM365 потребують зовнішнього MX |
| **Webmail** | — | ✅ SnappyMail container | — | Image не для всіх архітектур |
| **SSL/TLS Certificates** | ✅ Let's Encrypt ACME HTTP-01, auto-renewal | — | — | DNS-01 (wildcard) відсутній |
| **Reverse Proxy** | ✅ Central nginx, auto-config, HTTPS | — | — | Тільки nginx |
| **Backup & Restore** | ✅ tar+gzip, CLI, API, UI | — | ❌ Scheduling, rotation, off-site | Тільки ручний |
| **SFTP/File Access** | — | ✅ User/grant CRUD | ❌ Real SFTP provider | LocalSftpProvider — заглушка |
| **User Management** | ✅ Admin user, password change | — | — | Тільки admin |
| **Multi-admin / Roles** | — | — | ❌ | Тільки один admin |
| **API Authentication** | — | ✅ Web UI session auth | ❌ REST API AllowAll | Port 8080 без auth |
| **API Tokens** | — | — | ❌ | Для automation/Ansible/Terraform |
| **Audit Log** | — | — | ❌ | Немає логування дій |
| **Resource Monitoring** | — | — | ❌ | CPU, RAM, disk usage |
| **Log Viewer** | — | ✅ Page exists | ❌ Mock data | GET /api/logs повертає заглушку |
| **Alerting/Notifications** | — | — | ❌ | |
| **Backup Scheduling** | — | — | ❌ | |
| **Firewall Management** | — | — | ❌ | |
| **Multi-server/Cluster** | — | — | ❌ | Тільки local node |
| **High Availability** | — | — | ❌ | |
| **One-click Installers** | — | — | ❌ | WordPress створюється через шаблони |
| **Plugin/Extension System** | — | — | ❌ | |
| **Import/Migration Tools** | ✅ VestaCP | — | — | Тільки VestaCP |
| **Application Catalog** | — | — | ❌ | |
| **Scheduler/Cron** | — | — | ❌ | |
| **Service Management** | — | ✅ Runtime actions | — | restart-web/php/db/redis |
| **Resource Limits/Quotas** | — | — | ❌ | |
| **Update/Upgrade** | ✅ install.sh, update.sh | — | — | |
| **Security Hardening** | — | — | ❌ | Fail2ban, firewall, etc. |

---

## 5. Missing Major Product Modules

| Модуль | Проблема | Потрібен до 1.0 | Залежності | Існуючий фундамент |
|--------|----------|-----------------|------------|-------------------|
| **Authentication & Authorization** | API без автентифікації, немає ролей, немає API токенів | **Так** — security | AuthService вже існує | AuthService (SHA-256, session tokens), AuthUserManager, AuthMiddleware interface |
| **Real SFTP Provider** | LocalSftpProvider — заглушка, неможливо завантажити файли | **Так** — core workflow | AccessUser/Grant вже є | AccessProvider.h interface, docs/SFTP-PROVIDER.md з детальним планом |
| **Audit Log** | Немає логування дій адміністратора | **Так** — compliance | Auth, Storage | Logger module |
| **Storage → SQLite** | Pipe-delimited без атомарності, без індексів | **Так** — foundation | Немає | Storage.cpp (788 рядків) |
| **Job Persistence** | Jobs втрачаються при перезапуску daemon | **Рекомендовано** | Storage | JobManager, JobExecutor thread pool |
| **Pagination** | Великі datasets не підтримуються | **Рекомендовано** | Storage | Немає |
| **Backup Scheduling** | Відсутнє автоматичне створення backup | **Так** — operations | Jobs (persistence) | TarBackupProvider |
| **Resource Monitoring** | Немає CPU/RAM/disk usage | Ні — v2.0 scope | NetworkService, Runtime | NetworkService (IP detection) |
| **Log Management** | Mock-дані в /api/logs | **Рекомендовано** | Storage, Daemon | Logger module |
| **Alerting/Notifications** | Немає повідомлень про помилки | Ні | Mail, Monitoring | Mail module (can send) |
| **Authoritative DNS** | DNS zone management | Ні — deferred | Storage, Provider | Немає |
| **Multi-node** | Немає remote nodes | Ні — v0.8 scope | Node, Storage, Auth | Node struct, ADR-0001 |
| **API Tokens** | Немає automation API access | **Рекомендовано** | Auth | AuthService |
| **Rate Limiting** | API не захищене від DDoS | **Рекомендовано** | Auth | Немає |
| **Role-based Access Control** | Multi-admin неможливий | **Рекомендовано** | Auth, Audit | AuthUser має `role` поле |
| **Secrets Management** | Паролі в .env файлах | Ні | Config | Немає |

---

## 6. Technical Foundation and Blockers

### Storage Layer (pipe-delimited)

**Поточний стан:** `libs/storage/Storage.cpp` — 788 рядків. Pipe-delimited (`|`) текст. Кожен тип даних — окремий `.db` файл. Кожен `save()` перезаписує весь файл. Жодної атомарності, індексів, concurrent access.

**Блокує:**
- Job persistence (немає атомарного save)
- Pagination (немає індексів, неможливий offset/limit)
- Audit log (немає append-only storage)
- Backup scheduling metadata
- Будь-який багатокористувацький доступ

**Рекомендація:** Окремий епік або частина ARCH-008.

### Authentication (AllowAll middleware)

**Поточний стан:** `libs/api/AuthMiddleware.cpp` — `return true;`. REST API на порту 8080 не має автентифікації. Web UI на 8081 має session auth через `WebServer::require_session()`.

**Блокує:**
- Production deployment (будь-хто з доступом до порту 8080 може керувати всім)
- API Tokens
- Role-based access
- Audit log (хто зробив дію?)
- Multi-admin

**Рекомендація:** Ключова частина ARCH-008.

### Job Persistence

**Поточний стан:** `JobManager::jobs_` — `std::vector<Job>`. Жодної серіалізації. При SIGTERM jobs губляться. Jobs з `status=running` при перезапуску daemon не відновлюються.

**Блокує:**
- Backup scheduling (періодичні jobs мають виживати після restart)
- SSL renewal (якщо daemon перезапустився під час renew)
- Будь-яка автоматизація

**Рекомендація:** Залежить від SQLite migration. Може бути частиною Storage Foundation епіку.

### Session Persistence

**Поточний стан:** `AuthService::sessions_` — `std::unordered_map<std::string, Session>`. Після перезапуску daemon всі сесії губляться.

**Блокує:**
- Production UX (admin має логінитись після кожного restart)

**Рекомендація:** Частина ARCH-008 (Security & Auth).

### Pagination

**Поточний стан:** Всі list endpoints повертають повний масив. При 100+ сайтах — проблема.

**Блокує:**
- Scalability
- Production deployment з реальними даними

**Рекомендація:** Залежить від SQLite migration.

### Event System

**Поточний стан:** Відсутній. `RuntimeSynchronizer` — callback-based, але не event-driven.

**Блокує:**
- Audit logging (події мають генеруватись централізовано)
- Plugin system
- Webhooks

**Рекомендація:** Може бути частиною ARCH-008 (Audit потребує подій).

### Multi-node Abstractions

**Поточний стан:** `node_id` на кожному Resource згідно ADR-0001. `libs/node/Node.h` — struct. Немає `NodeManager`.

**Блокує:**
- Multi-node/clustering (v0.8)

**Рекомендація:** Не для ARCH-008. Відкладено.

---

## 7. Revised Epic Roadmap

The original ARCH-008 proposal (sections 9–10 of the previous version)
was reviewed critically against the actual codebase. The following
revised sequence reflects the findings of that review.

---

### ARCH-008: REST API Authentication

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.7.0 |
| **Scope** | Medium |
| **Product Value** | High |

#### What

Replace the `AllowAllAuth` middleware with real token-based authentication,
persist sessions to disk, and add API tokens for automation.

#### Scope (narrow — 3 workstreams only)

1. **AuthMiddleware** — real implementation replacing `AllowAllAuth`. Token
   validation on every REST API request. Session tokens from Web UI login.
   API tokens from CLI/automation clients.

2. **Session Persistence** — save active sessions to disk (using existing
   pipe-delimited storage — sufficient for single-admin). Restore on daemon
   restart. Eliminates re-login after restart.

3. **API Tokens** — long-lived tokens for automation (Ansible, Terraform,
   CI/CD). Token hash stored in Storage. CLI commands for
   `token create` / `token revoke` / `token list`.

#### Explicitly excluded from ARCH-008

| Feature | Reason for exclusion |
|---------|---------------------|
| Role-based access control (RBAC) | Only one admin user exists. Multi-admin is v2.0 scope per Product Vision. |
| Real SFTP Provider | Separate concern (system users, chroot, SSH). No architectural dependency on auth. |
| Audit Log | Depends on identity (auth) + scalable storage (SQLite). Deferred to ARCH-011. |
| Event System | Scope creep for this epic. Basic audit can log directly in handlers without events. |

#### Why ARCH-008 first

1. **Security principle.** AllowAll on localhost:8080 is not a critical
   external threat (port is 127.0.0.1-only), but fixing it is the right
   foundation. Every subsequent epic benefits from having real identity.

2. **Session persistence.** Eliminates "login after every daemon restart" —
   the most visible UX complaint.

3. **API tokens.** Enables automation and integration before the platform
   gains more features. Token auth is the prerequisite for any scripted
   or tool-based management.

4. **Smallest scope, fastest delivery.** Auth alone (no SFTP, no RBAC,
   no audit) is ~2–3 weeks.

#### Dependencies

- `AuthService` (exists, 248 lines) — session auth, password hashing
- `AuthMiddleware` interface (exists) — abstract class for auth plugins
- `WebServer::require_session()` (exists) — Web UI session validation
- `Storage` (exists) — pipe-delimited is sufficient for auth tokens
- `web/app.js` (exists) — login page, session management

#### Risks

- **Backward compatibility.** Existing scripts calling `curl localhost:8080`
  without auth headers will break. Mitigation: document migration, support
  a grace period where both AllowAll and token auth are accepted.
- **Token security.** Token hash, rotation, and revocation must be correct.
- **Scope creep.** Resist pressure to add RBAC, SFTP, or audit to this epic.

#### Acceptance Direction

- Deterministic tests for auth middleware (valid token, expired, missing)
- API integration tests (login, token create/revoke, authenticated requests)
- Browser validation (login → session persists across restart)
- CLI validation (token create → use token → curl API endpoints)
- Web UI regression (existing pages still load after auth is enabled)

---

### ARCH-009: Storage Foundation

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.7.0 (immediately after ARCH-008) |
| **Scope** | Medium-Large |
| **Product Value** | High (technical) |

#### What

Replace pipe-delimited text storage with SQLite, persist jobs, add pagination
across all list endpoints.

#### Scope

1. **SQLite migration** — replace `libs/storage/Storage.cpp` (788 lines of
   pipe-delimited text) with SQLite backed storage. Each resource type keeps
   its own table. Provides: atomic writes, concurrent access, indices,
   transactions.

2. **Job Persistence** — jobs survive daemon restart. Uses SQLite for
   durable job queue. Enables reliable SSL auto-renewal (resume interrupted
   renewal after restart) and future backup scheduling.

3. **Pagination** — `?offset=N&limit=M` parameters for every list endpoint.
   Requires SQLite for efficient LIMIT/OFFSET queries.

#### Explicitly excluded

- Rate limiting — deferred to post-v1.0 (not blocking any production workflow)

#### Why ARCH-008 before ARCH-009

- Auth does not need SQLite. Pipe-delimited storage is sufficient for
  storing session tokens and API tokens (small datasets, single admin).
- ARCH-008 builds foundational identity that ARCH-009 can use for
  operation attribution (e.g., "who scheduled this job").
- If storage migration hits issues, auth is already deployed and working.

#### Why ARCH-009 follows immediately

- Pipe-delimited storage is the single biggest technical debt in the
  codebase. Every module uses it. It has no atomicity, no indices, no
  concurrent access.
- Blocks: job persistence (needed for SSL renewal reliability),
  pagination (needed before v1.0 with real data), scalable audit log.
- Without SQLite, ARCH-011 (Audit Log) would be O(n) per write.

#### Dependencies

- `Storage` (exists, 788 lines) — to be replaced, not extended
- `JobManager` (exists, in-memory) — to be backed by SQLite
- All list API endpoints — to be extended with pagination params

#### Risks

- **Migration.** Existing `.db` files must be converted to SQLite on
  daemon startup. Conversion script needed. Rollback plan required.
- **Testing.** Every existing test that mocks Storage may need updates.
- **Performance.** SQLite is fast for single-writer, but concurrent reads
  must be verified under load.

#### Size: Medium-Large (3–4 weeks)

---

### ARCH-010: Real SFTP Provider

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.7.0 or v0.8.0 |
| **Scope** | Medium |
| **Product Value** | High |

#### What

Replace the `LocalSftpProvider` no-op with a real SFTP implementation
using system users, chroot jails, and SSH key authentication.

#### Scope

1. **System user creation** — `useradd` / `userdel` for each access user,
   matching the site's directory ownership.
2. **chroot jail** — restricted shell, locked to the site's directory.
3. **SSH key management** — store public keys, generate `authorized_keys`.
4. **User lifecycle** — create on site creation, remove on site removal.

#### Why not part of ARCH-008

- System administration (useradd, chroot, SSH config) is a completely
  different concern from HTTP auth middleware (tokens, sessions).
- No architectural dependency on auth: SFTP users are separate OS-level
  accounts, not API tokens.
- Can be developed independently and in parallel with ARCH-009.

#### Dependencies

- `AccessUserManager` (exists) — user CRUD
- `AccessGrantManager` (exists) — site-to-user mapping
- `AccessProvider` interface (exists) — abstract provider
- `docs/SFTP-PROVIDER.md` (exists) — detailed implementation plan

#### Risks

- **System user conflicts** when sites are renamed or removed.
- **chroot complexity** — SSH chroot requires specific directory structure
  and binaries inside the jail.
- **Cleanup** — removing a site must also remove the system user.

#### Size: Medium (2–3 weeks)

---

### ARCH-011: Audit Log and Event System

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.8.0 |
| **Scope** | Medium |
| **Product Value** | High (compliance) |

#### What

Centralized audit logging of all mutating operations with user identity,
timestamp, action, and detail. Basic event system for pre/post hooks.

#### Scope

1. **Audit Log** — log every mutating API operation: who, what, when,
   details. Stored in SQLite (depends on ARCH-009).
2. **Audit API** — `GET /api/audit` with filtering by user, action, date.
3. **Audit Web UI** — audit log viewer page.
4. **Event System** — lightweight pre/post hooks for audit and future
   extensions (webhooks, plugin system).

#### Dependencies

- ARCH-008 (user identity for "who" field in audit records)
- ARCH-009 (SQLite for scalable append-only storage)

#### Why not in ARCH-008

- Audit log without SQLite would be O(n) per write (pipe-delimited
  rewrite entire file). ARCH-009 must come first.
- Event system is an architectural decision that deserves its own design.
  Adding it to the auth epic would cause scope creep.

#### Size: Medium (2–3 weeks)

---

### ARCH-012: Backup Scheduling and Disaster Recovery

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.8.0 or v0.9.0 |
| **Scope** | Medium |
| **Product Value** | High |

#### What

Automated backup creation on a schedule, retention policies, rotation.

#### Scope

1. **Backup scheduling** — periodic backup creation via `JobExecutor`.
2. **Retention policies** — daily/weekly/monthly with configurable counts.
3. **Backup rotation** — automatic removal of expired backups.
4. **Backup encryption** — optional encryption (deferred stretch goal).

#### Dependencies

- ARCH-009 (job persistence — schedules must survive daemon restart)

#### Size: Medium (2 weeks)

---

### ARCH-013: Monitoring and Observability

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.9.0 |
| **Scope** | Medium |
| **Note** | Per Product Vision: "No Prometheus/Grafana replacement." Lightweight built-in system. |

#### What

System metrics, site-level resource usage, real log viewer (replace mocks),
health check dashboard.

#### Scope

1. **System metrics** — CPU, RAM, disk, network per-server.
2. **Site metrics** — per-site container resource usage via Docker stats.
3. **Real log viewer** — replace mock data in `GET /api/logs`.
4. **Health dashboard** — consolidated view of all health checks.

#### Dependencies

- ARCH-009 (pagination for log viewer)

#### Size: Medium (2–3 weeks)

---

### ARCH-014: Authoritative DNS Zone Management

| Aspect | Value |
|--------|-------|
| **Target Version** | v1.0+ (deferred) |
| **Scope** | Medium |
| **Product Value** | Medium |

#### What

DNS zone CRUD, DNS provider interface, DNS record management, DNSSEC.

#### Why deferred

- Read-only DNS diagnostics already exist (ARCH-007).
- Users can use Cloudflare, Route53, DigitalOcean for production DNS.
- Authoritative DNS is a separate product (DNS server), not a core
  hosting control panel requirement.

#### Dependencies

- Storage, Provider pattern (both exist)

#### Size: Medium

---

### Post v1.0 Epics

- Role-based Access Control (multi-admin, reseller hosting)
- Multi-node / Cluster Management
- High Availability
- Plugin System / Marketplace
- Application Catalog (one-click installers)
- Commercial Edition features

---

## 8. Prioritization Matrix

| Epic | Product Value | Technical Value | Complexity | Risk | Dependencies | Recommended Order |
| ---- | ------------- | --------------- | ---------- | ---- | ------------ | ----------------- |
| **ARCH-008: REST API Auth** | **High** | **High** | Medium | Low | AuthService exists | **1** |
| ARCH-009: Storage Foundation | High | **Critical** | Medium-Large | **High** (migration) | — | **2** |
| ARCH-010: Real SFTP Provider | **High** | Medium | Medium | Medium | AccessUser exists | **3 (parallel with 2)** |
| ARCH-011: Audit Log + Events | High | Medium | Medium | Low | ARCH-008 + ARCH-009 | 4 |
| ARCH-012: Backup Scheduling | High | Medium | Medium | Low | ARCH-009 | 5 |
| ARCH-013: Monitoring | Medium | Medium | Medium | Low | ARCH-009 | 6 |
| ARCH-014: Authoritative DNS | Medium | Low | Medium | Low | — | 7 (deferred) |
| RBAC / Multi-admin | Medium | Medium | Large | Medium | ARCH-008 | Post-1.0 |
| Multi-node | Medium | High | Extra Large | Very High | ARCH-008, 009 | Post-1.0 |
| Plugin System | Low | Medium | Extra Large | High | ARCH-008, 011 | Post-1.0 |

---

## 9. Critical Self-Review and Revised Recommendation

### Background

The original ARCH-008 proposal ("Production Security & Access Layer")
was a Large-scope epic bundling 7 workstreams: auth middleware, session
persistence, API tokens, RBAC, SFTP provider, audit log, and event
system. A critical self-review was conducted against the actual codebase
to challenge this scope.

### Key findings from the review

#### 1. AllowAll security gap is overblown

The proposal claimed "anyone with network access to port 8080 can control
everything." In reality:

- `ApiServer` binds to `127.0.0.1` (localhost only) — `ApiServer.cpp:263`
- The external Web UI port (8081) already has session auth via
  `WebServer::require_session()`
- CLI access goes through a UNIX socket with file permission protection

Someone with SSH access can already run `containercp` commands directly.
External attackers cannot reach port 8080 at all.

**Severity: Medium, not Critical.**

#### 2. The original ARCH-008 scope was too large

Seven workstreams bundled as one epic is an anti-pattern:

| Workstream | Domain | Can be separate? |
|-----------|--------|-----------------|
| AuthMiddleware | HTTP middleware | Core of ARCH-008 |
| Session Persistence | Auth infra | Part of ARCH-008 |
| API Tokens | Auth infra | Part of ARCH-008 |
| RBAC | Authorization | Premature (1 admin user exists) |
| Real SFTP | System administration | Independent (useradd, chroot, SSH) |
| Audit Log | Compliance | Depends on ARCH-008 (identity) + ARCH-009 (SQLite) |
| Event System | Architecture | Scope creep for auth epic |

#### 3. RBAC is premature

There is exactly one admin user. The Product Vision explicitly lists
multi-admin as a v2.0 concern. Implementing roles before the platform
has multiple user types is speculative engineering.

#### 4. Storage Foundation is more critical than acknowledged

Pipe-delimited storage (`libs/storage/Storage.cpp`, 788 lines) blocks:
- Job persistence (no atomic save → SSL renewal breaks on restart)
- Pagination (no indices → O(n) on every list)
- Audit log (O(n) per write — rewrite entire file on every mutation)
- Concurrent access (not thread-safe during write)

ARCH-009 (Storage Foundation) should follow ARCH-008 immediately, not
be deferred to a later version.

#### 5. SFTP and auth are independent concerns

HTTP auth middleware (tokens, sessions) has zero architectural overlap
with SFTP (system users, chroot, SSH keys). Combining them means SFTP
delays auth and vice versa.

### Revised recommendation

**ARCH-008 should be "REST API Authentication" — a Medium epic with
3 focused workstreams:**

1. AuthMiddleware (replace AllowAll)
2. Session Persistence
3. API Tokens

**Explicitly excluded from ARCH-008:**
- RBAC (deferred to post-v1.0 multi-admin)
- Real SFTP Provider (separate epic — ARCH-010)
- Audit Log (separate epic — ARCH-011, needs SQLite first)
- Event System (separate epic — ARCH-011)

### Revised epic sequence

```
ARCH-008: REST API Authentication     ← v0.7.0, Medium
    ↓ (foundation for identity)
ARCH-009: Storage Foundation           ← v0.7.0, Medium-Large
    ↓ (foundation for scalability)
┌─── ARCH-010: Real SFTP Provider     ← v0.7.0–v0.8.0, Medium
│   (independent of ARCH-009)
├─── ARCH-011: Audit Log + Events     ← v0.8.0, Medium
│   (depends on ARCH-008 + ARCH-009)
├─── ARCH-012: Backup Scheduling      ← v0.8.0–v0.9.0, Medium
│   (depends on ARCH-009)
├─── ARCH-013: Monitoring             ← v0.9.0, Medium
│   (depends on ARCH-009)
└─── ARCH-014: Authoritative DNS      ← v1.0+, Medium
    (deferred — users can use Cloudflare/Route53)
```

### Why ARCH-008 should be first (revised justification)

1. **Security principle, not critical urgency.** AllowAll is Medium
   severity, but fixing it is the right thing to do before adding more
   features to the platform. Every new endpoint becomes authenticated
   from day one.

2. **Session persistence.** The most visible UX issue: "login after every
   daemon restart." Fixing this is a quick win.

3. **API tokens.** Enables automation (Ansible, Terraform, CI/CD) before
   the platform accumulates more features that would need retrofitting.

4. **Smallest scope, fastest delivery.** By excluding RBAC, SFTP, audit,
   and events, ARCH-008 becomes a focused 2–3 week epic instead of a
   2+ month megaproject.

5. **Foundation for subsequent epics.** ARCH-009 (Storage Foundation)
   needs identity to attribute jobs to users. ARCH-011 (Audit Log) needs
   identity for the "who" field. ARCH-010 (SFTP) is independent and can
   proceed in parallel.

### Why ARCH-009 follows immediately after ARCH-008

- Auth does not need SQLite. Pipe-delimited storage is adequate for
  session and API token storage (small datasets, single admin).
- But ARCH-009 is the #1 technical debt. Every module uses storage.
- ARCH-009 unblocks: job persistence (SSL renewal reliability),
  pagination (needed before production deployment with real data),
  and scalable audit logging.
- Performance: pipe-delimited audit log would be O(n) per write — 
  unacceptable for any production hosting provider.

### What ARCH-008 does NOT solve

- **SFTP.** The no-op `LocalSftpProvider` remains a placeholder. Users
  still cannot upload files. This is a real core workflow gap and is
  tracked as ARCH-010.
- **Audit.** No compliance logging until ARCH-011.
- **Scalability.** No pagination, no job persistence — these are ARCH-009.

### Risks of the revised plan

- **Perception.** "You shipped v0.6.0 without auth" — the gap will remain
  until ARCH-008 is implemented.
- **Backward compatibility.** Existing scripts using `curl localhost:8080`
  without tokens will break. Grace period required.
- **SFTP delay.** The core workflow gap ("create site → upload files")
  remains until ARCH-010. Mitigation: document workarounds (docker cp,
  volume mount).

---

## 10. Proposed Planning Sequence

### Phase 0: Critical review completed ✅

The original ARCH-008 scope has been reviewed, challenged, and split.
This document records the revised sequence.

### Phase 1: ARCH-008 — REST API Authentication

1. **Architecture Proposal** — `planning/proposals/ARCH-008-REST-API-Authentication.md`
   - Problem: AllowAllAuth on localhost:8080
   - Proposed: Token-based AuthMiddleware, session persistence, API tokens
   - Explicit out-of-scope: RBAC, SFTP, Audit, Events
   - Migration strategy: grace period allowing both AllowAll and token auth
   - Rejected alternatives, risks, validation plan

2. **ADR** (if needed):
   - Token storage strategy (hash + salt vs encrypted)
   - Token revocation model (blacklist vs short expiry + refresh)

3. **Implementation phases**:
   - Phase 1a: AuthMiddleware real implementation
   - Phase 1b: Session persistence (disk-backed sessions)
   - Phase 1c: API tokens (create, revoke, list, authenticate)
   - Phase 1d: Web UI adaptations (login flow, token management page)
   - Phase 1e: CLI commands (token create, revoke, list)
   - Phase 1f: Tests (unit, integration, security)
   - Phase 1g: Validation VM deployment + acceptance testing

4. **Release target** — v0.7.0-alpha

### Phase 2: ARCH-009 — Storage Foundation

1. **Architecture Proposal** — `planning/proposals/ARCH-009-Storage-Foundation.md`
   - SQLite schema design for all existing resource types
   - Migration script for existing .db files
   - Job persistence design (SQLite-backed job queue)
   - Pagination API contract

2. **Implementation phases**:
   - Phase 2a: SQLite storage backend (parallel to existing pipe-delimited)
   - Phase 2b: Data migration on daemon startup
   - Phase 2c: Job persistence (JobManager → SQLite)
   - Phase 2d: Pagination for all list endpoints
   - Phase 2e: Remove pipe-delimited storage
   - Phase 2f: Tests + validation

3. **Release target** — v0.7.0-beta (may ship in same version as ARCH-008)

### Phase 3: ARCH-010 — Real SFTP Provider

1. **Architecture Proposal** — `planning/proposals/ARCH-010-Real-SFTP-Provider.md`
   - System user lifecycle (useradd/userdel)
   - chroot directory structure
   - SSH key management
   - Integration with existing AccessUser/AccessGrant

2. **Note:** Can start in parallel with ARCH-009 — no storage dependency.

### Phase 4: ARCH-011 through ARCH-014

Each epic follows the same pattern:
1. Architecture Proposal
2. Implementation
3. Tests
4. Validation

### Summary timeline

```
v0.7.0 ──────────────────────────────────────────────
  ARCH-008  ████████████████░░░░  (auth)
  ARCH-009  ░░░░████████████████  (storage)
  ARCH-010  ░░░░████████████░░░░  (sftp, parallel)
                                                   
v0.8.0 ──────────────────────────────────────────────
  ARCH-011  ████████████████░░░░  (audit)
  ARCH-012  ░░░░████████████████  (backup sched)
  ARCH-013  ░░░░░░░░░░░░░░░░░░░░  (monitoring)
                                                   
v1.0+ ──────────────────────────────────────────────
  ARCH-014  ████████████████████  (dns zone mgmt)
  RBAC      ░░░░░░░░░░░░░░░░░░░░  (multi-admin)
  Multi-node░░░░░░░░░░░░░░░░░░░░  (cluster)
```

### Key dependency graph

```
ARCH-008 (auth) ──────┐
                       ├──> ARCH-011 (audit) ──> Post-v1.0
ARCH-009 (storage) ────┘
    │
    ├──> ARCH-012 (backup scheduling)
    │
    └──> ARCH-013 (monitoring)

ARCH-010 (sftp) ──> independent, no dependencies on ARCH-008 or ARCH-009
```

---

*Документ підготовлено на основі аналізу репозиторію ContainerCP (коміт `4eceaf7`, v0.6.0).*
*Section 7–10 revised 2026-07-16 after critical self-review of the original ARCH-008 scope.*
