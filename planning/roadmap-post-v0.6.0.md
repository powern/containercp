# ContainerCP Product Roadmap — Post v0.6.0

> **Date:** 2026-07-16 (revised 2026-07-16)
> **Version:** v0.6.0
> **Status:** Analysis document for next phase planning
>
> **Product-owner decision (2026-07-16):** REST API Authentication
> is deferred. SQLite Storage Foundation is ARCH-008. See sections 7–9
> for the rationale and revised epic sequence.

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
- SSL auto-renewal reliability (jobs губляться при restart daemon)

**Рекомендація:** ARCH-008 — перший епік після v0.6.0.

### Authentication (AllowAll middleware)

**Поточний стан:** `libs/api/AuthMiddleware.cpp` — `return true;`. REST API на порту 8080 не має автентифікації. Web UI на 8081 має session auth через `WebServer::require_session()`. API server binds to `127.0.0.1` (localhost only).

**Блокує:**
- API Tokens
- Audit log attribution
- Multi-admin
- Remote API access (multi-node, external automation)

**Рекомендація:** Deferred. Не блокує поточний single-node, localhost-only deployment. Буде реалізовано, коли з'явиться потреба в remote API access, multi-node, або multi-admin.

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

## 7. Revised Epic Roadmap — Product-Owner Decision

The following epic order reflects the product-owner's final decision
after the critical self-review. REST API Authentication is deferred;
SQLite Storage Foundation is ARCH-008.

---

### ARCH-008: SQLite Storage Foundation

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.7.0 |
| **Scope** | Medium-Large |
| **Product Value** | Critical (technical foundation) |

#### What

Replace the current pipe-delimited text database files with SQLite,
preserving the existing manager and storage abstractions where possible.
Provide atomic writes, transactions, and a schema migration mechanism.
Migrate all existing resource data safely from v0.6.0 text databases.
Support rollback and recovery. Prepare the storage foundation for
persistent jobs, scheduling, audit logs, pagination, metrics history,
and future multi-node functionality.

#### Scope

1. **SQLite storage backend** — replace `libs/storage/Storage.cpp`
   (788 lines of pipe-delimited text I/O) with SQLite. Each resource
   type gets its own table but the `Storage` public API (save, load,
   load_all, remove) remains unchanged so existing managers work
   without modification.

2. **Atomic writes and transactions** — eliminate the "rewrite entire
   file on every save" pattern. Use SQLite transactions for atomic
   multi-table updates. Enable concurrent reads without corruption.

3. **Schema migration mechanism** — versioned schema in SQLite.
   On daemon startup, auto-detect schema version and apply migrations
   sequentially. Rollback support (snapshot-based or version downgrade).

4. **Data migration** — on first startup after upgrade, read existing
   `.db` pipe-delimited files, convert each record to SQLite rows,
   and verify integrity. Preserve original `.db` files as backup for
   rollback. Log migration success/failure per resource type.

5. **Indices** — add indices on commonly queried fields: `id` (primary),
   `name`, `site_id` (foreign key), `node_id` (future multi-node).
   Enable efficient pagination queries (LIMIT/OFFSET).

#### Why ARCH-008 is first

1. **Biggest technical debt.** Pipe-delimited storage is 788 lines of
   hand-rolled text I/O with no atomicity, no indices, no concurrent
   access. Every module depends on it.

2. **Blocks everything downstream.** Without reliable storage:
   - Job persistence is impossible (SSL renewal lost on restart)
   - Audit log is O(n) per write (rewrite entire file)
   - Pagination is O(n) per query (scan entire file)
   - Backup scheduling has no durable metadata store
   - Multi-node synchronization has no transactional foundation

3. **Lowest risk, highest leverage.** The Storage API is simple
   (save/load/load_all/remove). Wrapping SQLite behind the same
   interface means no changes to managers or API handlers. The
   migration is contained in one module.

4. **Prerequisite for v0.7+ epics.** ARCH-010 (Backup Scheduling)
   needs job persistence → needs SQLite. ARCH-012 (Monitoring)
   needs pagination → needs SQLite. ARCH-011 (Audit Log) needs
   append-only storage → needs SQLite.

#### Expected direction

- Replace the `Storage` implementation, not its interface
- Managers call `storage.save(resource)` — unchanged
- Internally: SQLite `INSERT OR REPLACE` instead of file rewrite
- `load_all()` uses `SELECT * FROM <type>` with optional pagination
- Schema version stored in a `_meta` table
- Migration: `_schema_version` → apply pending migrations → verify
- Backup original `.db` files before migration for rollback
- If migration fails: log error, keep original files, daemon may
  start in degraded mode or abort

#### Dependencies

- `libs/storage/Storage.h` — interface to preserve
- `libs/storage/Storage.cpp` — implementation to replace
- SQLite3 — new dependency (C++ API, not CLI)
- CMakeLists.txt — add SQLite3 find_package

#### Risks

- **Migration failure.** If a `.db` file is corrupted, migration
  may fail. Mitigation: validate each record before insert, keep
  original files, support manual recovery.
- **Performance regression.** SQLite has overhead per query.
  Mitigation: benchmark load_all() vs current implementation,
  use prepared statements, verify with production-scale data.
- **SQLite version compatibility.** Must work on Debian 13 (Trixie)
  with the system SQLite3 package.
- **Thread safety.** SQLite in WAL mode supports concurrent reads
  but single writer. Mitigation: use a database connection pool or
  a single connection with WAL mode.

#### Size: Medium-Large (3–4 weeks)

---

### ARCH-009: Real SFTP Provider

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.7.0 |
| **Scope** | Medium |
| **Product Value** | High |

#### What

Replace the `LocalSftpProvider` no-op with a real SFTP implementation
using system users, chroot jails, and SSH key authentication.

#### Scope

1. **System user creation** — `useradd` / `userdel` for each access
   user, matching the site's directory ownership.

2. **chroot jail** — restricted shell, locked to the site's directory
   with minimal required binaries.

3. **SSH key management** — store public keys, generate `authorized_keys`,
   support add/remove/list via CLI and API.

4. **User lifecycle** — create on site creation, remove on site removal.
   Integration with `AccessUserManager` and `AccessGrantManager`.

#### Why ARCH-009 after ARCH-008

- No architectural dependency on SQLite. SFTP uses system calls
  (useradd, chroot, SSH), not storage.
- Can proceed independently or in parallel with ARCH-008.
- Real SFTP fixes the most visible core workflow gap: "create site →
  cannot upload files."

#### Dependencies

- `AccessUserManager` (exists) — user CRUD
- `AccessGrantManager` (exists) — site-to-user mapping
- `AccessProvider` interface (exists) — abstract provider
- `docs/SFTP-PROVIDER.md` (exists) — detailed implementation plan

#### Risks

- **System user conflicts** when sites are renamed or removed.
- **chroot complexity** — SSH chroot requires specific directory
  structure and binaries inside the jail.
- **Cleanup** — removing a site must also remove the system user.
- **Race conditions** if multiple sites created concurrently.

#### Size: Medium (2–3 weeks)

---

### ARCH-010: Backup Scheduling and Persistent Jobs

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.8.0 |
| **Scope** | Medium |
| **Product Value** | High |

#### What

Leverage the new SQLite storage to persist jobs across daemon restarts.
Add scheduled backup creation with retention policies and rotation.

#### Scope

1. **Job persistence** — `JobManager` stores jobs in SQLite. Jobs with
   `status=running` are recovered on daemon restart. Enables reliable
   SSL auto-renewal (interrupted renewal resumes after restart).

2. **Backup scheduling** — periodic backup creation via `JobExecutor`
   with configurable cron-like schedules (daily, weekly, monthly).

3. **Retention policies** — configurable keep counts per schedule tier.
   Automatic cleanup of expired backups.

4. **Backup rotation** — remove oldest backups when retention limit is
   exceeded, before creating new ones.

#### Dependencies

- ARCH-008 (SQLite for job persistence)

#### Why ARCH-010 after ARCH-008

- Job persistence requires atomic storage (needs SQLite).
- Backup scheduling requires job persistence (schedules must survive
  daemon restart).

#### Size: Medium (2–3 weeks)

---

### ARCH-011: Audit Log and Event System

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.8.0 |
| **Scope** | Medium |
| **Product Value** | High (compliance) |

#### What

Centralized audit logging of all mutating operations. Basic event
system for pre/post hooks.

#### Scope

1. **Audit log storage** — append-only log table in SQLite. Each
   mutation records: timestamp, user agent (CLI/API/Web UI), action,
   resource type, resource id, before/after state (JSON diff).

2. **Audit API** — `GET /api/audit` with filtering by user, action,
   resource type, date range. Paginated (depends on ARCH-008).

3. **Audit Web UI** — audit log viewer page with search and filters.

4. **Event system** — lightweight pre/post hooks for audit
   registration. Designed for future extension to webhooks, plugin
   system, and notification channels.

#### Dependencies

- ARCH-008 (SQLite for scalable append-only audit storage)
- Identity attribution (user agent from request context) — uses
  existing session auth, no new auth middleware needed

#### Why ARCH-011 after ARCH-008

- Audit log without SQLite would be O(n) per write (pipe-delimited
  rewrites entire file). Must wait for SQLite.
- Event system without audit use-case is speculative. Build it when
  audit needs it.

#### Size: Medium (2–3 weeks)

---

### ARCH-012: Monitoring and Observability

| Aspect | Value |
|--------|-------|
| **Target Version** | v0.9.0 |
| **Scope** | Medium |
| **Note** | Per Product Vision: "No Prometheus/Grafana replacement." Lightweight built-in system. |

#### What

System metrics collection, site-level resource usage dashboard,
real log viewer (replace mock data), health check dashboard.

#### Scope

1. **System metrics** — CPU, RAM, disk, network per-server. Collected
   periodically via a background job (needs ARCH-010 job persistence).

2. **Site metrics** — per-site container resource usage via Docker
   stats API. Stored in SQLite time-series (needs ARCH-008).

3. **Real log viewer** — replace mock data in `GET /api/logs` with
   real daemon log parsing. Paginated (needs ARCH-008 pagination).

4. **Health dashboard** — consolidated view of all subsystem health
   checks (core, mail, DNS, SSL, proxy, runtime).

#### Dependencies

- ARCH-008 (SQLite for time-series metrics storage, pagination)
- ARCH-010 (job persistence for periodic metric collection)

#### Size: Medium (2–3 weeks)

---

### Post-v0.9.0 Epics

| Epic | Trigger / Prerequisites |
|------|-----------------------|
| REST API Authentication | When remote API access, multi-node, external automation, or multi-admin is required |
| Authoritative DNS Zone Mgmt | Users need built-in DNS hosting (deferred — Cloudflare/Route53 suffice for most) |
| Role-based Access Control | Multi-admin users exist (post-v1.0 scope per Product Vision) |
| Multi-node / Cluster | REST API Auth + SQLite Storage + NodeManager |
| High Availability | Multi-node foundation |
| Plugin System / Marketplace | Event System + REST API Auth |
| Resource Limits / Quotas | Multi-node or reseller hosting need |

---

## 8. Prioritization Matrix

| Epic | Product Value | Technical Value | Complexity | Risk | Dependencies | Recommended Order |
| ---- | ------------- | --------------- | ---------- | ---- | ------------ | ----------------- |
| **ARCH-008: SQLite Storage** | **Critical** | **Critical** | Medium-Large | **High** (migration) | Storage.h interface | **1** |
| ARCH-009: Real SFTP Provider | **High** | Medium | Medium | Medium | AccessUser exists | **2 (parallel)** |
| ARCH-010: Backup + Persistent Jobs | **High** | **High** | Medium | Low | ARCH-008 | 3 |
| ARCH-011: Audit Log + Events | High | Medium | Medium | Low | ARCH-008 | 4 |
| ARCH-012: Monitoring | Medium | Medium | Medium | Low | ARCH-008, 010 | 5 |
| REST API Authentication | Medium | Medium | Medium | Low | — | Deferred |
| Authoritative DNS | Medium | Low | Medium | Low | — | Deferred |
| RBAC / Multi-admin | Medium | Medium | Large | Medium | Auth (deferred) | Post-v1.0 |
| Multi-node | Medium | High | Extra Large | Very High | Auth + Storage | Post-v1.0 |
| Plugin System | Low | Medium | Extra Large | High | Events + Auth | Post-v1.0 |

---

## 9. Rationale — Why SQLite Is ARCH-008

### The critical self-review validated the wrong recommendation

The critical self-review correctly identified that:
1. AllowAll is Medium severity (localhost-only, not external)
2. The original ARCH-008 scope (7 workstreams) was too large
3. RBAC is premature (single admin)
4. SFTP and auth are independent concerns
5. Storage Foundation is the #1 technical debt

However, the self-review then recommended REST API Authentication as
ARCH-008, with Storage Foundation as ARCH-009. The product owner
overruled this for the following reasons.

### Why REST API Authentication was deferred

1. **No external attack surface.** The API server binds to `127.0.0.1`
   (localhost only). An external attacker cannot reach port 8080. The
   external Web UI port (8081) already has session auth. Someone with
   SSH access can already run `containercp` CLI commands directly.

2. **No current consumer need.** There is no remote API access, no
   multi-node communication, no external automation tooling (Ansible,
   Terraform), and no multi-admin setup. Adding API tokens before
   anyone needs them is speculative engineering.

3. **Session persistence is cosmetic.** The UX complaint "login after
   every daemon restart" is real but minor compared to the technical
   debt of the storage layer.

4. **Auth would need to be reimplemented on SQLite anyway.** Session
   persistence built on pipe-delimited storage would need to be
   rewritten when ARCH-008 (SQLite) ships. Better to build it once
   on the correct foundation.

5. **Timing.** Auth will be needed when the platform gains remote API
   consumers, multi-node, or multi-admin. Those are post-v1.0 features.
   Building auth now means it sits unused for multiple release cycles.

### Why SQLite Storage Foundation is ARCH-008

1. **Biggest technical debt, highest leverage.** The 788-line
   pipe-delimited storage implementation is the foundation of every
   subsystem. Fixing it improves everything.

2. **Blocks all subsequent epics.** Without atomic storage:
   - Job persistence is impossible (SSL renewal lost on restart)
   - Audit log is O(n) per write (rewrite entire file)
   - Pagination is O(n) per query (scan entire file)
   - Backup scheduling has no durable metadata
   - Multi-node synchronization has no transactional foundation

3. **Cleanest migration boundary.** The `Storage` interface is small
   and stable: `save()`, `load()`, `load_all()`, `remove()`. Replacing
   the implementation behind this interface touches exactly one module.
   Managers and API handlers need zero changes.

4. **Build once, use everywhere.** Every downstream epic (backup
   scheduling, audit log, monitoring, pagination, metrics history)
   benefits from SQLite without re-architecting storage again.

5. **Prerequisite for v0.7+ roadmap.** ARCH-010 (Backup Scheduling)
   needs job persistence → needs SQLite. ARCH-012 (Monitoring) needs
   pagination → needs SQLite. ARCH-011 (Audit Log) needs append-only
   storage → needs SQLite.

### What REST API Authentication depends on

When auth is eventually needed (remote API access, multi-node,
multi-admin, external automation), it will depend on:
- ARCH-008 (SQLite — for session and token persistence)
- Existing `AuthService`, `AuthMiddleware` interface, `WebServer::require_session()`

These components already exist. Auth can be implemented as a focused
Medium epic at any future point without architectural changes.

### Revised epic sequence

```
v0.7.0
  ARCH-008  ████████████████████  SQLite Storage Foundation
  ARCH-009  ░░░░████████████████  Real SFTP Provider (parallel)

v0.8.0
  ARCH-010  ████████████████████  Backup Scheduling + Persistent Jobs
  ARCH-011  ░░░░████████████████  Audit Log + Event System

v0.9.0
  ARCH-012  ████████████████████  Monitoring and Observability

Post-v0.9.0 / v1.0+
  REST API Auth (when remote access is needed)
  Authoritative DNS (deferred — Cloudflare/Route53 suffice)
  RBAC / Multi-admin (when multiple admin users exist)
  Multi-node / Cluster (needs auth + storage)
```

### Key dependency graph

```
ARCH-008 (SQLite) ────┬──> ARCH-010 (backup + jobs)
                       ├──> ARCH-011 (audit log)
                       └──> ARCH-012 (monitoring, pagination)

ARCH-009 (SFTP) ─────── independent, no storage dependency

REST API Auth ───────── deferred; depends on ARCH-008 when needed
```

---

## 10. Proposed Planning Sequence

### Phase 0: Product-owner decision ✅

REST API Authentication is deferred. SQLite Storage Foundation is
ARCH-008. This document records the final epic sequence.

### Phase 1: ARCH-008 — SQLite Storage Foundation

1. **Architecture Proposal** — `planning/proposals/ARCH-008-SQLite-Storage-Foundation.md`
   - Problem analysis: pipe-delimited storage limitations
   - Proposed: SQLite backend preserving Storage interface
   - Schema design: table-per-resource-type, `_meta` for schema version
   - Migration strategy: read `.db` → validate → insert into SQLite → verify
   - Rollback: keep original `.db` files, version flag to revert
   - Rejected alternatives (keep pipe-delimited, use LMDB, etc.)
   - Risks and validation plan

2. **ADR** (if needed):
   - SQLite schema design (table-per-type vs single table with type column)
   - WAL mode vs journal mode for concurrent access
   - Migration reliability (transactional migration vs incremental)

3. **Implementation phases**:
   - Phase 1a: SQLiteStorage implementation behind Storage interface
   - Phase 1b: Schema versioning and migration engine
   - Phase 1c: Data migration on daemon startup (`.db` → SQLite)
   - Phase 1d: Rollback and recovery (keep originals, version downgrade)
   - Phase 1e: Indices and pagination foundation
   - Phase 1f: Test suite (unit: SQLiteStorage, integration: migration)
   - Phase 1g: Validation VM deployment + acceptance testing

4. **Release target** — v0.7.0-alpha

### Phase 2: ARCH-009 — Real SFTP Provider

1. **Architecture Proposal** — `planning/proposals/ARCH-009-Real-SFTP-Provider.md`
   - System user lifecycle (useradd/userdel)
   - chroot directory structure and required binaries
   - SSH key management
   - Integration with existing AccessUser/AccessGrant

2. **Note:** Can start in parallel with ARCH-008 — no storage dependency.

### Phase 3: ARCH-010 — Backup Scheduling and Persistent Jobs

1. **Architecture Proposal** — `planning/proposals/ARCH-010-Backup-Scheduling.md`
   - Job persistence design using SQLite
   - Scheduled job engine (cron-like expressions)
   - Retention policies and rotation algorithm
   - Integration with existing TarBackupProvider

### Phase 4: ARCH-011 — Audit Log and Event System

1. **Architecture Proposal** — `planning/proposals/ARCH-011-Audit-Log.md`
   - Append-only audit log in SQLite
   - Pre/post hook registration for events
   - Audit API contract
   - Web UI audit viewer

### Phase 5: ARCH-012 — Monitoring and Observability

1. **Architecture Proposal** — `planning/proposals/ARCH-012-Monitoring.md`
   - System metrics collection (periodic job)
   - Time-series storage in SQLite
   - Real log viewer (replace mock data)
   - Health dashboard

### Summary timeline

```
v0.7.0 ──────────────────────────────────────────────
  ARCH-008  ████████████████████  SQLite Storage Foundation
  ARCH-009  ░░░░████████████████  Real SFTP Provider (parallel)

v0.8.0 ──────────────────────────────────────────────
  ARCH-010  ████████████████████  Backup Scheduling + Persistent Jobs
  ARCH-011  ░░░░████████████████  Audit Log + Event System

v0.9.0 ──────────────────────────────────────────────
  ARCH-012  ████████████████████  Monitoring and Observability
```

### Key dependency graph

```
ARCH-008 (SQLite) ────┬──> ARCH-010 (backup + jobs)
                       ├──> ARCH-011 (audit log)
                       └──> ARCH-012 (monitoring, pagination)

ARCH-009 (SFTP) ─────── independent, no storage dependency
```

---

*Документ підготовлено на основі аналізу репозиторію ContainerCP (коміт `4eceaf7`, v0.6.0).*
*Sections 7–10 revised 2026-07-16: product-owner decision — REST API Authentication deferred, SQLite Storage Foundation is ARCH-008.*
