# ContainerCP Product Roadmap — Post v0.6.0

> **Date:** 2026-07-16
> **Version:** v0.6.0
> **Status:** Analysis document for next phase planning

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

## 7. Proposed Epic Roadmap

### ARCH-008: Production Security & Access Layer

| Аспект | Опис |
|--------|------|
| **Target Version** | v0.7.0 |
| **Scope** | Large |
| **Product Value** | Critical |

#### What

Real REST API authentication, role-based access control, API tokens, real SFTP provider, session persistence, audit logging.

#### Why ARCH-008 now

1. **AllowAll — security gap.** REST API на порту 8080 без автентифікації.
2. **SFTP — core workflow.** Адміністратор не може завантажити файли для сайту.
3. **Audit log — compliance.** Немає логування дій.
4. **Foundation для:** API tokens, roles, multi-admin, automation, security hardening.
5. **Існуючий фундамент:** `AuthService`, `AccessUserManager`, `AccessGrantManager`, `AuthMiddleware` interface, `AuthUser.role` поле, `Session` struct.

#### Scope

1. **REST API Authentication** — real `AuthMiddleware` (token-based), заміна `AllowAllAuth`.
2. **Session Persistence** — сесії зберігаються на диск (в Storage) і відновлюються після restart.
3. **API Tokens** — довгострокові токени для automation/CLI/Ansible, зберігаються з хешем.
4. **Role-based Access** — admin, operator, reseller ролі з обмеженням доступу до ресурсів.
5. **Real SFTP Provider** — системні користувачі (useradd), chroot, SSH keys, authorized_keys.
6. **Audit Log** — централізоване логування всіх mutating operations з user/timestamp/action/detail.
7. **Event System** — базова система подій для audit (pre/post hooks).

#### Out of Scope

- SQLite migration (окремий епік або частина ARCH-009)
- Job persistence
- Pagination
- Rate limiting
- Multi-node
- DNS zone management
- Monitoring

#### Dependencies

- AuthService (існує)
- AccessUserManager, AccessGrantManager (існують)
- AuthMiddleware interface (існує)
- Storage (існує, але pipe-delimited достатньо для auth)
- Logger (існує)

#### Risks

- **Migration:** Існуючі CLI-скрипти та інтеграції використовують порт 8080 без auth. Потрібен backward compatibility plan.
- **Security:** Правильна імплементація token security (hash, rotation, revocation).
- **SFTP:** chroot configuration, system user conflicts при видаленні/оновленні сайту.
- **Performance:** Audit logging не має блокувати API. Потрібен async log queue.

#### Approximate Size: Large (~3 workstreams: Auth + SFTP + Audit)

#### Completion Result

Адміністратор:
1. Логіниться в Web UI з сесією, що виживає після restart.
2. Створює API токени для automation.
3. Створює SFTP-користувача для сайту → може завантажити файли через SFTP.
4. Бачить audit log всіх дій.
5. REST API на порту 8080 вимагає автентифікації.

#### Acceptance Direction

- Deterministic tests (auth, SFTP CRUD, audit)
- Integration tests (real SFTP chroot, API auth flow)
- API validation (token-based access, role restrictions)
- Browser validation (login/logout, session persistence)
- Security validation (token hashing, SQL injection, path traversal)

---

### ARCH-009: Storage Foundation & Scalability

| Аспект | Опис |
|--------|------|
| **Target Version** | v0.7.0 or v0.8.0 |
| **Scope** | Medium-Large |
| **Product Value** | High (technical) |

#### What

SQLite migration, job persistence, pagination for all endpoints, rate limiting.

#### Scope

1. **SQLite migration** — заміна pipe-delimited .db файлів на SQLite.
2. **Job persistence** — jobs зберігаються в SQLite, відновлюються після restart.
3. **Pagination** — `?offset=N&limit=M` для всіх list endpoints.
4. **Rate limiting** — per-IP/per-token обмеження запитів.

#### Dependencies: ARCH-008 (не критично, але бажано)

#### Risks: Migration — існуючі .db файли мають бути сконвертовані

#### Size: Medium-Large

---

### ARCH-010: Backup Scheduling & Disaster Recovery

| Аспект | Опис |
|--------|------|
| **Target Version** | v0.8.0 |
| **Scope** | Medium |

#### What

Автоматичне створення backup за розкладом, retention policies, ротація, відновлення.

#### Scope

1. **Backup scheduling** — періодичне створення backup через JobExecutor.
2. **Retention policies** — daily/weekly/monthly з configurable retention.
3. **Backup rotation** — автоматичне видалення старих backup.
4. **Backup encryption** — опціональне шифрування.
5. **UI enhancements** — scheduling configuration, retention config.

#### Dependencies: ARCH-009 (job persistence)

#### Risks: Disk space management, large backup performance

#### Size: Medium

---

### ARCH-011: Monitoring & Observability Dashboard

| Аспект | Опис |
|--------|------|
| **Target Version** | v0.9.0 |
| **Scope** | Medium |
| **Note** | Відповідно до Product Vision — "No Prometheus/Grafana replacement". Легка вбудована система. |

#### What

System metrics, site-level resource usage, health check dashboard, log viewer (real data).

#### Scope

1. **System metrics** — CPU, RAM, disk, network per-server.
2. **Site metrics** — per-site container resource usage.
3. **Health dashboard** — consolidated view всіх health checks.
4. **Real log viewer** — заміна mock-даних на реальні логи з файлів.
5. **Metrics API** — REST endpoints для consumption.

#### Dependencies: ARCH-009 (pagination for logs)

#### Size: Medium

---

### ARCH-012: Authoritative DNS Zone Management

| Аспект | Опис |
|--------|------|
| **Target Version** | v1.0+ |
| **Scope** | Medium |
| **Note** | DNS Management explicitly excluded from being ARCH-008. |

#### What

DNS zone CRUD, DNS provider interface, DNS record management, automatic DNSSEC.

#### Dependencies: Storage, Provider pattern

#### Size: Medium

---

### Post v1.0 Epics

- Multi-node / Cluster Management
- High Availability
- Plugin System / Marketplace
- Application Catalog (one-click installers)
- Commercial Edition features

---

## 8. Prioritization Matrix

| Epic | Product Value | Technical Value | Complexity | Risk | Dependencies | Recommended Order |
| ---- | ------------- | --------------- | ---------- | ---- | ------------ | ----------------- |
| **ARCH-008: Security & Access** | **Critical** | **High** | Large | Medium | AuthService exists | **1 (ARCH-008)** |
| ARCH-009: Storage Foundation | High | **Critical** | Medium-Large | **High** (migration) | — | 2 |
| ARCH-010: Backup Scheduling | High | Medium | Medium | Low | ARCH-009 | 3 |
| ARCH-011: Monitoring | Medium | Medium | Medium | Low | ARCH-009 | 4 |
| ARCH-012: Authoritative DNS | Medium | Low | Medium | Low | — | 5 |
| Multi-node | Medium | High | Extra Large | Very High | ARCH-008, 009 | Post-1.0 |
| Plugin System | Low | Medium | Extra Large | High | ARCH-008 | Post-1.0 |
| Resource Limits | Medium | Low | Small | Low | ARCH-009 | Post-1.0 |

---

## 9. ARCH-008 Recommendation

### ARCH-008: Production Security & Access Layer

**Рекомендований наступний епік.**

#### Чому саме цей модуль потрібен наступним

1. **Security gap.** `AllowAllAuth` — REST API без автентифікації. Це єдина critical відома проблема, задокументована в release notes, яка блокує production deployment.

2. **Core workflow.** SFTP provider — заглушка. Адміністратор не може виконати базову операцію "створити сайт → завантажити файли" згідно Product Vision для v1.0.

3. **Compliance.** Audit log відсутній. Без нього неможливий production для hosting provider.

4. **Foundation.** Auth, roles, audit — це foundation для всіх наступних епіків: multi-admin, API tokens, automation, plugin system, multi-node.

#### Чому важливіший за інші кандидати

- **Storage Foundation (SQLite):** Важливий, але не блокує production так, як відсутність auth. Користувач воліє бачити захищену панель з pipe-delimited storage, ніж незахищену з SQLite.

- **Backup Scheduling:** Відсутній scheduling — це незручність, а не безпека. Можна створювати backup через cron з CLI.

- **Monitoring:** Відповідно до Product Vision — out of scope для v1.0.

- **Authoritative DNS:** Read-only diagnostics вже є. Zone management — v2.0 scope.

#### Існуючі компоненти

- `libs/auth/AuthService.h/.cpp` (248 рядків) — session auth, password hashing
- `libs/auth/AuthUser.h` — `role` поле вже існує
- `libs/api/AuthMiddleware.h` — abstract interface, ready for real implementation
- `libs/access/AccessUser.h/.cpp` — реальний CRUD
- `libs/access/AccessGrant.h/.cpp` — permissions per site
- `libs/access/AccessProvider.h` — abstract interface
- `docs/SFTP-PROVIDER.md` — детальний план SFTP implementation
- `libs/api/WebServer.cpp` — `require_session()` вже реалізовано
- `web/app.js` — login page, session management

#### Які технічні ризики прибирає

- AllowAll — безпека API
- In-memory sessions — UX (login після кожного restart)
- SFTP placeholder — неможливість завантажити файли
- Відсутність audit — compliance

#### Які наступні епіки відкриває

- API tokens → Ansible/Terraform integration
- Role-based access → multi-admin → reseller hosting
- Audit → compliance → enterprise features
- Real SFTP → site migration → backup scheduling

#### Чому доцільно реалізувати саме після ARCH-007

ARCH-007 додав DNS Diagnostics — read-only модуль без мутацій даних. Модулі до ARCH-007 (Sites, SSL, Mail, Proxy) вже мають повноцінні CRUD операції. Без автентифікації всі ці операції доступні будь-кому з мережевим доступом до порту 8080. Після ARCH-007 system має достатньо функціональності, щоб бути корисною, але незахищеною. ARCH-008 має захистити цю функціональність.

#### Чому наступним епіком не повинні бути

**DNS Management:** Read-only diagnostics вже існує. Authoritative DNS — це окремий продукт (DNS server), який потребує значної архітектурної роботи. Користувачі можуть використовувати Cloudflare, DigitalOcean, Route53 для DNS. Це не блокує core hosting workflow.

**Multi-node:** Потребує: auth (ARCH-008), storage migration (ARCH-009), node-to-node communication, distributed storage. Це Extra Large епік з високим ризиком. Занадто рано.

**High Availability:** Ще більш складний, ніж multi-node. Потребує multi-node як foundation.

**Plugin Marketplace:** Потребує: auth (ARCH-008), event system (ARCH-008), plugin API design, sandboxing. Великий scope без негайної product value.

---

## 10. Proposed Planning Sequence

Після погодження ARCH-008:

1. **Architecture Proposal** — `planning/proposals/ARCH-008-Security-Access-Layer.md`
   - Problem, Motivation, Current Architecture, Proposed Architecture
   - New/Modified Resources, Managers, Providers
   - REST API зміни (AuthMiddleware, нові endpoints)
   - Web UI зміни (SFTP management, roles management, audit viewer)
   - CLI зміни (access key, token commands)
   - Migration Strategy (AllowAll → real auth, backward compatibility)
   - Rejected Alternatives, Risks, Validation Plan

2. **ADR** — за потреби:
   - Token-based API authentication strategy
   - SFTP chroot architecture
   - Audit log data model

3. **Implementation Plan** — phase breakdown:
   - Phase 1: AuthMiddleware + Session persistence + API tokens
   - Phase 2: Role-based access control
   - Phase 3: Real SFTP provider (system users, chroot, SSH keys)
   - Phase 4: Audit log (event system, storage, API, UI)
   - Phase 5: Web UI (SFTP management, roles, audit viewer)
   - Phase 6: CLI (token create/revoke, access key management)
   - Phase 7: Tests (unit, integration, security)
   - Phase 8: Validation VM deployment + acceptance testing

4. **Phase breakdown** — деталізація кожного phase з технічними завданнями.

5. **Acceptance criteria** — checklist згідно з `planning/product-validation.md`.

6. **Test strategy** — deterministic tests, integration tests, security validation.

7. **Release target** — v0.7.0 (після ARCH-008).

---

*Документ підготовлено на основі аналізу репозиторію ContainerCP (коміт `4eceaf7`, v0.6.0).*
