#!/bin/bash
# ContainerCP Self-Test
# Verifies normal startup, proxy health, recovery, and legacy migration.
# Must be run as root on the ContainerCP server.

set -e
PASS=0 FAIL=0
DATA_ROOT="${DATA_ROOT:-/srv/containercp}"
ADMIN_URL="${ADMIN_URL:-https://web2.softico.ua}"

info()  { echo "  [INFO] $*"; }
pass()  { echo "  [PASS] $*"; PASS=$((PASS + 1)); }
fail()  { echo "  [FAIL] $*"; FAIL=$((FAIL + 1)); }

cleanup() {
    echo ""
    echo "=== Cleanup ==="
    # Restore setup_completed if we corrupted it
    if [ -f "$DATA_ROOT/server_hostname" ] && [ "$(cat "$DATA_ROOT/server_hostname" 2>/dev/null)" != "" ]; then
        echo 1 > "$DATA_ROOT/setup_completed" 2>/dev/null || true
    fi
    # Ensure containercp-proxy is running
    docker start containercp-proxy 2>/dev/null || true
    sleep 2
}
trap cleanup EXIT

echo "============================================"
echo "  ContainerCP Self-Test"
echo "============================================"
echo ""

# ---- Test 1: daemon status ----
echo "--- Test 1: Daemon status ---"
if systemctl is-active containercpd > /dev/null 2>&1; then
    pass "containercpd is active"
else
    fail "containercpd is NOT active (try: systemctl start containercpd)"
fi

# ---- Test 2: port binding ----
echo "--- Test 2: Port binding ---"
RESTAPI=$(ss -ltnp | grep ':8080' | head -1)
WEBUI=$(ss -ltnp | grep ':8081' | head -1)
if echo "$RESTAPI" | grep -q '127.0.0.1:8080'; then
    pass "REST API on 127.0.0.1:8080"
else
    fail "REST API not found on 127.0.0.1:8080"
fi
if echo "$WEBUI" | grep -q ':8081'; then
    pass "Web UI on port 8081"
else
    fail "Web UI not found on port 8081"
fi

# ---- Test 3: admin URL ----
echo "--- Test 3: Admin URL ---"
if curl -skI "$ADMIN_URL" 2>/dev/null | grep -q '200\|302'; then
    pass "Admin URL $ADMIN_URL returns 200/302"
else
    fail "Admin URL $ADMIN_URL not reachable"
fi

# ---- Test 4: proxy container ----
echo "--- Test 4: Proxy container ---"
if docker ps --filter name=containercp-proxy --format '{{.Status}}' | grep -q 'Up'; then
    pass "containercp-proxy is running"
else
    fail "containercp-proxy is not running"
fi

# ---- Test 5: legacy setup_completed migration ----
echo "--- Test 5: Legacy setup_completed migration ---"
if [ -f "$DATA_ROOT/server_hostname" ] && [ "$(cat "$DATA_ROOT/server_hostname" 2>/dev/null)" != "" ]; then
    # Save current flag, corrupt it, restart, verify migration
    SAVED_FLAG=$(cat "$DATA_ROOT/setup_completed" 2>/dev/null || echo "missing")
    echo 0 > "$DATA_ROOT/setup_completed"
    sync
    systemctl restart containercpd
    sleep 3
    NEW_FLAG=$(cat "$DATA_ROOT/setup_completed" 2>/dev/null || echo "missing")
    if [ "$NEW_FLAG" = "1" ]; then
        pass "Legacy setup_completed migration restored flag to 1"
    else
        fail "setup_completed is '$NEW_FLAG', expected '1'"
    fi
    # Verify system is still up
    if systemctl is-active containercpd > /dev/null 2>&1; then
        pass "Daemon active after migration"
    else
        fail "Daemon NOT active after migration"
    fi
else
    info "No server_hostname configured — skipping migration test"
fi

# ---- Test 6: proxy recovery ----
echo "--- Test 6: Proxy recovery ---"
docker stop containercp-proxy
sleep 5
# Wait up to 90s for RecoveryManager to detect and recover
RECOVERED=false
for i in $(seq 1 90); do
    if docker ps --filter name=containercp-proxy --format '{{.Status}}' | grep -q 'Up'; then
        RECOVERED=true
        break
    fi
    sleep 1
done
if [ "$RECOVERED" = true ]; then
    pass "Proxy recovered automatically after docker stop"
else
    fail "Proxy did NOT recover within 90s"
fi

# Verify admin URL after recovery
sleep 3
if curl -skI "$ADMIN_URL" 2>/dev/null | grep -q '200\|302'; then
    pass "Admin URL $ADMIN_URL returns 200/302 after recovery"
else
    fail "Admin URL $ADMIN_URL not reachable after recovery"
fi

# ---- Summary ----
echo ""
echo "============================================"
echo "  Results: $PASS passed, $FAIL failed"
echo "============================================"

# Cleanup happens via trap
exit $FAIL
