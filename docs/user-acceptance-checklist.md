# User Acceptance Checklist — PHP Mail Integration

**Target:** `unity.softico.ua` (production)
**Date:** _______________
**Tested by:** _______________

---

## Prerequisites

- [ ] Stage 9 deployment completed (binary updated, daemon running)
- [ ] `POST /api/mail/regenerate` executed successfully
- [ ] Mail containers are healthy:
      ```
      containercp-mail-postfix   Up
      containercp-mail-dovecot   Up
      containercp-mail-rspamd    Up
      containercp-mail-redis     Up
      ```

---

## 1. Web GUI — Enable PHP Mail

- [ ] Open ContainerCP Web GUI
- [ ] Navigate to Sites → unity.softico.ua
- [ ] Verify **PHP Mail** card is visible below the existing cards
- [ ] Card shows: `● Disabled`, all status indicators ❌
- [ ] Click **Enable PHP Mail**
- [ ] Confirm the dialog
- [ ] Wait for status refresh
- [ ] **Expected:** Card shows `● Enabled` (green), all indicators ✅
- [ ] Screenshot the status card: ___________

---

## 2. Web GUI — Mail Status Verification

After enable, verify each component:

| Component | Expected | Actual |
|-----------|----------|--------|
| Status | `● Enabled` (green) | |
| Credentials | ✅ | |
| msmtprc | ✅ | |
| Network | ✅ | |

- [ ] If any ❌, note which: ___________

---

## 3. Technical: SMTP AUTH Test (via API)

```bash
curl -X POST http://localhost:8080/api/sites/{SITE_ID}/send-test-email \
  -H "Content-Type: application/json" \
  -d '{"to": "admin@softico.ua"}'
```

- [ ] Response: `{"success":true,"data":{"message":"Test email sent to admin@softico.ua"}}`
- [ ] Check Postfix queue: `docker exec containercp-mail-postfix postqueue -p`
- [ ] Expected: mail is queued (not rejected)

---

## 4. PHP mail() Test

Test PHP's built-in `mail()` function directly:

```bash
# Find the PHP container
docker exec site-{ID}-php php -r '
  $to = "admin@softico.ua";
  $subject = "PHP mail() test from unity.softico.ua";
  $message = "This is a test from PHP mail()\nTime: " . date("Y-m-d H:i:s");
  $headers = "From: wordpress@unity.softico.ua";
  if (mail($to, $subject, $message, $headers)) {
    echo "mail() returned true\n";
  } else {
    echo "mail() returned false\n";
  }
'
```

- [ ] `mail()` returns `true`
- [ ] Email appears in Postfix queue: `postqueue -p`
- [ ] Queue ID: ___________

---

## 5. WordPress wp_mail() Test

1. Open `https://unity.softico.ua/wp-admin` in browser
2. Navigate to Users → Profile or trigger a Password Reset:
   ```
   https://unity.softico.ua/wp-login.php?action=lostpassword
   ```
3. Enter the admin email address
4. Submit
- [ ] Password reset email sent successfully (green message in WordPress)
- [ ] Email appears in Postfix queue:
      ```bash
      docker exec containercp-mail-postfix postqueue -p
      ```
- [ ] Queue ID: ___________
- [ ] Check email headers:
      - `From:` matches `wordpress@unity.softico.ua` or admin email
      - `DKIM-Signature:` present (if DKIM configured)
      - `SPF:` pass (if DNS configured)

---

## 6. Sender Restrictions Verification

Test that sender restrictions are active:

```bash
# This should succeed (exact match)
curl -X POST http://localhost:8080/api/sites/{SITE_ID}/send-test-email \
  -H "Content-Type: application/json" \
  -d '{"to": "admin@softico.ua"}'
```

- [ ] ✅ Test email succeeds (envelope sender matches allowed pattern)

**Expected behaviour with sender restrictions active:**
- Envelope sent as `wordpress@unity.softico.ua` → ✅ allowed (exact match)
- Attempt to send as `admin@other.com` → ❌ rejected (553)

---

## 7. Disable PHP Mail via GUI

- [ ] Open ContainerCP Web GUI → Sites → unity.softico.ua
- [ ] Click **Disable PHP Mail**
- [ ] Confirm the dialog ("PHP mail() and wp_mail() will stop working")
- [ ] Wait for status refresh
- [ ] **Expected:** Card shows `● Disabled` (gray), all indicators ❌
- [ ] Verify msmtprc is removed:
      ```bash
      ls /srv/containercp/sites/unity.softico.ua/config/php/msmtprc
      # Expected: "No such file or directory"
      ```
- [ ] Verify network disconnected:
      ```bash
      docker inspect site-{ID}-php --format '{{range $k,$v:=.NetworkSettings.Networks}}{{$k}} {{end}}'
      # Expected: "containercp-mail" NOT present
      ```
- [ ] Verify credentials removed:
      ```bash
      cat /srv/containercp/mail/config/generated/passwd-php | grep unity
      # Expected: no output
      ```

---

## 8. Re-Enable PHP Mail via GUI

- [ ] Click **Enable PHP Mail** again
- [ ] Verify all indicators return to ✅
- [ ] Send test email again → ✅ succeeds
- [ ] Expected: idempotent — same result as first enable

---

## 9. Runtime Upgrade Recovery Test

Simulate recovery scenario:

```bash
# Remove msmtprc
sudo rm /srv/containercp/sites/unity.softico.ua/config/php/msmtprc

# Disconnect network
docker network disconnect containercp-mail site-{ID}-php

# Restart daemon
sudo systemctl restart containercp
sleep 5

# Check if msmtprc and network are restored
ls /srv/containercp/sites/unity.softico.ua/config/php/msmtprc
# Expected: file exists (restored)

docker inspect site-{ID}-php --format '{{range $k,$v:=.NetworkSettings.Networks}}{{$k}} {{end}}'
# Expected: "containercp-mail" present
```

- [ ] msmtprc restored
- [ ] Network reconnected
- [ ] Test email still works

---

## 10. Negative Scenarios

| Scenario | Expected | Actual |
|----------|----------|--------|
| Enable mail for already-enabled site | ✅ Idempotent — no error | |
| Disable mail for disabled site | ✅ No error (no-op) | |
| send-test-email with invalid email | ❌ 400 Invalid email | |
| send-test-email without PHP container | ❌ 500 Container not found | |
| curl to API without session token | ❌ 401 Unauthorized | |

---

## 11. Logging & Monitoring

- [ ] Postfix log shows submission activity:
      ```bash
      docker logs containercp-mail-postfix | grep submission
      ```
- [ ] Dovecot log shows SASL auth:
      ```bash
      docker logs containercp-mail-dovecot | grep "auth"
      ```
- [ ] PHP error log shows no mail() failures:
      ```bash
      tail -20 /srv/containercp/sites/unity.softico.ua/logs/php/error.log
      ```

---

## 12. Final Verification

- [ ] `unity.softico.ua` loads in browser (HTTP 200)
- [ ] WordPress admin works (wp-admin)
- [ ] Login form works
- [ ] Password reset email is received
- [ ] SnappyMail still works at `/webmail/`
- [ ] DKIM signing still present on outbound mail
- [ ] Other sites on the same server unaffected

---

## Signature

- [ ] All tests passed
- [ ] Issues found (list): _________________________________
- [ ] Ready for production: YES / NO
