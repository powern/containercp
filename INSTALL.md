# ContainerCP Installation Guide

ContainerCP is a container-native hosting control panel.

## Quick Install (Debian 13)

Run the installation script on a clean Debian 13 (Trixie) server:

```bash
curl -fsSL https://raw.githubusercontent.com/powern/containercp/main/scripts/install.sh | bash
```

Or clone the repository and run locally:

```bash
git clone https://github.com/powern/containercp.git
cd containercp
sudo ./scripts/install.sh
```

The script will:

1. Verify the OS is Debian 13
2. Install build dependencies (git, cmake, ninja, g++, curl)
3. Install Docker if missing
4. Install Docker Compose if missing
5. Clone or update the repository to `/opt/containercp`
6. Create all required data and configuration directories
7. Build ContainerCP in Release mode
8. Install binaries to `/usr/local/bin`
9. Install and enable the systemd service (`containercpd`)
10. Start the daemon
11. Print access URLs

## Updating

If ContainerCP was installed via the install script, run:

```bash
sudo ./scripts/update.sh
```

This will:

1. Pull the latest code via git
2. Rebuild in Release mode
3. Restart the systemd service

## Manual Build

```bash
apt update
apt install -y git cmake ninja-build g++ curl docker.io docker-compose-v2
git clone https://github.com/powern/containercp.git
cd containercp
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

## Systemd Service

ContainerCP runs as a systemd service. The service file is installed at
`/etc/systemd/system/containercp.service`.

Manage the service:

```bash
systemctl status containercpd    # Check status
systemctl restart containercpd   # Restart
systemctl stop containercpd      # Stop
journalctl -u containercpd -f    # Follow logs
```

The daemon is configured to:

- Start automatically on boot (`systemctl enable containercpd`)
- Restart on failure (up to 5 seconds delay)
- Log to systemd journal

## Single Instance

Only one daemon instance can run at a time. If you try to start a second
instance, it will exit immediately with a clear message. A PID file is
stored at `/srv/containercp/containercpd.pid`.

If the daemon crashes and the PID file is stale, remove the file and
restart the service.

## Startup Recovery

On every startup, the daemon automatically verifies:

- All required directories exist (database, sites, proxy, backups, etc.)
- The `containercp-public` Docker network exists
- The central proxy container (`containercp-proxy`) is running
- Proxy configuration directories are in place

Missing resources are recovered automatically — no manual intervention
required.

## Access

After installation:

| URL | Purpose |
|-----|---------|
| `http://127.0.0.1:8080/` | REST API + Web UI (local only) |
| `http://<server-ip>:8081/` | Web UI (external, login required) |

**First login:** Username: `admin`, Password: auto-generated (check the
daemon log with `journalctl -u containercpd`).

## File Locations

| Path | Purpose |
|------|---------|
| `/opt/containercp/` | Source code and build |
| `/usr/local/bin/containercpd` | Daemon binary |
| `/usr/local/bin/containercp` | CLI client binary |
| `/srv/containercp/` | Site data and persistent storage |
| `/srv/containercp/database/` | Pipe-delimited .db files |
| `/srv/containercp/sites/` | Per-site files and Docker stacks |
| `/srv/containercp/proxy/sites/` | Reverse proxy configs |
| `/srv/containercp/backups/` | Backup archives (.tar.gz) |
| `/etc/containercp/templates/` | Disk-based config templates |
| `/var/log/containercp/` | Log files |

## Troubleshooting

**Daemon won't start:**
```bash
journalctl -u containercpd -f --no-pager
```

**Port already in use:**
```bash
lsof -i :8080
```

**Docker not available:**
```bash
systemctl status docker
docker --version
```

**CLI cannot connect:**
```bash
ls -la /srv/containercp/containercpd.sock
systemctl status containercpd
```
