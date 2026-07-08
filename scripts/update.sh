#!/usr/bin/env bash
set -euo pipefail

# ContainerCP Update Script
#
# Performs:
#   git pull
#   cmake configure
#   cmake build
#   restart systemd service
#
# Usage:
#   ./scripts/update.sh

INSTALL_DIR="/opt/containercp"
BIN_DIR="/usr/local/bin"
SERVICE="containercpd"

echo "[SYSTEM] ContainerCP Updater"
echo "[SYSTEM] ========================================"

if [ ! -d "$INSTALL_DIR/.git" ]; then
    echo "[ERROR] No git repository found at $INSTALL_DIR"
    echo "        This script only works with git-based installations."
    exit 1
fi

# --- 1. Pull latest ---
echo "[SYSTEM] Pulling latest code..."
cd "$INSTALL_DIR"
git pull

# --- 2. Configure ---
echo "[SYSTEM] Configuring build..."
cmake -S "$INSTALL_DIR" -B "$INSTALL_DIR/build-release" -DCMAKE_BUILD_TYPE=Release

# --- 3. Build ---
echo "[SYSTEM] Building..."
cmake --build "$INSTALL_DIR/build-release"

# --- 4. Install binaries ---
echo "[SYSTEM] Installing updated binaries..."
cp "$INSTALL_DIR/build-release/containercpd" "$BIN_DIR/containercpd"
cp "$INSTALL_DIR/build-release/containercp" "$BIN_DIR/containercp"
chmod 755 "$BIN_DIR/containercpd" "$BIN_DIR/containercp"

# --- 5. Restart service ---
echo "[SYSTEM] Restarting containercpd..."
systemctl daemon-reload
systemctl restart "$SERVICE"

sleep 2
if systemctl is-active --quiet "$SERVICE"; then
    echo "[SYSTEM] Update complete. containercpd is running."
else
    echo "[WARN] containercpd may not have started. Check: journalctl -u containercpd -f"
fi
