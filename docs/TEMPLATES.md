# Web Server Templates

## Location

Templates are stored on disk at:

```
/srv/containercp/templates/web/
```

Each template profile has a corresponding file:

```
nginx-php-default.conf.template
nginx-wordpress.conf.template
nginx-laravel.conf.template
apache-php-default.conf.template
apache-wordpress.conf.template
```

## How to edit

1. Find the template file in `/srv/containercp/templates/web/`
2. Edit with any text editor
3. Run `containercp template reload` to apply changes
4. Run `containercp template validate <name>` to verify

No recompilation is needed. Changes take effect after `template reload`.

## Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `{{DOMAIN}}` | Site domain name | `example.com` |
| `{{PUBLIC_ROOT}}` | Path to public web root | `/var/www/html` |
| `{{PHP_UPSTREAM}}` | PHP-FPM upstream address | `php:9000` |
| `{{LOG_ROOT}}` | Log directory | `/var/log` |
| `{{SSL_ENABLED}}` | Whether SSL is enabled | `true` / `false` |

## Validation

To validate a template:

```
containercp template validate nginx-php-default
```

This checks:

- The profile exists
- The template file exists on disk
- The file is not empty
- All required variables are present

## Default templates

When a template file does not exist, ContainerCP creates it with
default content. Existing files are never overwritten.

## How it works

1. `DockerComposeProvider` reads the active template profile
2. Loads the template file from disk
3. Replaces variables with site-specific values
4. Writes the rendered config to `config/<web_server>/default.conf`
