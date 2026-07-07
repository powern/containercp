# BUG-012: Created site missing nginx config file

## Severity
Critical

## Description

Site creation from Web UI succeeds (containers start) but nginx
container cannot serve HTTP because the nginx config file at
`config/nginx/default.conf` was never generated. The docker-compose
volume mount maps `./config/nginx:/etc/nginx/conf.d`, but the
directory is empty.

## Root cause

`DockerComposeProvider::create_site()` generates the nginx config
from a WEB_SERVER profile template. If the profile is null or the
template file doesn't exist on disk, no config is generated — but
`docker compose up` still runs. The fallback path was missing.

The template file is created by the ServiceRegistry profile seeding
code only when `profiles.db` is empty. On subsequent starts, if
`profiles.db` exists but the template files were wiped (e.g., by
cleaning `/etc/containercp/templates/`), the seeding is skipped and
the templates are not recreated.

## Fix

1. Added fallback config generation in `DockerComposeProvider`:
   If profile or template is unavailable, generate a hardcoded
   default nginx config that works (listen 80, php upstream,
   standard location blocks).

2. Added validation after config generation:
   If the expected `config/nginx/default.conf` file does not exist
   after both the profile and fallback attempts, site creation fails
   cleanly with an error message before `docker compose up`.

3. Added tests:
   - Default web template produces valid nginx config
   - Default nginx profile has correct metadata (web_server="nginx")
   - Site nginx config path structure matches compose mount

## Files changed

- `libs/provider/DockerComposeProvider.cpp` — fallback config + validation
- `tests/test_template.cpp` — config generation and path structure tests
