#!/usr/bin/env bash
set -euo pipefail

# ContainerCP Update Script
#
# Performs:
#   git pull
#   cmake configure
#   cmake build
#   stop systemd service
#   copy binaries
#   start systemd service
#   verify health endpoint
#
# Usage:
#   ./scripts/update.sh

INSTALL_DIR="/opt/containercp"
BIN_DIR="/usr/local/bin"
SERVICE="containercpd"
API_PORT="${CONTAINERCP_API_PORT:-8080}"

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

# --- 2. Ensure build dependencies ---
echo "[SYSTEM] Ensuring build dependencies..."
if command -v apt-get >/dev/null 2>&1; then
    apt-get update -qq
    apt-get install -y -qq git cmake ninja-build g++ curl libsqlite3-dev
else
    echo "[WARN] apt-get not found; skipping automatic dependency installation."
fi

# --- 3. Configure ---
echo "[SYSTEM] Configuring build..."
cmake -S "$INSTALL_DIR" -B "$INSTALL_DIR/build-release" -DCMAKE_BUILD_TYPE=Release

# --- 4. Build ---
echo "[SYSTEM] Building..."
cmake --build "$INSTALL_DIR/build-release"

# --- 5. Build ContainerCP PHP image if available ---
PHP_IMAGE="ghcr.io/powern/containercp-php:8.4"
if [ -f "$INSTALL_DIR/docker/php/Dockerfile" ]; then
    echo "[SYSTEM] Building ContainerCP PHP image..."
    if docker image inspect "$PHP_IMAGE" >/dev/null 2>&1; then
        echo "[SYSTEM] PHP image $PHP_IMAGE already exists, rebuilding..."
    fi
    docker build \
        -t "$PHP_IMAGE" \
        --build-arg PHP_VERSION=8.4 \
        -f "$INSTALL_DIR/docker/php/Dockerfile" \
        "$INSTALL_DIR/docker/php/" 2>&1 | tail -5
    echo "[SYSTEM] PHP image built: $PHP_IMAGE"
fi

# --- 6. Stop service before installing ---
echo "[SYSTEM] Stopping containercpd..."
systemctl stop "$SERVICE" 2>/dev/null || true

# --- 7. Install binaries ---
echo "[SYSTEM] Installing updated binaries..."
cp "$INSTALL_DIR/build-release/containercpd" "$BIN_DIR/containercpd"
cp "$INSTALL_DIR/build-release/containercp" "$BIN_DIR/containercp"
chmod 755 "$BIN_DIR/containercpd" "$BIN_DIR/containercp"

# --- 8. Start service ---
echo "[SYSTEM] Starting containercpd..."
systemctl daemon-reload
systemctl start "$SERVICE"

# --- 9. Wait for daemon and verify health ---
echo "[SYSTEM] Waiting for daemon to become ready..."
for i in $(seq 1 10); do
    sleep 1
    if curl -sf "http://127.0.0.1:${API_PORT}/api/health" >/dev/null 2>&1; then
        echo "[SYSTEM] Health check passed."
        break
    fi
    if [ "$i" -eq 10 ]; then
        echo "[WARN] Health check did not pass within 10 seconds."
        echo "       Check: journalctl -u containercpd -f"
    fi
done

# --- 10. Show status ---
if systemctl is-active --quiet "$SERVICE"; then
    echo "[SYSTEM] Update complete. containercpd is running."
    systemctl status "$SERVICE" --no-pager 2>&1 | head -20
else
    echo "[WARN] containercpd may not have started. Check: journalctl -u containercpd -f"
fi
