#!/usr/bin/env bash
set -euo pipefail

# ContainerCP Installation Script
# Verified: Debian 13 (Trixie)
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/powern/containercp/main/scripts/install.sh | bash
#   # or locally:
#   ./scripts/install.sh

REPO_URL="https://github.com/powern/containercp.git"
INSTALL_DIR="/opt/containercp"
BIN_DIR="/usr/local/bin"
DATA_DIR="/srv/containercp"
CONFIG_DIR="/etc/containercp"
LOG_DIR="/var/log/containercp"
SERVICE_FILE="/etc/systemd/system/containercp.service"

echo "[SYSTEM] ContainerCP Installer — Debian 13"
echo "[SYSTEM] ========================================"

# --- 1. Verify OS ---
if [ ! -f /etc/os-release ]; then
    echo "[ERROR] Cannot detect OS. Debian 13 (Trixie) required."
    exit 1
fi
. /etc/os-release
if [ "$ID" != "debian" ] || [ "$VERSION_ID" != "13" ]; then
    echo "[ERROR] This installer requires Debian 13 (Trixie). Detected: $NAME $VERSION_ID"
    exit 1
fi
echo "[SYSTEM] OS: $NAME $VERSION_ID — OK"

# --- 2. Install system dependencies ---
echo "[SYSTEM] Installing build dependencies..."
apt-get update -qq
apt-get install -y -qq git cmake ninja-build g++ curl

# --- 3. Install Docker if missing ---
if ! command -v docker &>/dev/null; then
    echo "[SYSTEM] Installing Docker..."
    apt-get install -y -qq docker.io
    systemctl enable --now docker
else
    echo "[SYSTEM] Docker already installed: $(docker --version)"
fi

# --- 4. Install Docker Compose if missing ---
if ! docker compose version &>/dev/null && ! docker-compose version &>/dev/null; then
    echo "[SYSTEM] Installing Docker Compose..."
    apt-get install -y -qq docker-compose-v2
else
    echo "[SYSTEM] Docker Compose already installed"
fi

# --- 5. Clone or update repository ---
if [ -d "$INSTALL_DIR/.git" ]; then
    echo "[SYSTEM] Updating existing installation at $INSTALL_DIR..."
    cd "$INSTALL_DIR"
    git pull
else
    echo "[SYSTEM] Cloning ContainerCP to $INSTALL_DIR..."
    git clone "$REPO_URL" "$INSTALL_DIR"
    cd "$INSTALL_DIR"
fi

# --- 6. Create required directories ---
echo "[SYSTEM] Creating data directories..."
mkdir -p "$DATA_DIR/database"
mkdir -p "$DATA_DIR/sites"
mkdir -p "$DATA_DIR/proxy/sites"
mkdir -p "$DATA_DIR/backups"
mkdir -p "$DATA_DIR/users"
mkdir -p "$CONFIG_DIR/templates/web"
mkdir -p "$LOG_DIR"

# --- 7. Build ---
echo "[SYSTEM] Building ContainerCP (Release)..."
cmake -S "$INSTALL_DIR" -B "$INSTALL_DIR/build-release" -DCMAKE_BUILD_TYPE=Release
cmake --build "$INSTALL_DIR/build-release"

# --- 8. Install binaries ---
echo "[SYSTEM] Installing binaries..."
cp "$INSTALL_DIR/build-release/containercpd" "$BIN_DIR/containercpd"
cp "$INSTALL_DIR/build-release/containercp" "$BIN_DIR/containercp"
chmod 755 "$BIN_DIR/containercpd" "$BIN_DIR/containercp"

# --- 9. Install systemd service ---
echo "[SYSTEM] Installing systemd service..."
cp "$INSTALL_DIR/packaging/containercp.service" "$SERVICE_FILE"
systemctl daemon-reload

# --- 10. Enable and start service ---
echo "[SYSTEM] Enabling and starting containercpd..."
systemctl enable containercpd
systemctl restart containercpd

# --- 11. Wait and verify ---
sleep 2
if systemctl is-active --quiet containercpd; then
    echo "[SYSTEM] containercpd is running"
else
    echo "[WARN] containercpd may not have started. Check: systemctl status containercpd"
fi

# --- 12. Print URLs ---
HOST_IP=$(ip -4 addr show | grep -oP 'inet \K[\d.]+' | grep -v '127.0.0.1' | head -1 || echo "<server-ip>")
echo ""
echo "[SYSTEM] ========================================"
echo "[SYSTEM] ContainerCP installation complete!"
echo "[SYSTEM] ========================================"
echo ""
echo "  Local API:   http://127.0.0.1:8080/"
echo "  Web UI:      http://${HOST_IP}:8081/"
echo "  API Health:  curl http://127.0.0.1:8080/api/health"
echo ""
echo "  First login (Web UI):"
echo "    Username: admin"
echo "    Password: auto-generated (check daemon log)"
echo ""
echo "  Manage service:"
echo "    systemctl status containercpd"
echo "    systemctl restart containercpd"
echo "    journalctl -u containercpd -f"
echo ""
echo "[SYSTEM] Install completed successfully."
