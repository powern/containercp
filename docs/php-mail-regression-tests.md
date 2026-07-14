# PHP Mail Integration — Regression Tests

## Before any mail-related changes, run this checklist.

---

## 1. New Site + Enable Mail

```
1. Create a new test site:
   containercp create-site --domain test-{DATE}.local --owner admin

2. Verify site loads (HTTP 200):
   curl -sI https://test-{DATE}.local | head -1

3. Enable mail via API:
   curl -X POST http://localhost:8080/api/sites/{ID}/enable-mail

4. ✅ Status shows "Enabled" with all indicators ✅
5. ✅ msmtprc exists: /srv/.../test-{DATE}.local/config/php/msmtprc
6. ✅ Docker network connected: docker inspect site-{ID}-php | grep containercp-mail
7. ✅ Credentials in passwd-php: grep site-{ID} /srv/.../passwd-php
8. ✅ Sender_login entry: grep site-{ID} /srv/.../sender_login
9. ✅ Compose has msmtprc mount: grep msmtprc /srv/.../docker-compose.yml
10. ✅ Compose has containercp-mail network: grep containercp-mail compose.yml
11. ✅ docker compose config passes (no validation errors)

12. Send test email:
    docker exec site-{ID}-php sh -c '
      printf "From: wordpress@test-{DATE}.local\nTo: test@example.com\nSubject: test\n\nbody\n" \
      | /usr/sbin/sendmail -t -i
    '

13. ✅ Email appears in Postfix queue: postqueue -p
14. ✅ Queue ID matches the email
```

---

## 2. Migrated Site + Enable Mail

```
1. Use existing site (unity.softico.ua or similar)
2. Enable mail via API:
   curl -X POST http://localhost:8080/api/sites/{ID}/enable-mail

3. ✅ Status shows "Enabled"
4. ✅ msmtprc created (config/php/ was missing)
5. ✅ Docker network connected
6. ✅ Credentials in passwd-php
7. ✅ sendmail test works
8. ✅ Existing site files unchanged:
   - public/ (WordPress files)
   - config/ (nginx/apache config)
   - docker-compose.yml (regenerated, but valid)
   - Database untouched
   - SSL certs untouched
```

---

## 3. Disable + Re-Enable Mail

```
1. Disable mail:
   curl -X POST http://localhost:8080/api/sites/{ID}/disable-mail

2. ✅ Status shows "Disabled"
3. ✅ msmtprc removed
4. ✅ Credentials removed from passwd-php
5. ✅ Credentials removed from sender_login
6. ✅ Docker network disconnected

7. Re-enable mail:
   curl -X POST http://localhost:8080/api/sites/{ID}/enable-mail

8. ✅ Status shows "Enabled"
9. ✅ msmtprc recreated
10. ✅ Credentials recreated (no duplicates)
11. ✅ sender_login has single entry (no duplicates)
12. ✅ sendmail test works again
```

---

## 4. Idempotency — Enable Multiple Times

```
1. Enable mail 3 times in a row:
   curl -X POST http://localhost:8080/api/sites/{ID}/enable-mail
   curl -X POST http://localhost:8080/api/sites/{ID}/enable-mail
   curl -X POST http://localhost:8080/api/sites/{ID}/enable-mail

2. ✅ passwd-php has exactly ONE entry per site:
   grep -c "site-{ID}@" /srv/.../passwd-php
   # Expected: 1

3. ✅ sender_login has exactly TWO entries per site (exact + wildcard):
   grep -c "site-{ID}@" /srv/.../sender_login
   # Expected: 2

4. ✅ postmap no "duplicate entry" warnings:
   docker logs containercp-mail-postfix | grep "duplicate"
   # Expected: no output

5. ✅ Disable once: only removes THIS site's entries
6. ✅ Other sites' credentials untouched
```

---

## 5. Daemon Restart — State Persistence

```
1. Enable mail for a site
2. Verify status shows "Enabled"
3. Restart daemon:
   systemctl restart containercp

4. Wait 5 seconds
5. Check mail status via API:
   curl -X POST http://localhost:8080/api/sites/{ID}/mail-status

6. ✅ Status still shows "Enabled"
7. ✅ Credentials still exist in passwd-php
8. ✅ msmtprc still exists
9. ✅ docker network still connected

10. sendmail test still works:
    docker exec site-{ID}-php /usr/sbin/sendmail ...
```

---

## 6. PHP Image Change — msmtp availability

```
1. Build new PHP image:
   cd /opt/containercp/docker/php
   docker build -t ghcr.io/powern/containercp-php:8.4 .

2. Run on test site:
   docker compose -f /srv/.../docker-compose.yml pull php
   docker compose -f /srv/.../docker-compose.yml up -d --force-recreate php

3. ✅ /usr/bin/msmtp exists:
   docker exec site-{ID}-php which msmtp

4. ✅ /usr/sbin/sendmail exists:
   docker exec site-{ID}-php which sendmail

5. ✅ sendmail_path configured:
   docker exec site-{ID}-php php -i | grep sendmail_path
   # Expected: /usr/bin/msmtp -t

6. ✅ /etc/msmtprc mounted and readable:
   docker exec site-{ID}-php head -5 /etc/msmtprc

7. ✅ sendmail works:
   docker exec site-{ID}-php sh -c '
     printf "From: test@test.local\nTo: test@example.com\nSubject: v\n\nbody\n" \
     | /usr/sbin/sendmail -t -i
   '
```

---

## 7. Docker Compose Regeneration

```
1. Enable mail for a site
2. Verify compose has mail features:
   - grep "containercp-mail" /srv/.../docker-compose.yml
   - grep "msmtprc" /srv/.../docker-compose.yml

3. Run manual compose validation:
   docker compose -f /srv/.../docker-compose.yml config --quiet
   # Expected: exit 0

4. Disable mail
5. Verify compose NO LONGER has mail features:
   grep "containercp-mail" /srv/.../docker-compose.yml
   # Expected: no output for PHP service networks
   grep "msmtprc" /srv/.../docker-compose.yml
   # Expected: no output

6. docker compose config --quiet still passes
```

---

## 8. WordPress wp_mail() — Real Email

```
1. Open https://{DOMAIN}/wp-login.php?action=lostpassword
2. Enter admin email
3. Submit

4. ✅ Postfix queue shows the email:
   docker exec containercp-mail-postfix postqueue -p

5. ✅ Email is delivered (check mailbox or mail log)

6. ✅ Email headers:
   - From: wordpress@{DOMAIN}
   - DKIM-Signature: present (if DKIM configured)
   - SPF: pass (if DNS configured)

7. ✅ Password reset link in email works
```

---

## 9. Runtime Upgrade Recovery (destructive — staging only)

```
1. Enable mail for a site
2. Manually remove msmtprc:
   rm /srv/.../config/php/msmtprc

3. Disconnect network:
   docker network disconnect containercp-mail site-{ID}-php

4. Remove credential from passwd-php:
   (edit passwd-php, remove the site's line)

5. Remove sender_login entry:
   (edit sender_login, remove the site's lines)

6. Restart daemon:
   systemctl restart containercp

7. ✅ msmtprc is restored (check exists)
8. ✅ Network is reconnected (docker inspect)
9. ✅ passwd-php entry is restored
10. ✅ sender_login entry is restored

11. ✅ sendmail test still works
```

---

## 10. Rollback — Partial Failure Recovery

```
1. Simulate a failure during enable-mail compose regeneration:
   - Temporarily make the template_dir readonly
   - Call enable-mail

2. ✅ API returns 500 error
3. ✅ No credentials leaked (passwd-php unchanged)
4. ✅ No msmtprc created
5. ✅ Docker compose unchanged

6. Simulate a failure during disable-mail credential removal:
   - Temporarily make passwd-php readonly
   - Call disable-mail

7. ✅ API returns 500 error
8. ✅ Compose unchanged (still has mail network if removal failed)
9. ✅ Credentials still in passwd-php (no data loss)
```

---

## 11. Multiple Sites — Isolation

```
1. Enable mail for site A and site B
2. ✅ site A can send (sendmail test)
3. ✅ site B can send
4. ✅ site A's credentials not in site B's msmtprc
5. ✅ site B's credentials not in site A's msmtprc

6. Disable mail for site A
7. ✅ site A cannot send
8. ✅ site B can still send
9. ✅ site B's credentials still in passwd-php
10. ✅ site A's credentials removed from passwd-php
```
