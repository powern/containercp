# ContainerCP Installation Guide

This guide is written for system administrators who want to install
ContainerCP on a Debian 13 (Trixie) server.

## Prerequisites

**Operating system:** Debian 13 (Trixie)

**Packages:**

```
apt update
apt install -y git cmake ninja-build g++ curl
```

**Docker and Docker Compose:**

Debian 13 (Trixie):
```
apt install -y docker.io docker-compose
systemctl enable --now docker
```

**Verify:**

```
docker --version
docker compose version
```

ContainerCP supports both `docker compose` (plugin) and
`docker-compose` (standalone binary). It detects the
available command automatically at runtime. Debian 13 ships both.

## Build

Clone the repository and build the Release binaries:

```
git clone https://github.com/powern/containercp.git
cd containercp
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

After building, verify the binaries exist:

```
ls -la build-release/containercpd
ls -la build-release/containercp
```

Expected:
- `containercpd` — the daemon binary (~1.1 MB)
- `containercp` — the CLI client binary (~100 KB)

## Start the daemon

```
./build-release/containercpd
```

The daemon will:

1. Create the data directory at `/srv/containercp/`
2. Seed default resources (admin user, local node, PHP versions, profiles)
3. Start the REST API on `http://127.0.0.1:8080`
4. Start the UNIX socket at `/srv/containercp/containercpd.sock`

Verify the daemon is running:

```
curl http://127.0.0.1:8080/api/health
```

Expected response:
```json
{"success":true,"data":{"status":"ok"}}
```

## First login

The admin user is created automatically on first start.

List users:

```
./build-release/containercp user list
```

Expected output:
```
admin
```

## Creating your first website

```
./build-release/containercp site create admin example.com
```

This will:

1. Create a site record
2. Create a domain record
3. Create a database with random credentials
4. Generate a docker-compose.yml
5. Generate nginx configuration
6. Start the Docker Compose stack

Verify the site is running:

```
./build-release/containercp site list
```

Expected output:
```
example.com
```

## Web UI

The daemon starts two HTTP listeners:

| Port | Bind | Purpose | Access |
|------|------|---------|--------|
| 8080 | 127.0.0.1 | REST API + Web UI | Local access only |
| 8081 | 0.0.0.0 | Web UI with API proxy | External network |

### Local access (recommended)

Open in browser:

```
http://127.0.0.1:8080/
```

No authentication required for local access.

### External access

Open in browser:

```
http://<server-ip>:8081/
```

The external Web UI requires a login with username and password:

- **Username:** `admin`
- **Password:** Generated on first daemon start, printed to the daemon log.
  The password is hashed and stored in `/srv/containercp/database/auth_users.db`.
  On first login, you will be required to change the temporary password.

The API proxy (`/ui-api/...`) forwards requests to the internal REST
API on `127.0.0.1:8080`. The raw `/api/...` paths are explicitly
rejected on port 8081 for security.

### SSH forwarding (alternative)

For local access without authentication:

```
ssh -L 8080:127.0.0.1:8080 user@<server>
```

Then open `http://127.0.0.1:8080/` on your local machine.

### Production reverse proxy

For production, set up nginx or Apache to serve the static files
from `/opt/containercp/web/` and proxy `/ui-api/*` to
`http://127.0.0.1:8080`. The public Web UI port (8081) is designed
for development and small deployments.

## SSL certificates

Request a Let's Encrypt certificate (placeholder):

```
./build-release/containercp ssl request example.com
```

## Backups

Create a backup:

```
./build-release/containercp backup create example.com
```

List backups:

```
./build-release/containercp backup list
```

Restore a backup:

```
./build-release/containercp backup restore <id>
```

## CLI commands

Run `containercp --help` for a complete list of commands.

Key commands:

| Command | Description |
|---------|-------------|
| `containercp node list` | List nodes |
| `containercp user list` | List users |
| `containercp site list` | List sites |
| `containercp domain list` | List domains |
| `containercp database list` | List databases |
| `containercp backup list` | List backups |
| `containercp ssl list` | List SSL certificates |
| `containercp proxy list` | List proxy configs |
| `containercp template list` | List template profiles |

## File locations

| Path | Purpose |
|------|---------|
| `/opt/containercp/` | Source code |
| `/srv/containercp/` | Site data and storage |
| `/srv/containercp/database/` | Persistent storage (.db files) |
| `/srv/containercp/sites/<domain>/` | Per-site files |
| `/srv/containercp/proxy/sites/` | Reverse proxy configs |
| `/srv/containercp/backups/` | Backup archives |
| `/etc/containercp/templates/` | Configuration templates |
| `/var/log/containercp/` | Log files |

## Troubleshooting

**Daemon won't start:**
Check if port 8080 is already in use: `lsof -i :8080`
Check the log output for error messages.

**CLI cannot connect:**
Ensure the daemon is running: `pgrep containercpd`
Check the socket file: `ls -la /srv/containercp/containercpd.sock`

**Docker stack fails:**
Ensure Docker is running: `docker info`
Try running `docker compose up -d` in the site directory.

**Web UI not loading:**
Ensure the daemon is running.
Check `curl http://127.0.0.1:8080/api/health` returns a valid response.

## Uninstalling

Stop the daemon:

```
kill $(pgrep containercpd)
```

Remove data (WARNING: deletes all sites and databases):

```
rm -rf /srv/containercp
```

Remove the build:

```
rm -rf /opt/containercp/build-release
```
