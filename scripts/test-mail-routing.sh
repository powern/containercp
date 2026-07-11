#!/bin/bash
# Mail Module End-to-End Routing Test
# Usage: sudo SERVER_HOSTNAME=mail-test.local ./scripts/test-mail-routing.sh
set -euo pipefail

PASS=0
FAIL=0
GENERATED_DIR="/srv/containercp/mail/config/generated"

cleanup() {
    echo "=== Cleanup ==="
    pkill -f containercpd 2>/dev/null || true
    sleep 1
    docker rm -f containercp-mail-postfix containercp-mail-dovecot containercp-mail-redis 2>/dev/null || true
    docker network rm containercp-mail 2>/dev/null || true
}
trap cleanup EXIT

check() {
    local desc="$1"
    shift
    if "$@" 2>/dev/null; then
        echo "  ✅ $desc"
        PASS=$((PASS + 1))
    else
        echo "  ❌ $desc"
        FAIL=$((FAIL + 1))
    fi
}

# 1. Clean start
cleanup
rm -f /srv/containercp/containercpd.pid /srv/containercp/containercpd.sock
echo "1" > /srv/containercp/setup_completed 2>/dev/null || true
echo "${SERVER_HOSTNAME:-mail-test.local}" > /srv/containercp/server_hostname

cd /opt/containercp/build
SERVER_HOSTNAME="${SERVER_HOSTNAME:-mail-test.local}" nohup ./containercpd > /tmp/containercpd-e2e.log 2>&1 &
sleep 5

# 2. Activate module
echo "=== Activate ==="
check "Module activates" curl -sf -X POST http://127.0.0.1:8080/api/mail/activate

# 3. Check containers
echo "=== Containers ==="
sleep 3
check "Postfix running" docker ps --format '{{.Names}}' | grep -q containercp-mail-postfix
check "Dovecot running" docker ps --format '{{.Names}}' | grep -q containercp-mail-dovecot
check "Redis running" docker ps --format '{{.Names}}' | grep -q containercp-mail-redis

# 4. Health check
echo "=== Health ==="
HEALTH=$(curl -sf http://127.0.0.1:8080/api/mail/health)
check "Health endpoint returns success" echo "$HEALTH" | grep -q '"success":true'
check "All services ok" echo "$HEALTH" | grep -q '"status":"ok"'

# 5. Create domain + mailbox + alias
echo "=== Data setup ==="
check "Create domain" curl -sf -X POST http://127.0.0.1:8080/api/mail/domains \
  -H "Content-Type: application/json" \
  -d '{"domain":"e2e-test.local","mode":"local-primary","owner_id":1}' | grep -q '"success":true'
check "Create mailbox" curl -sf -X POST http://127.0.0.1:8080/api/mail/domains/1/mailboxes \
  -H "Content-Type: application/json" \
  -d '{"local_part":"alice","password":"testpass123"}' | grep -q '"success":true'
check "Create alias" curl -sf -X POST http://127.0.0.1:8080/api/mail/domains/1/aliases \
  -H "Content-Type: application/json" \
  -d '{"source":"info","destination":"alice@e2e-test.local"}' | grep -q '"success":true'

# 6. Config generation
echo "=== Config files ==="
check "transport_maps exists" test -f "$GENERATED_DIR/transport_maps"
check "virtual_aliases exists" test -f "$GENERATED_DIR/virtual_aliases"
check "Alias content correct" grep -q "info@e2e-test.local" "$GENERATED_DIR/virtual_aliases"
check "Postfix main.cf has virtual_alias_maps" grep -q "virtual_alias_maps" "$GENERATED_DIR/postfix-main.cf"

# 7. Port accessibility
echo "=== Ports ==="
check "SMTP port 25 on 127.0.0.1" timeout 3 bash -c 'echo "EHLO test" | nc -w 2 127.0.0.1 25' 2>&1 | grep -q "250"
check "IMAP port 143 on 127.0.0.1" timeout 3 bash -c 'echo "a1 LOGOUT" | nc -w 2 127.0.0.1 143' 2>&1 | grep -q "OK"
check "LMTP port 24 NOT on host" ! timeout 2 bash -c 'nc -z 127.0.0.1 24' 2>/dev/null

# 8. Postfix config validation via apply (triggers postfix check)
echo "=== Config validation ==="
check "Regenerate endpoint validates config" curl -sf -X POST http://127.0.0.1:8080/api/mail/regenerate | grep -q '"success":true'

# 9. Self-loop alias rejected
echo "=== Alias validation ==="
check "Self-loop alias rejected" curl -sf -X POST http://127.0.0.1:8080/api/mail/domains/1/aliases \
  -H "Content-Type: application/json" \
  -d '{"source":"alice","destination":"alice@e2e-test.local"}' | grep -q '"success":false'

# 10. Certificate check
echo "=== Certificate ==="
CERT_STATUS=$(curl -sf http://127.0.0.1:8080/api/mail/health | grep -o '"certificate":"[^"]*"' | cut -d'"' -f4)
check "Certificate status reported: $CERT_STATUS" test -n "$CERT_STATUS"

# Summary
echo ""
echo "=========================================="
echo "  Results: $PASS passed, $FAIL failed"
echo "=========================================="
exit $FAIL
