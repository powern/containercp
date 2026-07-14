# User Acceptance Checklist — PHP Mail Integration

**Target:** `unity.softico.ua` (production)
**Date:** _______________
**Tested by:** _______________

## How to use

This checklist is for **manual user testing**. Technical API/SMTP tests
are performed automatically by OpenCode during deployment. The user only
needs to work through the Web GUI and WordPress sections.

---

## 1. Prerequisites

- [ ] Stage 9 deployment completed (OpenCode confirms)
- [ ] Mail stack regeneration complete (OpenCode confirms)
- [ ] Mail containers healthy:
      ```
      containercp-mail-postfix   Up
      containercp-mail-dovecot   Up
      containercp-mail-rspamd    Up
      containercp-mail-redis     Up
      ```

---

## 2. Web GUI — Enable PHP Mail

- [ ] Open ContainerCP Web GUI in browser
- [ ] Navigate to **Sites** → **unity.softico.ua**
- [ ] Scroll down — **PHP Mail** card should be visible below the other cards
- [ ] Card displays: `● Disabled` (gray), all indicators ❌
- [ ] Click **Enable PHP Mail**
- [ ] Confirm the dialog ("Enable PHP mail for unity.softico.ua?")
- [ ] Wait 2–3 seconds for status refresh
- [ ] **Expected:** Card shows `● Enabled` (green), all three indicators ✅
- [ ] If any ❌ remains, note which: ___________

---

## 3. Web GUI — Mail Status Check

| Component | Expected | Actual |
|-----------|----------|--------|
| Status | `● Enabled` (green) | |
| Credentials | ✅ | |
| msmtprc | ✅ | |
| Network | ✅ | |

---

## 4. Technical tests (OpenCode runs these automatically)

After the user enables PHP Mail via GUI, the following are verified
automatically by OpenCode:

- **SMTP AUTH test:** msmtp → Postfix submission → Dovecot SASL
- **Sender restrictions test:** correct envelope sender accepted,
  wrong envelope sender rejected (553)
- **PHP mail() test:** executed inside the PHP container
- **Postfix queue:** mail accepted (queue ID recorded)
- **TLS:** certificate with SAN verified

Results are reported in the final section.

---

## 5. WordPress wp_mail() Test (manual)

1. Open `https://unity.softico.ua/wp-login.php?action=lostpassword`
2. Enter the WordPress admin email address
3. Click "Get New Password" or equivalent
4. **Expected:** WordPress shows: "Password reset email sent"
5. Check the mailbox for the password reset email
   - **Note:** Postfix queue may already be empty if delivery was fast.
     Check the actual mailbox, not the queue.

---

## 6. Email Content Verification

Once the password reset email is received:

- [ ] `From:` header is `wordpress@unity.softico.ua` or similar
- [ ] Subject contains website name
- [ ] DKIM-Signature header is present (if DKIM configured)
- [ ] Plain text password reset link works
- [ ] Message arrives within 30 seconds

---

## 7. Re-Enable Test (optional)

- [ ] In GUI, click **Disable PHP Mail**
- [ ] Confirm the dialog
- [ ] Card shows `● Disabled` (gray)
- [ ] Click **Enable PHP Mail** again
- [ ] Card returns to `● Enabled` (green)
- [ ] All indicators ✅

---

## 8. Container Recreate Test (optional, not on production)

To verify network persistence after container recreate:

```bash
# In a controlled environment:
docker compose -f /srv/containercp/sites/unity.softico.ua/docker-compose.yml up -d php
# Check that containercp-mail network is still connected
```

**Note:** This test is OPTIONAL. It requires restarting the PHP container,
which causes brief downtime for the site. Only perform if explicitly
requested.

---

## 9. Runtime Recovery Test — ⚠️ DESTRUCTIVE — DO NOT RUN ON PRODUCTION

This test removes files and restarts the daemon. It is meant for
staging/QA environments only.

**Skip this test for production acceptance.**
Runtime upgrade recovery was verified during pre-implementation testing.

---

## 10. Negative Scenarios

| Test | Expected | Actual |
|------|----------|--------|
| Click Enable when already enabled | ✅ Idempotent — no error | |
| Click Disable when already disabled | ✅ No error | |
| Access GUI without login | ❌ Redirected to login | |

---

## 11. Other Services — Unaffected

Verify that enabling PHP Mail did NOT break other functionality:

- [ ] `unity.softico.ua` loads in browser (HTTP 200)
- [ ] WordPress admin (`/wp-admin`) works
- [ ] `/contact/` page works (if exists)
- [ ] Other sites on the same server still work
- [ ] SnappyMail (`/webmail/`) still works
- [ ] DKIM signing on existing mail still present

---

## 12. Final Result

| Test | Result |
|------|--------|
| GUI Enable PHP Mail | |
| GUI Status — all ✅ | |
| WordPress wp_mail() | |
| Email received | |
| DKIM/SPF headers | |
| Other services unaffected | |
| Issues found | |

**Signed off by:** _______________

**Date:** _______________

**Ready for production:** YES / NO
