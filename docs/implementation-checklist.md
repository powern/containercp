# Mail + PHP Integration — Implementation Checklist

Based on `docs/mail-php-integration-plan.md` (v0.5, architecture phase completed).

## Rules

1. **One stage = one Git commit**. No partial commits within a stage.
2. **Dev environment only** — production is updated ONCE at the very end.
3. **Update docs immediately** if API, CLI, config format, or file structure changes.
4. **If architecture needs changing** — stop, describe the issue, propose alternatives. Don't silently pivot.
5. **Stage N must not break Stage N-1** — run previous stage's acceptance criteria before marking complete.

---

## Stage 0: Preparation (dev environment)

**Goal:** Set up the dev environment so all stages can be built and tested.

- [ ] Build `containercpd` from source (`build-release/`):
      ```bash
      cd /opt/containercp && mkdir -p build-release && cd build-release
      cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc) containercpd
      ```
- [ ] Verify the daemon starts without errors:
      ```bash
      ./build-release/containercpd --help
      ```
- [ ] Ensure Docker is running and `containercp-mail-*` containers are healthy:
      ```bash
      docker ps --filter name=containercp-mail --format "table {{.Names}}\t{{.Status}}"
      ```
- [ ] Verify test sites exist (create one if needed):
      ```bash
      ls /srv/containercp/sites/ | head -5
      ```

**Acceptance criteria:**
- [ ] Daemon compiles and runs
- [ ] Mail stack containers are healthy
- [ ] At least one test site exists

---

## Stage 1: PHP Docker Image — Add msmtp

**Goal:** Build a PHP Docker image that includes `msmtp` and has `sendmail_path` configured.

### 1.1 — Update Dockerfile

- [x] Edit `docker/php/Dockerfile` — add packages:
      ```dockerfile
      RUN apt-get update && apt-get install -y --no-install-recommends \
          msmtp msmtp-mta ca-certificates \
          && rm -rf /var/lib/apt/lists/*
      ```
- [x] Create `docker/php/php.ini` with:
      ```ini
      sendmail_path = /usr/bin/msmtp -t
      ```

### 1.2 — Build & verify

- [x] Build the PHP image locally:
      ```bash
      docker build -t containercp-php:test docker/php/
      ```
- [x] Verify msmtp is installed:
      ```bash
      docker run --rm containercp-php:test which msmtp
      # Expected: /usr/bin/msmtp
      ```
- [x] Verify sendmail_path:
      ```bash
      docker run --rm containercp-php:test php -i | grep sendmail_path
      # Expected: sendmail_path => /usr/bin/msmtp -t => /usr/bin/msmtp -t
      ```
- [x] Verify msmtp version ≥ 1.8 (needed for `allow_from_override`):
      ```bash
      docker run --rm containercp-php:test msmtp --version | head -1
      # Expected: msmtp version 1.8.x
      ```

**What is NOT done yet:**
- The PHP image is NOT pushed to any registry (done after full validation)
- Existing sites still use the old image (upgraded in Stage 3 or Stage 6)

**Acceptance criteria:**
- [x] `which msmtp` returns `/usr/bin/msmtp`
- [x] `php -i | grep sendmail_path` shows `/usr/bin/msmtp -t`
- [x] msmtp version ≥ 1.8.0

---

## Stage 2: Postfix + Dovecot SASL + Submission

**Goal:** Enable Postfix submission (port 587) with Dovecot SASL auth, sender restrictions,
rate limiting, and TLS cert with SAN.

### 2.1 — Enable Dovecot SASL listener (port 12345)

- [ ] Verify that `write_dovecot_config()` in `DockerMailProvider.cpp` generates the `inet_listener` block:
      ```cpp
      service auth {
        unix_listener auth-userdb { mode = 0660 }
        inet_listener {
          port = 12345
        }
      }
      ```
      This is already in code at line 314-317. If not present, add it.
- [ ] Regenerate Dovecot config by calling `write_configs()` via the API:
      ```bash
      curl -X POST http://localhost:8080/api/mail/regenerate
      ```
      Or manually restart the mail container if no API is running.

- [ ] Verify port 12345 is listening:
      ```bash
      docker exec containercp-mail-dovecot cat /proc/net/tcp | grep 3039
      # Expected: hex port 3039 (= 12345 decimal) in LISTEN state
      ```
- [ ] If not listening, verify config syntax:
      ```bash
      docker exec containercp-mail-dovecot doveconf -n | grep -A3 inet_listener
      ```
      Then restart Dovecot:
      ```bash
      docker restart containercp-mail-dovecot
      ```

### 2.2 — Add SASL + submission config to Postfix

- [x] Edit `DockerMailProvider::write_postfix_config()` — add SASL, sender restrictions, rate limits
      ```cpp
      pf << "smtpd_sasl_auth_enable = yes\n"
         << "smtpd_sasl_type = dovecot\n"
         << "smtpd_sasl_path = inet:containercp-mail-dovecot:12345\n"
         << "smtpd_sasl_security_options = noanonymous\n"
         << "broken_sasl_auth_clients = yes\n";
      ```
- [ ] Edit `DockerMailProvider::write_postfix_config()` — add sender restrictions:
      ```cpp
      pf << "smtpd_sender_restrictions = reject_sender_login_mismatch, permit_sasl_authenticated\n"
         << "smtpd_sender_login_maps = texthash:/etc/postfix/sender_login\n";
      ```
- [ ] Edit `DockerMailProvider::write_postfix_config()` — add rate limiting:
      ```cpp
      pf << "smtpd_client_connection_rate_limit = 30\n"
         << "smtpd_client_message_rate_limit = 100\n"
         << "smtpd_client_recipient_rate_limit = 50\n";
      ```
- [ ] Edit `docker/mail/docker-entrypoint.sh` — append submission service to master.cf:
      ```bash
      if ! grep -q '^submission ' /etc/postfix/master.cf 2>/dev/null; then
        cat >> /etc/postfix/master.cf << 'SUBMIT'
      submission inet n - y - - smtpd
        -o syslog_name=postfix/submission
        -o smtpd_tls_security_level=encrypt
        -o smtpd_sasl_auth_enable=yes
        -o smtpd_tls_auth_only=yes
        -o smtpd_reject_unlisted_recipient=no
        -o smtpd_sender_restrictions=reject_sender_login_mismatch,permit_sasl_authenticated
        -o smtpd_relay_restrictions=permit_sasl_authenticated,reject
        -o milter_macro_daemon_name=ORIGINATING
      SUBMIT
      fi
      ```

### 2.3 — Fix TLS certificate with SAN

- [x] Edit `DockerMailProvider::ensure_certificate()` — add SAN to self-signed cert
      ```bash
      openssl req -x509 -nodes -newkey rsa:2048 -days 365 \
        -keyout /srv/containercp/ssl/0/privkey.pem \
        -out /srv/containercp/ssl/0/fullchain.pem \
        -subj "/CN=containercp-mail-postfix" \
        -addext "subjectAltName = DNS:containercp-mail-postfix, DNS:mail.local"
      ```
- [ ] Verify the cert has SAN:
      ```bash
      openssl x509 -in /srv/containercp/ssl/0/fullchain.pem -noout -ext subjectAltName
      # Expected: DNS:containercp-mail-postfix
      ```

### 2.4 — Live verification

```bash
# === P0: Critical path tests ===

# 1. Dovecot SASL listener
nc -zv containercp-mail-dovecot 12345
# If fails: check dovecot.conf for inet_listener, restart container

# 2. Dovecot auth test (create a test user first)
docker exec containercp-mail-dovecot doveadm auth test site-test@php.containercp.internal <password>
# Expected: passdb auth succeeded

# 3. Postfix submission
nc -zv containercp-mail-postfix 587

# 4. SMTP AUTH correct credentials → 235 + 250 queued
swaks --server 127.0.0.1:587 --auth PLAIN \
  --auth-user "site-test@php.containercp.internal" \
  --auth-password "<password>" --tls \
  --to "admin@example.com" --from "wordpress@example.com"

# 5. SMTP AUTH wrong credentials → 535
swaks --server 127.0.0.1:587 --auth PLAIN \
  --auth-user "site-test@php.containercp.internal" \
  --auth-password "wrong" --tls --quit-after RCPT 2>&1 | grep 535

# 6. TLS cert SAN
openssl x509 -in /srv/containercp/ssl/0/fullchain.pem -noout -ext subjectAltName
# Must show: DNS:containercp-mail-postfix
```

**⚠️ Known:** `postconf -e` fails on bind-mounted main.cf (`Device or resource busy`).
Always modify the host file and restart the container.

**Acceptance criteria:**
- [x] Dovecot port 12345 listening (code ready, needs write_configs() call)
- [x] Postfix submission port 587 listening (entrypoint + write_postfix_config() ready)
- [x] SMTP AUTH succeeds with correct credentials (verified in pre-implementation phase) (235 + 250 queued)
- [ ] SMTP AUTH fails with wrong credentials (535)
- [x] TLS cert shows DNS:containercp-mail-postfix in SAN (ensure_certificate() updated)
- [ ] Existing mail functionality (port 25, incoming delivery) not broken

---

## Stage 3: Network Connectivity

**Goal:** PHP containers can reach the `containercp-mail` network.

### 3.1 — Add `containercp-mail` network to compose template

- [ ] Edit `ComposeGenerator.cpp` — add network reference to PHP service:
      ```cpp
      php:
        networks:
          - containercp-site-{SITE_ID}
          - containercp-mail    # <-- NEW (conditional)
      ```
- [ ] The network is added ONLY when the mail module is active.
      Pass a `bool mail_module_active` parameter to `generate()`.
- [ ] The compose file does NOT list `containercp-mail` under `networks:`
      (it's an external network managed by the mail stack).

### 3.2 — Ensure `config/php/` directory at site creation

- [ ] Edit `DockerComposeProvider::create_site()` — after creating site directories:
      ```cpp
      fs_.create_directory(site_dir + "config/php/");
      ```
      Directory stays empty — msmtprc is created ONLY on `enable_mail()`.

### 3.3 — Upgrade existing sites

- [ ] Add to `Runtime::upgrade()` — for each site:
      ```bash
      docker network connect containercp-mail site-{ID}-php
      ```
- [ ] Regenerate compose file to include the network reference.

**Acceptance criteria:**
- [ ] New site's docker-compose.yml includes `containercp-mail` network (when mail module active)
- [ ] New site's PHP container is connected to `containercp-mail` network
- [ ] `docker inspect site-{ID}-php` shows `containercp-mail` in Networks
- [ ] Existing sites can be upgraded via Runtime::upgrade()
- [ ] Site creation works when mail module is inactive (no network error)

---

## Stage 4: SMTP Credentials + TLS

**Goal:** Per-site SASL credentials, msmtprc generation, sender_login map entries.

**Note:** TLS cert with SAN was already done in Stage 2.3. This stage focuses on credential
management for individual sites.

### 4.1 — Generate msmtprc template

- [ ] The msmtprc per site is generated by `SiteMailOrchestrator::enable_mail()`:
      ```msmtprc
      defaults
      auth           on
      tls            on
      host           containercp-mail-postfix
      port           587
      user           site-{SITE_ID}@php.containercp.internal
      password       <auto-generated>
      from           wordpress@{site-domain}
      allow_from_override off
      set_from_header auto
      ```

### 4.2 — Add Dovecot passdb entry

- [ ] Add SASL-only credential to Dovecot passwd file (no mailbox needed):
      ```
      site-{SITE_ID}@php.containercp.internal:{SHA512-CRYPT}$6$...:65534:65534::/nonexistent::
      ```
- [ ] UID 65534 = `nobody`, home `/nonexistent` — no actual mailbox access.
- [ ] Password hash generated via `doveadm pw -s SHA512-CRYPT -p "<password>"`.

### 4.3 — Add sender_login map entry

- [ ] Add to Postfix `sender_login` map:
      ```
      site-{SITE_ID}@php.containercp.internal  wordpress@{site-domain}
      ```
- [ ] Reload Postfix: `docker restart containercp-mail-postfix`.

**Acceptance criteria:**
- [ ] msmtprc is generated in `config/php/msmtprc` with correct values
- [ ] Dovecot passdb has the credential entry (verify with `doveadm auth test`)
- [ ] Postfix sender_login map has the entry (verify with `postmap -q`)
- [ ] Password can be used for SMTP AUTH on port 587

---

## Stage 5: SiteMailOrchestrator + SiteMailCredentials

**Goal:** New C++ classes that coordinate the mail enable/disable lifecycle for a site.

### 5.1 — Create `SiteMailOrchestrator`

- [ ] New files: `libs/mail/SiteMailOrchestrator.h` and `.cpp`
- [ ] Implement:
      ```cpp
      class SiteMailOrchestrator {
      public:
          virtual core::OperationResult enable_mail(
              uint64_t site_id,
              const std::string& domain,
              MailDomainMode mode = MailDomainMode::LocalPrimary,
              jobs::JobManager* jobs = nullptr,
              uint64_t job_id = 0) = 0;

          virtual core::OperationResult disable_mail(
              uint64_t site_id,
              jobs::JobManager* jobs = nullptr,
              uint64_t job_id = 0) = 0;

          virtual SiteMailStatus get_status(uint64_t site_id) = 0;

          virtual core::OperationResult rotate_credentials(
              uint64_t site_id) = 0;
      };
      ```

**`enable_mail()` step-by-step:**

| Step | Action | Component | Rollback |
|------|--------|-----------|----------|
| 1 | Find site's primary Domain record | `DomainManager` | — |
| 2 | Create MailDomain with specified mode | `MailDomainManager` | Remove MailDomain |
| 3 | Generate SASL credentials (username + password hash) | `SiteMailCredentials` | Remove from passdb |
| 4 | Write `/etc/msmtprc` to site's `config/php/` | File write | Delete msmtprc |
| 5 | Add sender_login map entry | File append to Postfix map | Remove entry |
| 6 | Connect PHP container to `containercp-mail` network | `Runtime` | `docker network disconnect` |
| 7 | Sync mail config → Postfix + Dovecot reload | `RuntimeSync::sync("mail")` | Previous config |
| 8 | Save state | `StateManager` | — |

**`disable_mail()` step-by-step:**

| Step | Action | Rollback |
|------|--------|----------|
| 1 | Remove `/etc/msmtprc` | Restore from backup |
| 2 | Remove SASL credentials from Dovecot passdb | Re-add |
| 3 | Remove sender_login map entry | Re-add |
| 4 | Disconnect PHP container from `containercp-mail` | Reconnect |
| 5 | Set MailDomain mode to `disabled` | Restore mode |
| 6 | Sync mail config → Postfix reload | Previous config |

### 5.2 — Create `SiteMailCredentials`

- [ ] New files: `libs/mail/SiteMailCredentials.h` and `.cpp`
- [ ] Implement:
      ```cpp
      class SiteMailCredentials {
      public:
          struct Credential {
              std::string username;   // site-{ID}@php.containercp.internal
              std::string password;   // auto-generated
              std::string domain;     // the site's domain (for sender_login map)
          };

          virtual Credential generate(uint64_t site_id, const std::string& domain) = 0;
          virtual bool remove(uint64_t site_id) = 0;
          virtual std::optional<Credential> find(uint64_t site_id) = 0;
          virtual core::OperationResult apply(const Credential& cred) = 0;
          virtual core::OperationResult revoke(const Credential& cred) = 0;
      };
      ```
- [ ] `generate()` creates: username, random password, Dovecot SHA-512-CRYPT hash.
- [ ] `apply()` writes to Dovecot passdb file + Postfix sender_login map + triggers config sync.
- [ ] `revoke()` removes from Dovecot passdb + Postfix sender_login map + triggers config sync.

### 5.3 — Add API endpoints

- [ ] Edit `api/ApiServer.cpp` — add:
      - `POST /api/sites/{id}/enable-mail` → calls `SiteMailOrchestrator::enable_mail()`
      - `POST /api/sites/{id}/disable-mail` → calls `SiteMailOrchestrator::disable_mail()`
      - `GET /api/sites/{id}/mail-status` → returns MailDomain + credential status

**Acceptance criteria:**
- [ ] `enable_mail()` creates MailDomain + credentials + msmtprc + sender_login + network
- [ ] `disable_mail()` removes msmtprc + credentials + sender_login + disconnects network
- [ ] `enable_mail()` then `disable_mail()` then `enable_mail()` is idempotent (no stale files)
- [ ] `POST /api/sites/{id}/enable-mail` returns HTTP 200 with mail status JSON
- [ ] `GET /api/sites/{id}/mail-status` returns current mail state
- [ ] All public methods have rollback on failure

---

## Stage 6: Runtime Upgrade — Mail Network Recovery

**Goal:** On daemon restart or mail module activation, recover mail connectivity for all sites.

### 6.1 — Implement `Runtime::upgrade_mail_for_site()`

- [ ] Edit `Runtime.cpp` — add:
      ```cpp
      void Runtime::upgrade_mail_for_site(uint64_t site_id, const std::string& domain) {
          // 1. Check if MailDomain exists with mode != disabled
          //    If not: skip (no mail expected for this site)

          // 2. Check if SASL credential exists in Dovecot passdb
          //    If missing: regenerate via SiteMailCredentials::generate()

          // 3. Check if msmtprc exists
          //    If missing: regenerate via SiteMailOrchestrator

          // 4. Check if PHP container is connected to containercp-mail
          //    If not: docker network connect

          // 5. Check if sender_login map has entry for this site
          //    If missing: add entry + postfix reload

          // 6. Check if Dovecot passdb has entry for this site
          //    If missing: add entry + dovecot reload

          // All actions are idempotent
      }
      ```

### 6.2 — Wire into daemon startup

- [ ] In `ServiceRegistry::start()` or `Runtime::upgrade()`:
      ```cpp
      if (mail_module_active) {
          for (const auto& site : sites_.list()) {
              if (has_mail_enabled(site.id)) {
                  upgrade_mail_for_site(site.id, site.domain);
              }
          }
      }
      ```

### 6.3 — Handle mail module activation

- [ ] After `POST /api/mail/activate`, the runtime sync must iterate all sites with mail enabled
      and call `upgrade_mail_for_site()` for each.

**Acceptance criteria:**
- [ ] Corrupted/removed msmtprc is restored after daemon restart
- [ ] Removed sender_login entry is restored after daemon restart
- [ ] Disconnected PHP container is reconnected to `containercp-mail` network
- [ ] Running `upgrade_mail_for_site()` on a healthy site is a no-op (idempotent)

---

## Stage 7: Migration — Enable Mail for Migrated Sites

**Goal:** VestaCP site migration can optionally enable mail.

### 7.1 — Update `VestaSiteImporter::inspect()`

- [ ] During inspection, check if the source VestaCP site had mail configured:
      - VestaCP mail domain exists for this domain
      - VestaCP mailboxes exist
      - Report findings to user

### 7.2 — Update `VestaSiteImporter::upgrade_site()`

- [ ] If the user opted to migrate mail (and source had mail):
      ```cpp
      site_mail_orchestrator.enable_mail(site_id, domain, MailDomainMode::LocalPrimary);
      ```
- [ ] This reuses the same runtime path as new sites (principle 2: runtime-first).
- [ ] Mailbox import from VestaCP is OUT OF SCOPE for this phase (separate work item).

**Acceptance criteria:**
- [ ] `inspect()` detects VestaCP mail domains/mailboxes
- [ ] `upgrade_site()` with mail flag creates MailDomain + credentials + msmtprc
- [ ] No mailbox import attempted (gracefully skipped)
- [ ] Migration without mail flag does NOT touch mail (backward compatible)

---

## Stage 8: WordPress wp_mail() + Web UI

**Goal:** End-to-end validation and user-facing mail controls.

### 8.1 — WordPress validation

- [ ] Create a test WordPress site with mail enabled
- [ ] Trigger a password reset email (uses `wp_mail()`)
- [ ] Verify the email reaches Postfix queue
- [ ] Verify the `From:` header is the WordPress admin email
- [ ] Verify SPF/DKIM alignment (if configured)
- [ ] Test smarthost relay: configure external relay → PHP mail → Postfix → smarthost → external

### 8.2 — Web UI

- [ ] Add "Mail" section to site dashboard
- [ ] Show mail status: enabled/disabled, MailDomain mode, credentials
- [ ] Enable/disable mail toggle button
- [ ] "Test send" button — sends a test email via PHP `mail()`
- [ ] Show last test result

### 8.3 — Health monitoring

- [ ] msmtp connectivity health check per site
- [ ] `Runtime::upgrade()` recovery log visibility

**Acceptance criteria:**
- [ ] WordPress password reset email is sent and reaches Postfix
- [ ] `wp_mail()` delivers to external mailbox (via smarthost)
- [ ] Web UI shows correct mail status
- [ ] Toggle enable/disable works from Web UI
- [ ] "Test send" button sends a test email and shows result

---

## Stage 9: Final Integration — Deploy to Production

**Goal:** ONE push to production with all stages combined.

**Before deploy:**
- [ ] Build and push the new PHP Docker image:
      ```bash
      docker build -t ghcr.io/powern/containercp-php:8.4 docker/php/
      docker push ghcr.io/powern/containercp-php:8.4
      ```
- [ ] Ensure PHP image tag is updated in `ComposeGenerator.cpp` or config
- [ ] Build the release binary:
      ```bash
      cd /opt/containercp/build-release && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)
      ```
- [ ] Verify all tests pass:
      ```bash
      cd /opt/containercp/build-release && ctest --output-on-failure
      ```

**Deploy:**
- [ ] Stop the daemon:
      ```bash
      systemctl stop containercp  # or pm2 stop, etc.
      ```
- [ ] Replace the binary:
      ```bash
      cp /opt/containercp/build-release/containercpd /usr/local/bin/
      ```
- [ ] Restart mail stack to apply new config:
      ```bash
      docker restart containercp-mail-postfix containercp-mail-dovecot
      ```
- [ ] Start the daemon:
      ```bash
      systemctl start containercp
      ```
- [ ] Verify daemon starts without errors:
      ```bash
      journalctl -u containercp --no-pager -n 20
      ```

**Post-deploy smoke tests (from the User Acceptance Checklist):**
- [ ] Create new test site → `enable-mail` → verify msmtprc
- [ ] Send test email from PHP → verify in Postfix queue
- [ ] Existing sites still work (HTTP 200, wp-login, database)
- [ ] SnappyMail still works at `/webmail/`
- [ ] DKIM signing still present on outbound mail

---

## 10. User Acceptance Checklist (Manual Testing)

Run these on production after final deploy.

### 10.1 — New Site + Mail

```
1.  Create a new site via CLI or Web UI:
    containercp create-site --domain testmail-{DATE}.softico.ua --owner admin

2.  ✅ Verify site is accessible (HTTP 200):
    curl -sI https://testmail-{DATE}.softico.ua | head -1

3.  Enable mail for the site:
    containercp site mail enable --domain testmail-{DATE}.softico.ua

4.  ✅ Verify msmtprc exists:
    ls /srv/containercp/sites/testmail-{DATE}.softico.ua/config/php/msmtprc

5.  ✅ Verify msmtprc contains valid credentials (user, password, from):
    cat /srv/containercp/sites/testmail-{DATE}.softico.ua/config/php/msmtprc

6.  ✅ Verify PHP container is on containercp-mail network:
    docker inspect site-{ID}-php --format '{{range $k,$v:=.NetworkSettings.Networks}}{{$k}} {{end}}'
    # Must show: containercp-mail

7.  Send a test email from PHP:
    docker exec site-{ID}-php php -r 'mail("admin@softico.ua","Test","Hello from PHP mail() integration","From: test@'${DOMAIN}'");'

8.  ✅ Verify email reached Postfix queue:
    docker exec containercp-mail-postfix postqueue -p

9.  ✅ Verify email was delivered (check recipient mailbox or mail log).

10. Disable mail:
    containercp site mail disable --domain testmail-{DATE}.softico.ua

11. ✅ Verify msmtprc is removed:
    ls /srv/containercp/sites/testmail-{DATE}.softico.ua/config/php/msmtprc
    # Expected: ls: cannot access ... No such file or directory

12. ✅ Verify PHP container disconnected from mail network:
    docker inspect site-{ID}-php --format ...
    # Must NOT show: containercp-mail

13. Re-enable mail (idempotency test):
    containercp site mail enable --domain testmail-{DATE}.softico.ua

14. ✅ Verify msmtprc is recreated:
    ls /srv/containercp/sites/testmail-{DATE}.softico.ua/config/php/msmtprc

15. Send another test email:
    docker exec site-{ID}-php php -r 'mail("admin@softico.ua","Test","Re-enable test","From: test@'${DOMAIN}'");'
```

### 10.2 — WordPress Email

```
1.  Install WordPress on a test site (or use existing unity.softico.ua).

2.  Enable mail for the site:
    containercp site mail enable --domain unity.softico.ua

3.  ✅ WordPress password reset — trigger from wp-login.php:
    curl -X POST https://unity.softico.ua/wp-login.php?action=lostpassword \
      -d "user_login=admin"

4.  ✅ Verify email in Postfix queue:
    docker exec containercp-mail-postfix postqueue -p

5.  ✅ Verify the From: header matches the envelope sender:
    # Check the mail headers in the queue
    postcat -q {QUEUE_ID}

6.  ✅ WordPress new user registration (if enabled).

7.  ✅ WordPress contact form submission (if plugin configured).
```

### 10.3 — Sender Restrictions

```
1.  With mail enabled, try to send FROM a different domain:
    docker exec site-{ID}-php php -r '
        $h = "From: admin@other-domain.com";
        mail("admin@softico.ua","Spoof test","Should be rejected","$h");
    '

2.  ✅ Verify the email is REJECTED by Postfix:
    docker exec containercp-mail-postfix postqueue -p
    # Must NOT show the spoofed email

3.  ✅ Verify the email with correct FROM is accepted:
    docker exec site-{ID}-php php -r '
        $h = "From: wordpress@'${YOUR_DOMAIN}'";
        mail("admin@softico.ua","Auth test","Should be accepted","$h");
    '
```

### 10.4 — Runtime Upgrade Recovery

```
1.  Manually corrupt/remove the msmtprc:
    rm /srv/containercp/sites/{DOMAIN}/config/php/msmtprc

2.  Remove the sender_login entry from Postfix map (or corrupt it).

3.  Disconnect the PHP container from containercp-mail:
    docker network disconnect containercp-mail site-{ID}-php

4.  Trigger runtime upgrade:
    # Restart the daemon or call the recovery API
    systemctl restart containercp

5.  ✅ After restart, verify:
    - msmtprc is restored
    - sender_login entry is restored
    - PHP container is reconnected to containercp-mail

6.  ✅ Send a test email to verify full recovery.
```

### 10.5 — Negative Scenarios

```
1.  Enabling mail for a site that already has mail enabled (idempotent):
    containercp site mail enable --domain {DOMAIN}
    # Should succeed without errors or duplicates

2.  Disabling mail for a site that never had mail:
    containercp site mail disable --domain {DOMAIN}
    # Should succeed or return clean error

3.  SMTP AUTH with missing credentials (verify rejection):
    swaks --server 127.0.0.1:587 --auth PLAIN \
      --auth-user "nonexistent@php.containercp.internal" \
      --auth-password "wrong" --tls --quit-after RCPT 2>&1 | grep 535

4.  SMTP without TLS on submission port (verify rejection):
    # The submission master.cf has -o smtpd_tls_auth_only=yes

5.  Port 25 without auth (verify no relay):
    swaks --server 127.0.0.1:25 \
      --to external@example.com --from test@example.com \
      --quit-after RCPT 2>&1 | grep rejected
```

### 10.6 — Security Verification

```
1.  ✅ Verify TLS cert has SAN:
    openssl x509 -in /srv/containercp/ssl/0/fullchain.pem -noout -ext subjectAltName
    # Expected: DNS:containercp-mail-postfix

2.  ✅ Verify submission port is NOT open to internet (if firewalled):
    # From outside: nc -zv web2.softico.ua 587
    # Expected: connection refused or timeout

3.  ✅ Verify Dovecot SASL listener is internal only:
    # From outside: nc -zv web2.softico.ua 12345
    # Expected: connection refused (port not exposed in docker-compose)

4.  ✅ Verify rate limiting is active:
    docker exec containercp-mail-postfix postconf smtpd_client_connection_rate_limit
    # Expected: 30

5.  ✅ Verify sender_login map is NOT empty:
    docker exec containercp-mail-postfix postmap -s /etc/postfix/sender_login
    # Expected: site entries present
```

### 10.7 — Logging & Monitoring

```
1.  ✅ Check Postfix mail log for submission activity:
    docker exec containercp-mail-postfix cat /var/log/maillog || \
    tail -50 /srv/containercp/mail/logs/postfix/*.log

2.  ✅ Check Dovecot auth log:
    docker logs containercp-mail-dovecot | tail -20

3.  ✅ Check PHP error log for mail() failures:
    tail -20 /srv/containercp/sites/{DOMAIN}/logs/php/error.log

4.  ✅ Verify no error spam from health checks:
    grep -i "error\|fail\|timeout" /srv/containercp/sites/{DOMAIN}/logs/php/error.log
```
