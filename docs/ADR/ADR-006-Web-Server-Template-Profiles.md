# ADR-006: Web Server Template Profiles

## Status

Accepted

## Context

ContainerCP currently hardcodes an nginx configuration inside
`DockerComposeProvider::create_site()`. This makes it impossible to:

- Switch between nginx and Apache
- Use different configurations for different stacks (WordPress, Laravel)
- Customize per-site web server settings

## Decision

Introduce `TemplateProfile` as a first-class resource. Each profile
defines:

- name (e.g., "nginx-php-default")
- web_server (e.g., "nginx", "apache")
- runtime (e.g., "docker")
- template_path
- description
- enabled
- default_profile

Profiles are persisted in `template_profiles.db` and seeded on first
run.

### Default profiles

- nginx-php-default (default)
- nginx-wordpress
- nginx-laravel
- apache-php-default
- apache-wordpress

### Template variables

All web server templates support:

- `{{DOMAIN}}` — site domain name
- `{{PUBLIC_ROOT}}` — path to public files
- `{{PHP_UPSTREAM}}` — PHP-FPM upstream address
- `{{LOG_ROOT}}` — log directory
- `{{SSL_ENABLED}}` — "true" or "false"

### Site create integration

```
containercp site create admin example.com --template nginx-wordpress
```

If `--template` is omitted, the default profile is used.

### Config generation

The hardcoded nginx config is removed from `DockerComposeProvider`.
Instead, the provider reads the selected profile, loads the template
file, renders it, and writes the result to `config/nginx/default.conf`
or `config/apache/default.conf`.

### Template storage

Templates are stored at `/etc/containercp/templates/web/`.
Created automatically if missing.

## Consequences

- New web server stacks can be added without code changes
- Existing sites use "nginx-php-default" for backward compatibility
- Template variables make configs flexible
- The `--template` CLI flag is optional
