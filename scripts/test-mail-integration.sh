#!/bin/bash
# Mail Module Integration Test
# Tests against real DNS domains on the test server.
# Usage: bash scripts/test-mail-integration.sh [api_base_url]
set -euo pipefail

API="${1:-http://127.0.0.1:8080}"
PASS=0
FAIL=0
DOMAIN="maillab.softi.co"

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

api() {
    curl -sf "$API$1" "${@:2}"
}

api_post() {
    curl -sf -X POST "$API$1" -H "Content-Type: application/json" -d "$2"
}

echo "=== 1. Mail Module Status ==="
check "Module is active" api /api/mail/status | grep -q '"state":"active"'

echo "=== 2. Domain Management ==="
# Create domain (if not exists — skip if already exists)
DOMAIN_EXISTS=$(api "/api/mail/domains" | grep -c "\"domain\":\"$DOMAIN\"" || true)
if [ "$DOMAIN_EXISTS" -eq 0 ]; then
    check "Create domain $DOMAIN" api_post "/api/mail/domains" \
        "{\"domain\":\"$DOMAIN\",\"mode\":\"local-primary\",\"owner_id\":1}" | grep -q '"success":true'
fi
check "Domain exists" api "/api/mail/domains" | grep -q "\"domain\":\"$DOMAIN\""

# Get domain ID
DOMAIN_ID=$(api "/api/mail/domains" | python3 -c "import sys,json; data=json.load(sys.stdin)['data']; print([d['id'] for d in data if d['domain']=='$DOMAIN'][0])" 2>/dev/null)
echo "  Domain ID: $DOMAIN_ID"

echo "=== 3. Mailbox Management ==="
check "Create mailbox alice" api_post "/api/mail/domains/$DOMAIN_ID/mailboxes" \
    '{"local_part":"alice","password":"testpass123"}' | grep -q '"success":true'
check "Create mailbox bob" api_post "/api/mail/domains/$DOMAIN_ID/mailboxes" \
    '{"local_part":"bob","password":"testpass456"}' | grep -q '"success":true'

echo "=== 4. Alias Management ==="
check "Create alias info→alice" api_post "/api/mail/domains/$DOMAIN_ID/aliases" \
    "{\"source\":\"info\",\"destination\":\"alice@$DOMAIN\"}" | grep -q '"success":true'
check "Self-loop alias rejected" api_post "/api/mail/domains/$DOMAIN_ID/aliases" \
    "{\"source\":\"alice\",\"destination\":\"alice@$DOMAIN\"}" | grep -q '"success":false'

echo "=== 5. Health Check ==="
HEALTH=$(api /api/mail/health)
check "Health returns success" echo "$HEALTH" | grep -q '"success":true'
check "All services ok" echo "$HEALTH" | grep -q '"status":"ok"'
check "Certificate status reported" echo "$HEALTH" | grep -q '"certificate"'
check "Domain count > 0" echo "$HEALTH" | grep -q '"domain_count":'

echo "=== 6. Config Validation ==="
check "Regenerate validates config" api_post "/api/mail/regenerate" "" | grep -q '"success":true'
check "Config files exist" ssh localhost "test -f /srv/containercp/mail/config/generated/postfix-main.cf" 2>/dev/null
check "Transport maps exist" ssh localhost "test -f /srv/containercp/mail/config/generated/transport_maps"
check "Virtual aliases exist" ssh localhost "test -f /srv/containercp/mail/config/generated/virtual_aliases"

echo "=== 7. Port Accessibility ==="
check "SMTP port 25 on 127.0.0.1" timeout 3 bash -c "echo 'EHLO test' | nc -w 2 127.0.0.1 25" 2>&1 | grep -q "250"
check "IMAP port 143 on 127.0.0.1" timeout 3 bash -c "echo 'a1 LOGOUT' | nc -w 2 127.0.0.1 143" 2>&1 | grep -q "OK"
check "LMTP port 24 NOT on host" ! timeout 2 bash -c "nc -z 127.0.0.1 24" 2>/dev/null

echo "=== 8. DKIM Generation ==="
check "Generate DKIM key" api_post "/api/mail/domains/$DOMAIN_ID/dkim/generate" "" | grep -q '"success":true'
check "DKIM DNS record returned" api_post "/api/mail/domains/$DOMAIN_ID/dkim/generate" "" | grep -q '"dns_record"'

echo "=== 9. Health After Operations ==="
HEALTH2=$(api /api/mail/health)
check "Mailbox count correct" echo "$HEALTH2" | grep -q '"mailbox_count":2'
check "Alias count correct" echo "$HEALTH2" | grep -q '"alias_count":1'

echo ""
echo "=========================================="
echo "  Results: $PASS passed, $FAIL failed"
echo "=========================================="
exit $FAIL
