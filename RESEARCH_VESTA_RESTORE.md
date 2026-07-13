# Дослідження: Механізм Restore у myVestaCP

## 1. Файли, що беруть участь у Restore

### CLI скрипти (ядро)

| Файл | Призначення | Хто викликає |
|------|-------------|--------------|
| `/bin/v-restore-user` | **Головний скрипт restore** (852 рядки). Розпаковує архів, відновлює WEB/DNS/MAIL/DB/CRON/UDIR послідовно | `v-schedule-user-restore` через `backup.pipe` |
| `/bin/v-schedule-user-restore` | **Планувальник restore** (68 рядків). Записує команду в чергу `data/queue/backup.pipe` | Web UI (`schedule/restore/`, `bulk/restore/`) |
| `/bin/v-normalize-restored-user` | **Нормалізація NS/IP** (89 рядків). Замінює NS1/NS2/IP старого сервера на нові у DNS зонах | Ручний запуск після cross-server restore |

### Web UI (PHP)

| Файл | Призначення |
|------|-------------|
| `/web/schedule/restore/index.php` | Сторінка запуску restore (вибір компонентів) |
| `/web/bulk/restore/index.php` | Bulk restore для кількох бекапів |
| `/web/list/backup/index.php` | Сторінка списку бекапів з кнопкою Restore |
| `/web/templates/admin/list_backup_detail.html` | Детальна сторінка з налаштуваннями restore |

### Helpers (shared libraries)

| Файл | Функції, що використовуються restore |
|------|--------------------------------------|
| `/func/main.sh` | `is_backup_available()`, `is_backup_enabled()`, `is_backup_scheduled()`, `check_args()`, `is_format_valid()`, `check_result()`, `send_notice()`, `recalc_user_disk_usage()`, `sync_cron_jobs()`, `is_object_valid()`, `is_domain_new()` |
| `/func/domain.sh` | `is_web_template_valid()`, `is_proxy_template_valid()`, `is_backend_template_valid()`, `is_dns_template_valid()`, `rebuild_web_domain_conf()`, `rebuild_dns_domain_conf()`, `rebuild_mail_domain_conf()` |
| `/func/ip.sh` | `is_ip_valid()`, `get_user_ip()` |
| `/func/db.sh` | `rebuild_mysql_database()`, `rebuild_pgsql_database()`, `import_mysql_database()`, `import_pgsql_database()` |
| `/func/rebuild.sh` | `rebuild_user_conf()`, `rebuild_web_domain_conf()` |
| `/conf/vesta.conf` | `BACKUP`, `BACKUP_SYSTEM`, `BACKUP_DISK_LIMIT`, `BACKUP_LA_LIMIT`, `BACKUP_GZIP`, `BACKUP_TEMP` |

---

## 2. Call Graph

```
Web UI (schedule/restore/index.php)
    │ POST: user, backup, web, dns, mail, db, cron, udir
    ▼
v-schedule-user-restore $user $backup $web $dns $mail $db $cron $udir
    │
    │ Записує в data/queue/backup.pipe:
    │   nice -n 19 ionice -c 3 v-restore-user $user $backup * * * * * no yes
    ▼
[CRON] v-update-sys-queue (читає backup.pipe)
    │
    │ Виконує команду з nice/ionice
    ▼
v-restore-user $user $backup [WEB] [DNS] [MAIL] [DB] [CRON] [UDIR] [NOTIFY]
    │
    ├── 1. Перевірки:
    │   ├── is_backup_available($user, $backup) — валідація імені файлу
    │   ├── disk space ≥ BACKUP_DISK_LIMIT
    │   ├── load average ≤ BACKUP_LA_LIMIT
    │   └── чи існує користувач (create_user=yes/no)
    │
    ├── 2. Скачування бекапу (якщо не локально):
    │   ├── google_download() — gsutil
    │   ├── sftp_download() — expect + sftp
    │   └── ftp_download() — ftp
    │
    ├── 3. Створення tmpdir: mktemp -p $BACKUP_TEMP
    │
    ├── 4. Відновлення користувача (якщо create_user=yes):
    │   ├── tar xf .../vesta — розпаковка конфігів
    │   ├── cp user.conf, ssl, backup-excludes.conf
    │   └── rebuild_user_conf — перебудова конфігу
    │
    ├── 5. PAM контейнер:
    │   ├── tar xf .../pam — отримання passwd
    │   ├── old_uid, new_uid — для chown
    │
    ├── 6. WEB ($web != 'no' && WEB_SYSTEM):
    │   ├── tar -tf | grep domain_data.tar.gz → список доменів
    │   ├── Фільтрація (user:* або список)
    │   ├── Для кожного domain:
    │   │   ├── is_domain_new('web', $domain) — перевірка конфлікту
    │   │   ├── tar xf .../web/$domain — розпаковка домену
    │   │   ├── Відновлення web.conf з перевірками:
    │   │   │   ├── Видалення конфліктних аліасів
    │   │   │   ├── is_ip_valid → заміна IP
    │   │   │   ├── is_web_template_valid → default
    │   │   │   ├── is_proxy_template_valid → default
    │   │   │   ├── is_backend_template_valid → default
    │   │   │   ├── Конвертація ftp/stats users: old_user_ → user_
    │   │   │   ├── Копіювання SSL сертифікатів
    │   │   │   └── Запис у web.conf
    │   │   ├── rebuild_web_domain_conf — конфіг веб-сервера
    │   │   ├── Розпаковка domain_data.tar.gz — файли сайту
    │   │   ├── chown: old_uid → new_uid (якщо відрізняються)
    │   │   └── Відновлення php-fpm pool.d:
    │   │       ├── cp -r $fpmver/ /etc/php/
    │   │       └── systemctl restart php$fpmver-fpm
    │   ├── v-restart-web — рестарт nginx/apache
    │   └── v-restart-proxy — рестарт proxy (якщо є)
    │
    ├── 7. DNS ($dns != 'no' && DNS_SYSTEM):
    │   ├── tar -tf | grep dns.conf → список доменів
    │   ├── Фільтрація
    │   ├── Для кожного domain:
    │   │   ├── is_domain_new('dns', $domain)
    │   │   ├── tar xf .../dns/$domain
    │   │   ├── Відновлення dns.conf (IP, TPL)
    │   │   ├── cp dns/$domain.conf → USER_DATA/dns/
    │   │   └── rebuild_dns_domain_conf
    │   └── v-restart-dns — рестарт bind9
    │
    ├── 8. MAIL ($mail != 'no' && MAIL_SYSTEM):
    │   ├── tar -tf | grep mail.conf → список доменів
    │   ├── Фільтрація
    │   ├── Для кожного domain:
    │   │   ├── is_domain_new('mail', $domain)
    │   │   ├── tar xf .../mail/$domain
    │   │   ├── Відновлення mail.conf
    │   │   ├── Копіювання DKIM ключів (*.pem, *.pub)
    │   │   ├── cp $domain.conf → USER_DATA/mail/
    │   │   ├── rebuild_mail_domain_conf
    │   │   ├── Розпаковка accounts.tar.gz — пошта
    │   │   └── chown old_uid → new_uid
    │
    ├── 9. DB ($db != 'no' && DB_SYSTEM):
    │   ├── tar -tf | grep db.conf → список БД
    │   ├── Фільтрація
    │   ├── Для кожної database:
    │   │   ├── tar xf .../db/$database
    │   │   ├── Конвертація імен: old_user_ → user_
    │   │   ├── gzip -d *.sql.gz
    │   │   └── mysql|pgsql:
    │   │       ├── rebuild_mysql_database
    │   │       └── import_mysql_database $dump
    │
    ├── 10. CRON ($cron != 'no' && CRON_SYSTEM):
    │   ├── tar xf .../cron
    │   ├── cp cron.conf
    │   ├── sync_cron_jobs
    │   └── v-restart-cron
    │
    ├── 11. UDIR ($udir != 'no'):
    │   ├── tar -tf | grep user_dir
    │   ├── Для кожного user_dir:
    │   │   ├── tar xf .../user_dir/$udir.tar.gz
    │   │   ├── tar xzf → $HOMEDIR/$user/
    │   │   └── chown old_uid → new_uid
    │
    ├── 12. Фіналізація:
    │   ├── mkdir tmp/ + chmod 700
    │   ├── Відправка email-повідомлення
    │   ├── rm -rf $tmpdir
    │   ├── Видалення скачаного бекапу (якщо downloaded=yes)
    │   └── Очищення черги
    │
    └── 13. Оновлення лічильників:
        ├── v-update-user-counters $user
        ├── v-update-user-counters admin
        └── v-update-sys-ip-counters
```

---

## 3. Структура backup архіву

Backup — це єдиний `.tar` файл (без стиснення на рівні архіву, але внутрішні компоненти — `.tar.gz`).

```
backup.tar
│
├── vesta/
│   ├── user.conf          # Конфіг користувача
│   ├── ssl/               # SSL сертифікати
│   │   ├── $domain.crt
│   │   └── $domain.key
│   └── backup-excludes.conf  # Виключення бекапу
│
├── pam/
│   └── passwd             # /etc/passwd для UID mapping
│
├── web/
│   └── $domain/
│       ├── vesta/
│       │   └── web.conf   # Конфіг домену (IP, TPL, ALIAS, SSL, PHP, BACKEND, PROXY, etc.)
│       └── domain_data.tar.gz   # Файли сайту (стиснуті gzip)
│
├── dns/
│   └── $domain/
│       └── vesta/
│           ├── dns.conf   # Конфіг DNS зони
│           └── $domain.conf       # DNS записи
│
├── mail/
│   └── $domain/
│       ├── vesta/
│       │   ├── mail.conf  # Конфіг поштового домену
│       │   ├── $domain.pem       # DKIM приватний ключ
│       │   ├── $domain.pub       # DKIM публічний ключ
│       │   └── $domain.conf      # Поштові акаунти
│       └── accounts.tar.gz       # Файли поштових скриньок
│
├── db/
│   └── $database/
│       ├── vesta/
│       │   └── db.conf    # Конфіг БД (DBUSER, TYPE, CHARSET, HOST)
│       └── $database.$TYPE.sql.gz  # Дамп БД (стиснутий gzip)
│
├── cron/
│   └── cron.conf          # Cron jobs
│
└── user_dir/
    └── $udir.tar.gz       # Довільні файли з home директорії
```

---

## 4. Як визначається, що потрібно відновити?

### Аргументи CLI
```
v-restore-user <user> <backup> [WEB] [DNS] [MAIL] [DB] [CRON] [UDIR] [NOTIFY]
```

Де:
- `*` або порожнє — всі компоненти
- `no` — пропустити компонент
- `dom1,dom2` — тільки конкретні домени (через кому)
- `notify=yes/no` — відправляти email

### Як працює фільтрація

```bash
# Для WEB:
backup_domains=$(tar -tf $BACKUP/$backup | grep "^./web")
backup_domains=$(echo "$backup_domains" | grep domain_data.tar.gz)
backup_domains=$(echo "$backup_domains" | cut -f 3 -d /)
if [ -z "$web" ] || [ "$web" = '*' ]; then
    domains="$backup_domains"
else
    echo "$web" | tr ',' '\n' | sed -e "s/^/^/" > $tmpdir/selected.txt
    domains=$(echo "$backup_domains" | egrep -f $tmpdir/selected.txt)
fi
```

Аналогічно для DNS, MAIL, DB.

### Для баз даних — фільтрація по суфіксу:
```bash
echo "$db" | tr ',' '\n' | sed -e "s/$/$/" > $tmpdir/selected.txt
databases=$(echo "$backup_databases" | egrep -f $tmpdir/selected.txt)
```

---

## 5. Як відновлюються компоненти

### 5.1 Web files
```bash
tar xf $backup -C $tmpdir ./web/$domain
chown $user $tmpdir && chmod u+w $HOMEDIR/$user/web/$domain
sudo -u $user tar -xzpf $tmpdir/web/$domain/domain_data.tar.gz \
    -C $HOMEDIR/$user/web/$domain/ --exclude=./logs/*
find ... -exec chown -h $user:$user {} \;    # fix для tar < 1.24
find ... -user $old_uid -exec chown -h $user:$user {} \;  # remap UID
```

### 5.2 Nginx/Apache конфіги
```bash
rebuild_web_domain_conf  # з func/rebuild.sh
# Використовує TPL, PROXY, BACKEND з web.conf
$BIN/v-restart-web
```

### 5.3 PHP settings
```bash
if [ -d "$tmpdir/web/$domain/php" ]; then
    fpmver=$(ls $tmpdir/web/$domain/php/)
    cp -r $tmpdir/web/$domain/php/$fpmver/ /etc/php/
    systemctl reset-failed php$fpmver-fpm
    systemctl restart php$fpmver-fpm
fi
```

### 5.4 SSL сертифікати
```bash
if [ "$SSL" = 'yes' ]; then
    certificates=$(ls $tmpdir/web/$domain/conf| grep ssl)
    for crt in $certificates; do
        crt=$(echo $crt|sed -e "s/ssl.//")
        cp -f $tmpdir/web/$domain/conf/ssl.$crt $USER_DATA/ssl/$crt
    done
fi
```

### 5.5 Databases (MySQL/PostgreSQL)
```bash
gzip -d $tmpdir/db/$database/$database.*.sql.gz
case $TYPE in
    mysql) rebuild_mysql_database; import_mysql_database $database_dump ;;
    pgsql) rebuild_pgsql_database; import_pgsql_database $database_dump ;;
esac
```

### 5.6 Database users
```bash
DB=$(echo "$DB"  |sed -e "s/${old_user}_//")
DB="${user}_${DB}"      # Додавання префікса нового користувача
DBUSER=$(echo "$DBUSER" |sed -e "s/${old_user}_//")
DBUSER="${user}_${DBUSER}"
```

### 5.7 DNS зони
```bash
cp -f $tmpdir/dns/$domain/vesta/$domain.conf $USER_DATA/dns/
rebuild_dns_domain_conf
$BIN/v-restart-dns
```

### 5.8 Mail акаунти
```bash
cp -f $tmpdir/mail/$domain/vesta/$domain.conf $USER_DATA/mail/
rebuild_mail_domain_conf
sudo -u $user tar -xzpf $tmpdir/mail/$domain/accounts.tar.gz \
    -C $HOMEDIR/$user/mail/$domain_idn/
find ... -user $old_uid -exec chown -h $user:mail {} \;  # remap UID
find ... -user root -exec chown $exim_user {} \;              # exim permissions
```

### 5.9 Cron jobs
```bash
cp $tmpdir/cron/cron.conf $USER_DATA/cron.conf
sync_cron_jobs
$BIN/v-restart-cron
```

---

## 6. Чи можна відновити лише один сайт?

**Так.** Це реалізовано через аргументи командного рядка.

```bash
# Відновити тільки web домен example.com (без DNS, MAIL, DB, CRON)
v-schedule-user-restore admin backup.tar example.com no no no no no
# АБО через інтерфейс:
# Web UI → list/backup → configure restore → вибрати компоненти
```

### Як це працює в коді:

```bash
# Аргумент $web може бути:
#   *           — всі домени
#   example.com — один домен
#   dom1,dom2   — кілька доменів
#   no          — пропустити

# Фільтрація:
domains=$(echo "$backup_domains" | egrep -f $tmpdir/selected.txt)
# selected.txt містить "^example.com$"
```

Для **одного сайту** виклик виглядає так:
```bash
v-restore-user $user $backup example.com no no no no no yes
```

**ContainerCP** матиме простіше — один сайт = одна операція без префіксів користувача.

---

## 7. Тимчасовий каталог

```bash
if [ -z "$BACKUP_TEMP" ]; then
    BACKUP_TEMP=$BACKUP   # за замовчуванням /backup
fi
tmpdir=$(mktemp -p $BACKUP_TEMP -d)
# Наприклад: /backup/tmp.XXXXXX
```

Алгоритм:
1. `mktemp -p $BACKUP_TEMP -d` — створює унікальну тимчасову директорію
2. `tar xf $BACKUP/$backup -C $tmpdir ./vesta` — часткове розпакування
3. Для кожного компонента: `tar xf $BACKUP/$backup -C $tmpdir ./<компонент>`
4. Після завершення: `rm -rf $tmpdir`

**Ключова особливість**: tar розпаковується ЧАСТКОВО — тільки потрібний компонент для кожного домена, а не весь архів одразу. Це економить диск і час.

---

## 8. Перевірка backup

### 8.1 Ownership (ім'я файлу)
```bash
is_backup_available() {
    passed=false
    if [[ $2 =~ ^$1.[0-9]{4}-[0-9]{2}-[0-9]{2}_[0-9]{2}-[0-9]{2}-[0-9]{2}.tar$ ]]; then
        passed=true
    elif [[ $2 =~ ^$1.[0-9]{4}-[0-9]{2}-[0-9]{2}.tar$ ]]; then
        passed=true
    fi
    if [ $passed = false ]; then
        check_result $E_FORBIDEN "permission denied"
    fi
}
```

Формат: `$user.YYYY-MM-DD_HH-MM-SS.tar` або `$user.YYYY-MM-DD.tar`

### 8.2 Інші перевірки
- **Disk space**: `df $BACKUP | tail -n1 | ... ≥ BACKUP_DISK_LIMIT`
- **Load average**: `/proc/loadavg ≥ BACKUP_LA_LIMIT` (чекає до 15 хвилин)
- **Domain конфлікти**: `is_domain_new('web', $domain)` — чи не належить іншому користувачу
- **Template валідність**: `is_web_template_valid`, `is_proxy_template_valid`, `is_backend_template_valid`, `is_dns_template_valid`

### Чого НЕМАЄ в перевірках
- ❌ **Checksum** архіву не перевіряється
- ❌ **Version** бекапу не зберігається
- ❌ **Metadata** окрім імені файлу не зберігається
- ❌ **Цілісність архіву** не перевіряється (`tar` сама видасть помилку при розпаковці)

---

## 9. Обробка помилок

### Патерн помилок
```bash
check_result "$E_PARSING" "опис помилки"
# або
rm -rf $tmpdir
echo "error" | $SENDMAIL -s "$subj" $email $notify
sed -i "/ $user /d" $VESTA/data/queue/backup.pipe
check_result "$E_*" "опис помилки"
```

### Типові помилки

| Ситуація | Реакція |
|----------|---------|
| Backup не існує локально | Скачування з FTP/SFTP/Google |
| Backup недоступний ніде | `check_result $E_NOTEXIST` |
| Недостатньо диска | `check_result $E_DISK` + email |
| Високий Load Average | Чекає до 15 хв, потім помилка |
| Домен належить іншому користувачу | `check_result $E_PARSING` |
| tar не може розпакувати | `check_result $E_PARSING` + email |
| Клієнт не існує | `create_user="yes"` → автоматичне створення |
| IP не валідний | Підставляється вільний IP |
| Template не існує | Підставляється 'default' |
| PHP-FPM не стартує | `systemctl reset-failed` + restart |

---

## 10. Функції, що можна використати у ContainerCP без змін

На жаль, майже нічого. MyVestaCP — це Bash-скрипти, ContainerCP — C++.

**Можна взяти як алгоритм (переписати на C++):**

1. **Логіка фільтрації доменів** — вибір конкретного домену з архіву
2. **Tar-команди** — часткове розпакування (`tar xf archive -C tmpdir ./component/domain`)
3. **UID remapping** — `find ... -user $old_uid -exec chown {} \;`
4. **Database префікси** — конвертація `old_user_` → `new_user_`
5. **IPP/SSL/Template fallback** — перевірка існування з підстановкою default

---

## 11. Що доведеться повністю переписати під ContainerCP

| Компонент Vesta | ContainerCP аналог |
|-----------------|-------------------|
| `/home/$user/web/$domain/` | `/srv/containercp/sites/$domain/` |
| `/home/$user/conf/web/` | Генерується через `NginxProxyProvider` + `DockerComposeProvider` |
| nginx/apache конфіги | Docker Compose + nginx proxy container |
| DNS zone files | Не реалізовано (DNS-модуль у плані) |
| Exim/Dovecot конфіги | Docker MailProvider (Postfix + Dovecot containers) |
| MySQL бази | `DatabaseManager` + MariaDB container |
| User система | `UserManager` + `SiteManager` |
| PHP-FPM pool | Docker PHP container per-site |
| SSL сертифікати | `CertificateStore` + Let's Encrypt |
| Cron | Немає аналога (можна через systemd timers або Docker) |

**Повністю нове:**
- Читання tar-архіву VestaCP і маппінг його структури на ContainerCP
- Імпорт веб-файлів у Docker volume
- Створення сайту через `SiteCreateOperation`
- Імпорт баз даних через MariaDB container
- Імпорт пошти через Dovecot/Postfix containers

---

## 12. Залежності Restore

| Інструмент | VestaCP | ContainerCP |
|-----------|---------|-------------|
| tar | ✅ обов'язково | ✅ обов'язково |
| gzip | ✅ обов'язково | ✅ обов'язково |
| mysql/mariadb | ✅ для DB | ✅ через Docker |
| pg_dump/pg_restore | ✅ для PostgreSQL | ⬜ не реалізовано |
| openssl | ✅ для DKIM | ✅ через Docker |
| rsync | ❌ не використовується | ❌ |
| systemctl | ✅ для restart сервісів | ❌ Docker |
| nginx/apache | ✅ рестарт | ❌ Docker |
| bind9 | ✅ для DNS | ⬜ не реалізовано |
| exim4 | ✅ для пошти | ❌ Docker Postfix |
| dovecot | ✅ для IMAP | ✅ через Docker |
| nice/ionice | ✅ для фонових задач | ⬜ можна додати |
| expect | ✅ для SFTP | ⬜ можна через libssh |

---

## 13. Повний алгоритм Restore (людською мовою)

1. **Запуск**: Користувач натискає "Restore" в Web UI, вибирає бекап і компоненти.
2. **Планування**: PHP записує команду в чергу `backup.pipe`.
3. **Виконання**: CRON читає чергу, запускає `v-restore-user` з низьким пріоритетом.
4. **Перевірки**: Скрипт перевіряє ім'я файлу, диск, load average.
5. **Скачування**: Якщо архів не локально — скачує з FTP/SFTP/Google.
6. **Розпаковка**: Створює tmpdir, розпаковує `vesta/` і `pam/` контейнери.
7. **Створення клієнта**: Якщо клієнт не існує — створює з конфігів бекапу.
8. **UID mapping**: Запам'ятовує old_uid з `pam/passwd` для chown.
9. **Відновлення WEB**: Для кожного домену перевіряє конфлікти, розпаковує файли,
   перевіряє IP/шаблони, конвертує ftp/stats користувачів, копіює SSL, 
   перебудовує конфіг веб-сервера, імпортує файли сайту, фіксить права.
10. **Відновлення DNS**: Розпаковує DNS зони, перевіряє IP/шаблони, перебудовує.
11. **Відновлення MAIL**: Розпаковує поштові домени, копіює DKIM, акаунти, дані.
12. **Відновлення DB**: Розпаковує бази, конвертує префікси, імпортує дампи.
13. **Відновлення CRON**: Копіює cron.conf, синхронізує, рестартить cron.
14. **Відновлення UDIR**: Розпаковує домашні файли, фіксить права.
15. **Очищення**: Видаляє tmpdir, скачаний бекап (якщо був).
16. **Сповіщення**: Відправляє email з логом.
17. **Лічильники**: Оновлює дискові лічильники користувача.

---

## Додатково: Порівняння VestaCP / myVestaCP / HestiaCP

### Відмінності

| Аспект | VestaCP (original) | myVestaCP | HestiaCP |
|--------|-------------------|-----------|----------|
| **Мова** | Bash | Bash + PHP | Bash + PHP |
| **Backup/Restore** | Базова реалізація | Без змін | Значно доопрацьовано |
| **v-restore-user** | Оригінал | Копія Vesta + SFTP | Рефакторинг: винесено `is_backup_available()`, додано `OVERRIDE_BACKUP_PATH`, прибрано `source /etc/profile` |
| **Підтримка SFTP** | ❌ | ✅ (expect) | ✅ (expect) |
| **Google Cloud** | ❌ | ✅ (gsutil) | ❌ |
| **Помилки** | Стандартні | Стандартні | Покращені повідомлення |
| **UID remapping** | ✅ | ✅ | ✅ |
| **Відновлення одного сайту** | ✅ (через аргументи) | ✅ | ✅ |
| **Queue система** | `backup.pipe` | `backup.pipe` | `backup.pipe` |

### Що краще в myVestaCP

1. **SFTP підтримка** — використовує `expect` для автоматизації SFTP
2. **Google Cloud Storage** — завантаження/скачування через `gsutil`
3. **Override backup path** — `OVERRIDE_BACKUP_PATH` для тестування

### Що краще в HestiaCP

1. **Чистіший код** — прибрано застарілі конструкції
2. **Краща обробка аргументів** — використовує `getopt`
3. **Більше валідації** — перевірка перед розпаковкою

### Рекомендації для ContainerCP

1. **Архітектура**: Використати асинхронний Job-підхід (вже є `JobManager` + `JobExecutor`)
2. **Tar**: C++ може використовувати `libarchive` або викликати `tar` через `CommandExecutor`
3. **Структура архіву**: Парсити `tar -tf` для отримання списку доменів
4. **UID remapping**: Не потрібен (Docker контейнери, немає UID на хості)
5. **Database імпорт**: Через docker exec MariaDB контейнера
6. **Пошта**: Через DockerMailProvider (create mailbox + import maildir)
7. **DNS**: Поки що не реалізовувати (DNS-модуль в плані)
8. **Rotate**: Врахувати що VestaCP зберігає тільки N останніх бекапів
