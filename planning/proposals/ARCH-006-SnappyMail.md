# ARCH-006: SnappyMail Webmail Integration

Status: Draft

## Problem

ContainerCP has a full mail stack (Postfix, Dovecot, Rspamd, Redis) but no
webmail interface. Users must configure external IMAP/SMTP clients to read
and send mail. This creates friction for non-technical users.

## Motivation

- **User experience**: webmail is expected for any mail hosting platform
- **Product Vision goal**: "container-oriented hosting control panel for
  system administrators, developers, and hosting providers" — webmail is
  a standard feature for hosting providers
- **Competitive parity**: cPanel, Plesk, and similar panels all include
  webmail

## Current Architecture

The mail stack runs as a Docker Compose project with 4 containers:

| Container | Image | Purpose |
|-----------|-------|---------|
| postfix | ghcr.io/containercp/mail-postfix | SMTP server |
| dovecot | ghcr.io/containercp/mail-dovecot | IMAP/POP3 server |
| rspamd | ghcr.io/containercp/mail-rspamd | Spam filter + DKIM signing |
| redis | redis:7-alpine | Cache |

All containers share the `containercp-mail` Docker network.

The central nginx proxy (`containercp-proxy`) proxies the admin panel
(web2.softico.ua → :8081) and all hosted sites via per-domain configs.

## Proposed Architecture

### Docker container: SnappyMail

Add a 5th container to the mail Compose project:

| Container | Image | Purpose |
|-----------|-------|---------|
| snappymail | ghcr.io/containercp/mail-snappymail | Webmail client |

SnappyMail connects to:
- **IMAP**: Dovecot (`containercp-mail-dovecot:143`)
- **SMTP**: Postfix with STARTTLS auth (`containercp-mail-postfix:587`)
- **Redis**: cache session data (`containercp-mail-redis:6379`)
- Network: `containercp-mail` (access to all mail services)

### Proxy integration

SnappyMail is accessible at `https://<server-hostname>/webmail/` via the
central nginx proxy. The `/webmail/` location is added to the admin panel's
server block in `ProxyConfigBuilder`.

Nginx config addition:
```nginx
location /webmail/ {
    proxy_pass http://containercp-mail-snappymail:80/;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto https;
}
```

### Web UI

Add a "Webmail" link in the admin panel sidebar that opens `/webmail/`
in the same browser tab.

### Docker image

Base: `php:8.4-fpm-alpine` (~30MB) + `nginx:alpine` (~25MB)
Total image: ~55MB.

SnappyMail is installed from the official release tarball. Configuration
is generated at container start with hardcoded IMAP/SMTP pointing to
Dovecot and Postfix containers.

## New Resources

None. SnappyMail does not use ContainerCP's resource model. It is a
stateless webmail client.

## Managers

None. SnappyMail is managed purely as a Docker container within the
existing mail Compose project.

## Storage

SnappyMail uses its own SQLite database for user settings. This is
stored in a Docker named volume so data persists across restarts.

## Providers

### Docker image build

```bash
docker build -t ghcr.io/containercp/mail-snappymail:latest \
    -f docker/mail/Dockerfile.snappymail docker/mail/
```

### Dockerfile

- Base: `alpine:3.20`
- Packages: nginx, php84, php84-fpm, php84-json, php84-session,
  php84-pdo, php84-pdo_sqlite, php84-sqlite3, php84-openssl,
  php84-mbstring, php84-xml, php84-curl
- SnappyMail: downloaded and extracted to `/var/www/snappymail`
- Config: `data/_data_/_default_/configs/` generated with IMAP/SMTP
  settings pointing to Docker service names
- nginx: configured with `root /var/www/snappymail`, index index.php,
  PHP-FPM passthrough
- Entrypoint: starts PHP-FPM, then nginx, then tails nginx log

## REST API

No new API endpoints. SnappyMail has its own login page.

### Modified endpoints

| Method | Path | Change |
|--------|------|--------|
| GET /api/health | extend with snappymail service status | Added to health check |

## Web UI

- Add "Webmail" link in sidebar (between Mail and Proxy)
- Link points to `/webmail/` (same origin, handled by nginx proxy)

## CLI

No new CLI commands.

## Configuration

| Setting | Value | Source |
|---------|-------|--------|
| IMAP host | containercp-mail-dovecot | hardcoded in entrypoint |
| IMAP port | 143 | hardcoded in entrypoint |
| SMTP host | containercp-mail-postfix | hardcoded in entrypoint |
| SMTP port | 587 | hardcoded in entrypoint |
| SMTP auth | required | hardcoded in entrypoint |
| SMTP STARTTLS | required | hardcoded in entrypoint |

## Migration Strategy

No migration needed. SnappyMail is purely additive.

## Backward Compatibility

Fully backward compatible:
- Existing mail stack continues unchanged
- Existing API endpoints unchanged
- Existing storage format unchanged
- New `/webmail/` location only added if SnappyMail container is running

## Rejected Alternatives

### Roundcube
- Heavier image (~150MB vs ~55MB)
- Requires MySQL/MariaDB (extra dependency)
- More complex configuration

### RainLoop
- Discontinued upstream
- Security concerns with unmaintained code

### Dedicated subdomain (e.g., webmail.example.com)
- Requires DNS changes for every mail domain
- More SSL certificates needed
- More complex routing

## Risks

| Risk | Mitigation |
|------|------------|
| SnappyMail upstream changes | Pin version in Dockerfile |
| Session security | Use HTTPS-only cookies; SnappyMail's native CSRF protection |
| Cross-origin auth | All requests go through same origin (/webmail/) |
| Resource usage | PHP-FPM processes limited to 2-5 children |

## Validation Plan

1. Build Docker image: `make -f docker/mail/Dockerfile.snappymail`
2. Verify SnappyMail starts and is accessible on `containercp-mail` network
3. Test login with existing mailbox (admin@maillab.softi.co / test123)
4. Test send/receive via SnappyMail → Dovecot → Postfix → Gmail
5. Verify `/webmail/` location works via nginx proxy
6. Verify health check includes snappymail container
