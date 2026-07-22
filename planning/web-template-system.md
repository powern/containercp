# Web Template System — Implementation Plan

## Objective
Зробити web server templates (Apache/Nginx) повністю кастомізованими: зберігати на диску, редагувати через UI, створювати/клонувати, застосовувати до існуючих сайтів, з real-IP фіксом в дефолтних шаблонах.

---

## 1. Шаблони на диску (перестати перезаписувати з binary)

**Файли:** `/etc/containercp/templates/web/<name>.conf.template`

**Зміна в `ServiceRegistry.cpp` (~рядки 243-263):**
- Якщо файл вже існує на диску — **не перезаписувати**
- Дефолтні шаблони писати тільки при першому запуску (коли файлів немає)
- Завжди синхронізувати **лише список профілів** (імена, тип), але content залишати як є

## 2. Default templates з real-IP фіксом

### Apache (`apache-php-default`):
```
<VirtualHost *:80>
    ServerName {{DOMAIN}}
    DocumentRoot {{PUBLIC_ROOT}}
    DirectoryIndex index.php index.html

    RemoteIPHeader X-Forwarded-For
    RemoteIPInternalProxy 172.31.0.0/16

    <Directory {{PUBLIC_ROOT}}>
        Options FollowSymLinks
        AllowOverride All
        Require all granted
    </Directory>

    <FilesMatch \.php$>
        SetHandler "proxy:fcgi://{{PHP_UPSTREAM}}"
    </FilesMatch>

    ErrorLog {{LOG_ROOT}}/error.log
    CustomLog {{LOG_ROOT}}/access.log combined
</VirtualHost>
```

### Apache (`apache-wordpress`):
Same + `mod_rewrite` + `SetEnvIf Authorization`.

### Nginx (`nginx-php-default`):
```
server {
    listen 80;
    server_name {{DOMAIN}};
    root {{PUBLIC_ROOT}};
    index index.php index.html;

    set_real_ip_from 172.31.0.0/16;
    real_ip_header X-Forwarded-For;

    location / {
        try_files $uri $uri/ /index.php?$query_string;
    }

    location ~ \.php$ {
        fastcgi_pass {{PHP_UPSTREAM}};
        fastcgi_index index.php;
        fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
        include fastcgi_params;
    }

    location ~ /\.ht {
        deny all;
    }
}
```

## 3. 00-load-modules.conf — додати mod_remoteip

**Файл:** `DockerComposeProvider.cpp` (~рядок 163-170)

Додати `LoadModule remoteip_module modules/mod_remoteip.so` до списку.

## 4. API endpoints

| Method | Path | Action |
|--------|------|--------|
| `GET` | `/api/profiles` | ✅ exists (read-only list) |
| `POST` | `/api/profiles` | Create new template |
| `PUT` | `/api/profiles/<id>` | Update template |
| `DELETE` | `/api/profiles/<id>` | Delete template |
| `POST` | `/api/sites/<id>/apply-template` | Re-render and apply to existing site |

### POST /api/profiles
```json
{
    "name": "apache-custom",
    "web_server": "apache",
    "description": "My custom template",
    "content": "<VirtualHost ...>{{DOMAIN}}...</VirtualHost>"
}
```
→ створює файл на диску + запис в БД.

### PUT /api/profiles/<id>
```json
{
    "content": "...",
    "default": true
}
```
→ оновлює файл на диску, оновлює default flag.

### POST /api/sites/<id>/apply-template
```json
{
    "template_id": 5
}
```
→ перечитує template з диску, рендерить з поточними параметрами сайту, перезаписує `config/apache/default.conf`, restart web container.

## 5. UI сторінка Templates

**Файл:** `web/pages/templates.js`

Переробити з read-only списку на повноцінну сторінку:

- **Таблиця:** Name | Web Server | Type | Default | [Edit] [Clone] [Apply to Site] [Delete]
- **Кнопка `+ Create Template`** — відкриває модалку
- **Модалка Create/Edit:**
  - Name (текст)
  - Web Server (select: apache | nginx)
  - Description (текст)
  - Content (textarea/code editor з monospace)
  - Set as default (checkbox)
- **Clone:** відкриває Create модалку з пре-філом даних з оригіналу
- **Apply to Site:** випадаючий список сайтів + кнопка Apply
- **Delete:** confirm діалог (захист від видалення останнього default)

## 6. Застосування до існуючого сайту

**Файл:** `libs/provider/DockerComposeProvider.{h,cpp}`

Новий метод:
```cpp
OperationResult apply_web_template(Site& site, const std::string& template_path);
```
Логіка:
- Визначити web_server_type (apache/nginx) з профілю
- Прочитати template з диску
- Забрати поточні параметри сайту (domain, doc_root, upstream, log_dir)
- Рендер через `engine.render_web()`
- Перезаписати `config/apache/default.conf` або `config/nginx/default.conf`
- Рестарт web контейнера (`docker compose restart web`)

**Файл:** `libs/operations/SiteCreateOperation.cpp` або окремий `ApplyTemplateOperation.cpp`

## 7. Оцінка змін

| Компонент | Рядків |
|-----------|--------|
| `ServiceRegistry.cpp` — stop overwriting | ~5 |
| `web_templates.h` — real-IP fix + nginx templates | ~30 |
| `DockerComposeProvider.cpp` — mod_remoteip + apply_web_template | ~25 |
| API — CRUD profiles + apply-template | ~120 |
| UI — templates.js | ~150 |
| UI — site page "Apply Template" button | ~20 |
| TemplateEngine — optional `{{PROXY_IP}}` var | ~10 |
| **Total** | **~360** |

## 8. Порядок реалізації (API first)

1. `web_templates.h` — оновити дефолтні шаблони з real-IP фіксом
2. `DockerComposeProvider.cpp` — додати mod_remoteip в 00-load-modules
3. `DockerComposeProvider.cpp` + `.h` — додати `apply_web_template()`
4. `ServiceRegistry.cpp` — stop overwriting disk templates
5. API: `POST /api/profiles`, `PUT /api/profiles/<id>`, `DELETE /api/profiles/<id>`
6. API: `POST /api/sites/<id>/apply-template`
7. UI: templates.js — Create/Edit/Clone/Delete/Apply
8. Компіляція, тести, валідація
9. Commit + push
