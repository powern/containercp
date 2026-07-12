#include "DockerMailProvider.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace containercp::mail {

DockerMailProvider::DockerMailProvider(logger::Logger& logger,
                                       const std::string& data_root)
    : logger_(logger)
    , data_root_(data_root)
{
}

std::string DockerMailProvider::compose_dir() const {
    return data_root_ + "/mail";
}

std::string DockerMailProvider::config_dir() const {
    return data_root_ + "/mail/config";
}

std::string DockerMailProvider::compose_project_flag() const {
    return "--project-directory " + compose_dir();
}

// ── Environment preparation (directories + Docker network) ──────────

core::OperationResult DockerMailProvider::prepare_environment() {
    // Create directory structure via CommandExecutor
    auto mkdir = executor_.run({
        "mkdir", "-p",
        config_dir() + "/generated",
        config_dir() + "/custom",
        config_dir() + "/custom/postfix",
        config_dir() + "/custom/dovecot",
        config_dir() + "/state/dkim",
        config_dir() + "/state",
        config_dir() + "/generated/opendkim"
    });
    if (mkdir.exit_code != 0) {
        return {false, "Failed to create mail directories: " + mkdir.err};
    }

    // Ensure Docker network exists
    auto net = executor_.run({
        "docker", "network", "inspect", "containercp-mail"
    });
    if (net.exit_code != 0) {
        auto create = executor_.run({
            "docker", "network", "create", "containercp-mail"
        });
        if (create.exit_code != 0) {
            return {false, "Failed to create Docker network: " + create.err};
        }
    }

    // Ensure TLS certificate exists
    auto cert = ensure_certificate();
    if (!cert.success) {
        logger_.warning("MAIL", "Certificate check: " + cert.message);
    }

    return {true, ""};
}

core::OperationResult DockerMailProvider::ensure_certificate() {
    std::string cert_dir = data_root_ + "/ssl/0";
    std::string cert_path = cert_dir + "/fullchain.pem";
    std::string key_path = cert_dir + "/privkey.pem";

    // Check if certificate already exists and is valid
    auto check = executor_.run({
        "openssl", "x509", "-in", cert_path, "-noout", "-checkend", "0"
    });
    if (check.exit_code == 0) {
        return {true, "Certificate valid"};
    }

    // Create directory and generate self-signed certificate
    executor_.run({"mkdir", "-p", cert_dir});
    auto gen = executor_.run({
        "openssl", "req", "-x509", "-newkey", "rsa:2048",
        "-keyout", key_path,
        "-out", cert_path,
        "-days", "365",
        "-nodes",
        "-subj", "/CN=mail.local"
    });
    if (gen.exit_code != 0) {
        return {false, "Failed to generate self-signed certificate: " + gen.err};
    }

    // Secure permissions on private key
    executor_.run({"chmod", "0600", key_path});
    logger_.info("MAIL", "Self-signed certificate created at " + cert_path);
    return {true, "Self-signed certificate created"};
}

void DockerMailProvider::set_smarthost(const std::string& host, int port,
                                        const std::string& username,
                                        const std::string& password) {
    smarthost_host_ = host;
    smarthost_port_ = port;
    smarthost_user_ = username;
    smarthost_pass_ = password;
}

// ── Configuration generation (no Docker/runtime logic) ─────────────

core::OperationResult DockerMailProvider::write_postfix_config(
    const std::vector<MailDomain>& domains,
    const MailboxManager& mailboxes,
    const MailAliasManager& aliases) {
    (void)mailboxes;

    std::ostringstream pf;
    pf << "# ContainerCP generated configuration\n"
       << "# Do not edit manually — changes will be overwritten.\n\n"
       << "myhostname = mail.local\n"
       << "mydomain = local\n"
       << "myorigin = $mydomain\n"
       << "inet_interfaces = all\n"
       << "inet_protocols = ipv4\n"
       << "mydestination = localhost\n"
       << "mynetworks = 127.0.0.0/8, 192.168.0.0/16, 172.16.0.0/12, 10.0.0.0/8\n"
       << "maillog_file = /var/log/postfix/maillog\n"
       << "compatibility_level = 3.6\n"
       << "smtp_address_preference = ipv4\n"
       << "smtpd_relay_restrictions = permit_mynetworks, permit_sasl_authenticated, reject_unauth_destination\n"
       << "smtp_host_lookup = dns\n"
       << "smtp_dns_support_level = enabled\n";

    // TLS settings (certificates from ContainerCP CertificateStore via mounted path)
    pf << "smtpd_tls_cert_file = /srv/containercp/ssl/0/fullchain.pem\n"
       << "smtpd_tls_key_file = /srv/containercp/ssl/0/privkey.pem\n"
       << "smtpd_tls_security_level = may\n"
       << "smtpd_tls_loglevel = 1\n"
       << "smtp_tls_security_level = may\n";

    // DKIM signing via OpenDKIM milter
    pf << "milter_protocol = 6\n"
       << "milter_default_action = accept\n"
       << "smtpd_milters = inet:localhost:8891\n"
       << "non_smtpd_milters = inet:localhost:8891\n";

    // Transport maps for split delivery
    pf << "transport_maps = texthash:/etc/postfix/transport_maps\n";

    // Virtual alias maps
    pf << "virtual_alias_maps = texthash:/etc/postfix/virtual_aliases\n";

    // Smarthost (external SMTP relay) for all outbound mail
    if (!smarthost_host_.empty() && smarthost_port_ > 0) {
        pf << "relayhost = [" << smarthost_host_ << "]:" << smarthost_port_ << "\n";
        if (!smarthost_user_.empty()) {
            pf << "smtp_sasl_auth_enable = yes\n"
               << "smtp_sasl_password_maps = texthash:/etc/postfix/sasl_passwd\n"
               << "smtp_sasl_security_options = noanonymous\n"
               << "smtp_tls_security_level = encrypt\n"
               << "smtp_tls_CApath = /etc/ssl/certs\n";
        }
    }

    // Relay domains
    bool relay_first = true;
    for (const auto& d : domains) {
        if (d.mode != MailDomainMode::ExternalRelay &&
            d.mode != MailDomainMode::SplitM365) continue;
        if (!d.enabled) continue;
        if (relay_first) { pf << "relay_domains = "; relay_first = false; }
        else { pf << ", "; }
        pf << d.domain_name;
    }
    if (!relay_first) pf << "\n";

    // Virtual domains
    pf << "virtual_mailbox_domains = ";
    bool first = true;
    for (const auto& d : domains) {
        if (d.mode != MailDomainMode::LocalPrimary && d.mode != MailDomainMode::SplitM365) continue;
        if (!d.enabled) continue;
        if (!first) pf << ", ";
        first = false;
        pf << d.domain_name;
    }
    pf << "\n"
       << "virtual_mailbox_maps = texthash:/etc/postfix/virtual_mailboxes\n"
       << "virtual_transport = lmtp:containercp-mail-dovecot:24\n";

    std::string path = config_dir() + "/generated/postfix-main.cf";
    std::ofstream out(path);
    if (!out.is_open()) return {false, "Failed to write " + path};
    out << pf.str();

    // Virtual mailbox map (always created, even if empty — Postfix needs the file)
    std::string vmb_path = config_dir() + "/generated/virtual_mailboxes";
    std::ofstream vmb_out(vmb_path);
    if (!vmb_out.is_open()) return {false, "Failed to write " + vmb_path};
    vmb_out << "# ContainerCP virtual mailbox map\n";
    for (const auto& mb : mailboxes.list()) {
        std::string domain;
        for (const auto& d : domains) {
            if (d.id == mb.domain_id) { domain = d.domain_name; break; }
        }
        if (domain.empty()) continue;
        vmb_out << mb.local_part << "@" << domain << " "
                << domain << "/" << mb.local_part << "/\n";
    }

    // Virtual alias map (always created, even if empty)
    std::string ali_path = config_dir() + "/generated/virtual_aliases";
    std::ofstream ali_out(ali_path);
    if (!ali_out.is_open()) return {false, "Failed to write " + ali_path};
    ali_out << "# ContainerCP virtual alias map\n";
    for (const auto& a : aliases.list()) {
        std::string domain;
        for (const auto& d : domains) {
            if (d.id == a.domain_id) { domain = d.domain_name; break; }
        }
        if (domain.empty() || !a.enabled) continue;
        ali_out << a.source_local_part << "@" << domain
                << "\t" << a.destination << "\n";
    }

    // SASL password file for smarthost authentication
    std::string sasl_path = config_dir() + "/generated/sasl_passwd";
    if (!smarthost_user_.empty()) {
        std::ofstream sasl_out(sasl_path);
        if (!sasl_out.is_open()) return {false, "Failed to write " + sasl_path};
        sasl_out << "[" << smarthost_host_ << "]:" << smarthost_port_
                 << "\t" << smarthost_user_ << ":" << smarthost_pass_ << "\n";
    } else {
        // Create empty file (Postfix needs the file for texthash)
        std::ofstream sasl_out(sasl_path);
        if (!sasl_out.is_open()) return {false, "Failed to write " + sasl_path};
        sasl_out << "# ContainerCP SASL password file (empty)\n";
    }

    // Postfix resolver config for external DNS (override Docker's DNS)
    std::string res_path = config_dir() + "/generated/postfix-resolv.conf";
    std::ofstream res_out(res_path);
    if (!res_out.is_open()) return {false, "Failed to write " + res_path};
    res_out << "# ContainerCP Postfix DNS resolver configuration\n"
            << "nameserver 8.8.8.8\n"
            << "nameserver 8.8.4.4\n"
            << "options ndots:0\n";

    return {true, ""};
}

core::OperationResult DockerMailProvider::write_dovecot_config(
    const std::vector<MailDomain>& domains,
    const MailboxManager& mailboxes) {
    (void)domains;
    std::ostringstream dv;
    dv << "# ContainerCP generated configuration\n"
       << "# Do not edit manually — changes will be overwritten.\n\n"
       << "mail_location = maildir:/var/mail/%d/%n\n"
       << "ssl = yes\n"
       << "ssl_cert = </srv/containercp/ssl/0/fullchain.pem\n"
       << "ssl_key = </srv/containercp/ssl/0/privkey.pem\n"
       << "ssl_min_protocol = TLSv1.2\n"
       << "passdb {\n"
       << "  driver = passwd-file\n"
       << "  args = /etc/dovecot/passwd\n"
       << "}\n"
       << "userdb {\n"
       << "  driver = static\n"
       << "  args = uid=1000 gid=1000 home=/var/mail/%d/%n\n"
       << "}\n"
       << "protocols = imap pop3 lmtp\n"
       << "service auth {\n"
       << "  unix_listener auth-userdb {\n"
       << "    mode = 0660\n"
       << "  }\n"
       << "}\n";

    std::string dv_path = config_dir() + "/generated/dovecot.conf";
    std::ofstream dv_out(dv_path);
    if (!dv_out.is_open()) return {false, "Failed to write " + dv_path};
    dv_out << dv.str();

    // Dovecot passwd file (SHA-512-CRYPT hashes from MailboxManager)
    std::string pw_path = config_dir() + "/generated/passwd";
    std::ofstream pw_out(pw_path);
    if (!pw_out.is_open()) return {false, "Failed to write " + pw_path};
    for (const auto& mb : mailboxes.list()) {
        std::string domain;
        for (const auto& d : domains) {
            if (d.id == mb.domain_id) { domain = d.domain_name; break; }
        }
        if (domain.empty() || !mb.enabled) continue;
        pw_out << mb.local_part << "@" << domain
               << ":" << mb.password_hash
               << ":1000:1000::/var/mail/" << domain << "/" << mb.local_part << "/::\n";
    }
    return {true, ""};
}

core::OperationResult DockerMailProvider::write_transport_maps(
    const std::vector<MailDomain>& domains,
    const MailboxManager& mailboxes) {
    std::string path = config_dir() + "/generated/transport_maps";
    std::ofstream out(path);
    if (!out.is_open()) return {false, "Failed to write " + path};

    for (const auto& d : domains) {
        if (!d.enabled) continue;

        // LocalPrimary — all recipients are local.  Transport overrides
        // all delivery to LMTP.  Unknown recipients are rejected by
        // Dovecot (not ideal, but functional).
        if (d.mode == MailDomainMode::LocalPrimary) {
            out << d.domain_name << " lmtp:containercp-mail-dovecot:24\n";
        }

        // SplitM365 — generate per-user LMTP entries for every local
        // mailbox, then a domain-level SMTP catch-all for unknowns.
        // transport_maps takes precedence over virtual_transport, so
        // per-user entries are required to keep local delivery local.
        if (d.mode == MailDomainMode::SplitM365 && !d.relay_host.empty()) {
            // Per-user LMTP entries: these match full addresses and
            // take priority over the domain-level SMTP entry below.
            for (const auto& mb : mailboxes.list()) {
                if (mb.domain_id != d.id || !mb.enabled) continue;
                out << mb.local_part << "@" << d.domain_name
                    << " lmtp:containercp-mail-dovecot:24\n";
            }
            // Domain-level catch-all: recipients NOT in virtual_mailbox_maps
            // (and not matched by a per-user LMTP entry above) fall through
            // to this domain entry and are relayed to M365.
            out << d.domain_name << " smtp:[" << d.relay_host << "]:25\n";
        }

        // ExternalRelay — all recipients relayed.
        if (d.mode == MailDomainMode::ExternalRelay && !d.relay_host.empty()) {
            out << d.domain_name << " smtp:[" << d.relay_host << "]\n";
        }

        // Disabled: no entry — Postfix rejects by default
    }
    return {true, ""};
}

core::OperationResult DockerMailProvider::write_opendkim_config(
    const std::vector<MailDomain>& domains) {
    std::string conf_dir = config_dir() + "/generated/opendkim";

    // opendkim.conf — main configuration
    std::string conf_path = conf_dir + "/opendkim.conf";
    std::ofstream conf_out(conf_path);
    if (!conf_out.is_open()) return {false, "Failed to write " + conf_path};
    conf_out << "# ContainerCP generated OpenDKIM configuration\n"
             << "Syslog                  yes\n"
             << "UMask                   002\n"
             << "UserID                  opendkim\n"
             << "KeyTable                /etc/opendkim/KeyTable\n"
             << "SigningTable            /etc/opendkim/SigningTable\n"
             << "InternalHosts           127.0.0.0/8, 192.168.0.0/16, 172.16.0.0/12, 10.0.0.0/8\n"
             << "Canonicalization        relaxed/simple\n"
             << "Mode                    sv\n"
             << "SubDomains              no\n"
             << "AutoRestart             yes\n"
             << "AutoRestartRate         10/1h\n"
             << "Background              no\n"
             << "DNSTimeout              5\n"
             << "SignatureAlgorithm      rsa-sha256\n"
             << "Socket                  inet:8891\n"
             << "PidFile                 /var/run/opendkim/opendkim.pid\n";

    // KeyTable — maps selector@domain to key file
    std::string kt_path = conf_dir + "/KeyTable";
    std::ofstream kt_out(kt_path);
    if (!kt_out.is_open()) return {false, "Failed to write " + kt_path};

    // SigningTable — maps email patterns to signing rules
    std::string st_path = conf_dir + "/SigningTable";
    std::ofstream st_out(st_path);
    if (!st_out.is_open()) return {false, "Failed to write " + st_path};

    bool has_keys = false;
    for (const auto& d : domains) {
        if (!d.enabled || d.mode == MailDomainMode::Disabled) continue;
        std::string host_key_path = config_dir() + "/state/dkim/"
            + d.domain_name + "/" + d.dkim_selector + ".private";
        std::string container_key_path = "/etc/opendkim/keys/"
            + d.domain_name + "/" + d.dkim_selector + ".private";

        // Only add if key file exists
        auto check = executor_.run({"test", "-f", host_key_path});
        if (check.exit_code != 0) continue;

        has_keys = true;
        kt_out << d.dkim_selector << "._domainkey." << d.domain_name
               << " " << d.domain_name << ":" << d.dkim_selector
               << ":" << container_key_path << "\n";
        st_out << "*@" << d.domain_name
               << " " << d.dkim_selector << "._domainkey." << d.domain_name << "\n";
    }

    if (!has_keys) {
        // Write placeholder — OpenDKIM needs the files to exist even if empty
        kt_out << "# No DKIM keys configured\n";
        st_out << "# No DKIM keys configured\n";
    }

    return {true, ""};
}

core::OperationResult DockerMailProvider::write_configs(
    const std::vector<MailDomain>& domains,
    const MailboxManager& mailboxes,
    const MailAliasManager& aliases) {
    (void)aliases;
    auto pf = write_postfix_config(domains, mailboxes, aliases);
    if (!pf.success) return pf;
    auto dv = write_dovecot_config(domains, mailboxes);
    if (!dv.success) return dv;
    auto tm = write_transport_maps(domains, mailboxes);
    if (!tm.success) return tm;
    auto dkim = write_opendkim_config(domains);
    if (!dkim.success) return dkim;
    logger_.info("MAIL", "Configuration files written");
    return {true, "Configuration written"};
}

// ── Docker Compose management ─────────────────────────────────────

core::OperationResult DockerMailProvider::write_docker_compose() {
    std::ostringstream yml;
    yml << "services:\n"
        << "  postfix:\n"
        << "    image: ghcr.io/containercp/mail-postfix:latest\n"
        << "    container_name: containercp-mail-postfix\n"
        << "    restart: unless-stopped\n"
        << "    dns:\n"
        << "      - 8.8.8.8\n"
        << "      - 8.8.4.4\n"
        << "    ports:\n"
        << "      - 127.0.0.1:25:25\n"
        << "      - 127.0.0.1:465:465\n"
        << "      - 127.0.0.1:587:587\n"
        << "    networks:\n"
        << "      - containercp-mail\n"
        << "    volumes:\n"
        << "      - " << config_dir() << "/generated/postfix-main.cf:/etc/postfix/main.cf:ro\n"
        << "      - " << config_dir() << "/generated/transport_maps:/etc/postfix/transport_maps:ro\n"
         << "      - " << config_dir() << "/generated/virtual_mailboxes:/etc/postfix/virtual_mailboxes:ro\n"
        << "      - " << config_dir() << "/generated/virtual_aliases:/etc/postfix/virtual_aliases:ro\n"
        << "      - " << config_dir() << "/generated/postfix-resolv.conf:/etc/postfix/resolv.conf:ro\n"
        << "      - " << config_dir() << "/generated/sasl_passwd:/etc/postfix/sasl_passwd:ro\n"
        << "      - " << config_dir() << "/custom/postfix/:/etc/postfix/custom/:ro\n"
        << "      - " << data_root_ << "/ssl/:/srv/containercp/ssl/:ro\n"
        << "    depends_on:\n"
        << "      - redis\n"
        << "  dovecot:\n"
        << "    image: ghcr.io/containercp/mail-dovecot:latest\n"
        << "    container_name: containercp-mail-dovecot\n"
        << "    restart: unless-stopped\n"
        << "    ports:\n"
        << "      - 127.0.0.1:143:143\n"
        << "      - 127.0.0.1:993:993\n"
        << "    networks:\n"
        << "      - containercp-mail\n"
        << "    volumes:\n"
        << "      - " << config_dir() << "/generated/dovecot.conf:/etc/dovecot/dovecot.conf:ro\n"
        << "      - " << config_dir() << "/custom/dovecot/:/etc/dovecot/custom/:ro\n"
        << "      - " << config_dir() << "/state/:/var/mail/:rw\n"
        << "      - " << data_root_ << "/ssl/:/srv/containercp/ssl/:ro\n"
        << "    depends_on:\n"
        << "      - redis\n"
        << "  opendkim:\n"
        << "    image: ghcr.io/containercp/mail-opendkim:latest\n"
        << "    container_name: containercp-mail-opendkim\n"
        << "    restart: unless-stopped\n"
        << "    networks:\n"
        << "      - containercp-mail\n"
        << "    volumes:\n"
        << "      - " << config_dir() << "/generated/opendkim/opendkim.conf:/etc/opendkim/opendkim.conf:ro\n"
        << "      - " << config_dir() << "/generated/opendkim/KeyTable:/etc/opendkim/KeyTable:ro\n"
        << "      - " << config_dir() << "/generated/opendkim/SigningTable:/etc/opendkim/SigningTable:ro\n"
        << "      - " << config_dir() << "/state/dkim/:/etc/opendkim/keys/:ro\n"
        << "  redis:\n"
        << "    image: redis:7-alpine\n"
        << "    container_name: containercp-mail-redis\n"
        << "    restart: unless-stopped\n"
        << "    networks:\n"
        << "      - containercp-mail\n"
        << "    volumes:\n"
        << "      - redis-data:/data\n"
        << "networks:\n"
        << "  containercp-mail:\n"
        << "    external: true\n"
        << "volumes:\n"
        << "  redis-data:\n";

    std::string path = compose_dir() + "/docker-compose.yml";
    std::ofstream out(path);
    if (!out.is_open()) return {false, "Failed to write docker-compose.yml"};
    out << yml.str();
    return {true, ""};
}

core::OperationResult DockerMailProvider::start() {
    auto dc = write_docker_compose();
    if (!dc.success) return dc;

    auto result = executor_.run({
        "docker", "compose",
        "--project-directory", compose_dir(),
        "up", "-d"
    });
    if (result.exit_code != 0) {
        logger_.error("MAIL", "Docker compose up failed: " + result.err);
        return {false, "Failed to start mail containers: " + result.err};
    }

    logger_.info("MAIL", "Mail containers started");
    return {true, "Mail stack started"};
}

core::OperationResult DockerMailProvider::stop() {
    auto result = executor_.run({
        "docker", "compose",
        "--project-directory", compose_dir(),
        "down"
    });
    if (result.exit_code != 0) {
        logger_.warning("MAIL", "Docker compose down warning: " + result.err);
    }
    logger_.info("MAIL", "Mail containers stopped");
    return {true, "Mail stack stopped"};
}

core::OperationResult DockerMailProvider::reload() {
    // Signal Postfix to reload configuration
    auto result = executor_.run({
        "docker", "exec", "containercp-mail-postfix",
        "postfix", "reload"
    });
    if (result.exit_code != 0) {
        return {false, "Failed to reload Postfix: " + result.err};
    }
    return {true, "Configuration reloaded"};
}

bool DockerMailProvider::is_running() const {
    auto result = executor_.run({
        "docker", "inspect",
        "--format", "{{.State.Running}}",
        "containercp-mail-postfix"
    });
    return result.exit_code == 0 &&
           result.out.find("true") != std::string::npos;
}

std::string DockerMailProvider::status_description() const {
    if (is_running()) return "running";
    return "stopped";
}

runtime::ServiceHealth DockerMailProvider::check_service(
    const std::string& container_name,
    const std::string& service_name) const {
    runtime::ServiceHealth h;
    h.name = service_name;

    auto result = executor_.run({
        "docker", "inspect",
        "--format", "{{.State.Running}}",
        container_name
    });

    if (result.exit_code != 0) {
        h.status = "error";
        h.message = "container not found";
    } else if (result.out.find("true") != std::string::npos) {
        h.status = "ok";
        h.message = "running";
    } else {
        h.status = "error";
        h.message = "stopped";
    }
    return h;
}

runtime::HealthReport DockerMailProvider::check_health() const {
    runtime::HealthReport report;
    report.services.push_back(check_service("containercp-mail-postfix", "postfix"));
    report.services.push_back(check_service("containercp-mail-dovecot", "dovecot"));
    report.services.push_back(check_service("containercp-mail-redis", "redis"));

    // Process-level checks for services that support them
    // These verify the daemon is actually responding, not just the container
    auto postfix_proc = executor_.run({
        "docker", "exec", "containercp-mail-postfix",
        "postfix", "status"
    });
    runtime::ServiceHealth postfix_ready;
    postfix_ready.name = "postfix-process";
    if (postfix_proc.exit_code == 0) {
        postfix_ready.status = "ok";
        postfix_ready.message = "postfix status ok";
    } else {
        postfix_ready.status = "degraded";
        postfix_ready.message = "postfix status check failed: " + postfix_proc.err;
    }
    report.services.push_back(postfix_ready);

    auto dovecot_proc = executor_.run({
        "docker", "exec", "containercp-mail-dovecot",
        "doveadm", "who"
    });
    runtime::ServiceHealth dovecot_ready;
    dovecot_ready.name = "dovecot-process";
    if (dovecot_proc.exit_code == 0) {
        dovecot_ready.status = "ok";
        dovecot_ready.message = "dovecot responding";
    } else {
        dovecot_ready.status = "degraded";
        dovecot_ready.message = "dovecot check failed: " + dovecot_proc.err;
    }
    report.services.push_back(dovecot_ready);

    // Aggregate status
    bool has_error = false;
    bool has_degraded = false;
    for (const auto& s : report.services) {
        if (s.status == "error") has_error = true;
        if (s.status == "degraded") has_degraded = true;
    }
    if (has_error) report.status = "error";
    else if (has_degraded) report.status = "degraded";
    else report.status = "ok";
    return report;
}

ApplyResult DockerMailProvider::apply_config(
    const std::vector<MailDomain>& domains,
    const MailboxManager& mailboxes,
    const MailAliasManager& aliases) {
    // Serialise concurrent applies
    std::lock_guard<std::mutex> lock(apply_mutex_);

    ApplyResult result;

    // Helper: find domain name by ID
    auto domain_name_of = [&](uint64_t domain_id) -> std::string {
        for (const auto& d : domains) {
            if (d.id == domain_id) return d.domain_name;
        }
        return "";
    };

    // 1. Read current config files into memory (for rollback)
    std::string gen_dir = config_dir() + "/generated";
    auto read_file = [](const std::string& path) -> std::string {
        std::ifstream in(path);
        if (!in.is_open()) return "";
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    auto write_file = [](const std::string& path, const std::string& content) -> bool {
        std::ofstream out(path);
        if (!out.is_open()) return false;
        out << content;
        return true;
    };

    // Backup: read files before we overwrite them
    // Use a subset so we only restore what we actually change
    std::vector<std::pair<std::string, std::string>> backup;
    auto backup_if_exists = [&](const std::string& fname) {
        std::string content = read_file(gen_dir + "/" + fname);
        if (!content.empty()) backup.emplace_back(fname, content);
    };
    backup_if_exists("postfix-main.cf");
    backup_if_exists("dovecot.conf");
    backup_if_exists("transport_maps");
    backup_if_exists("virtual_mailboxes");
    backup_if_exists("passwd");
    backup_if_exists("virtual_aliases");

    auto restore = [&]() {
        for (const auto& [fname, content] : backup) {
            write_file(gen_dir + "/" + fname, content);
        }
    };

    // 2. Generate new config files
    auto cfg = write_configs(domains, mailboxes, aliases);
    if (!cfg.success) {
        result.message = cfg.message;
        result.failed_stage = "generate";
        return result;
    }

    // 3. Validate Postfix config
    auto pf_check = executor_.run({
        "docker", "exec", "containercp-mail-postfix",
        "postfix", "check"
    });
    if (pf_check.exit_code != 0) {
        restore();
        result.message = "Postfix config validation failed: " + pf_check.err;
        result.failed_stage = "validate";
        logger_.error("MAIL", result.message);
        return result;
    }

    // 4. Validate alias map (if any aliases exist)
    bool has_aliases = false;
    for (const auto& a : aliases.list()) { if (a.enabled) { has_aliases = true; break; } }
    if (has_aliases) {
        // Get the first enabled alias for validation
        std::string test_source, test_domain;
        for (const auto& a : aliases.list()) {
            if (!a.enabled) continue;
            test_source = a.source_local_part;
            test_domain = domain_name_of(a.domain_id);
            if (!test_domain.empty()) break;
        }
        if (!test_source.empty() && !test_domain.empty()) {
            auto alias_check = executor_.run({
                "docker", "exec", "containercp-mail-postfix",
                "postmap", "-q",
                test_source + "@" + test_domain,
                "texthash:/etc/postfix/virtual_aliases"
            });
            if (alias_check.exit_code != 0) {
                restore();
                result.message = "Alias lookup validation failed for "
                    + test_source + "@" + test_domain + ": " + alias_check.err;
                result.failed_stage = "validate";
                logger_.error("MAIL", result.message);
                return result;
            }
        }
    }

    // 5. Reload Postfix
    auto rl = reload();
    if (!rl.success) {
        restore();
        result.message = rl.message;
        result.failed_stage = "reload";
        logger_.error("MAIL", result.message);
        return result;
    }

    result.success = true;
    result.message = "Configuration applied and validated";
    logger_.info("MAIL", result.message);
    return result;
}

} // namespace containercp::mail
