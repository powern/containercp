#include "DockerMailProvider.h"

#include <fstream>
#include <sstream>

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
        config_dir() + "/state"
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

    return {true, ""};
}

// ── Configuration generation (no Docker/runtime logic) ─────────────

core::OperationResult DockerMailProvider::write_postfix_config(
    const std::vector<MailDomain>& domains,
    const MailboxManager& mailboxes) {
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
       << "virtual_mailbox_domains = ";
    bool first = true;
    for (const auto& d : domains) {
        if (d.mode != MailDomainMode::LocalPrimary && d.mode != MailDomainMode::SplitM365) continue;
        if (!d.enabled) continue;
        if (!first) pf << ", ";
        first = false;
        pf << d.domain_name;
    }
    pf << "\n"
       << "virtual_mailbox_maps = texthash:/etc/postfix/virtual_mailboxes\n";

    std::string path = config_dir() + "/generated/postfix-main.cf";
    std::ofstream out(path);
    if (!out.is_open()) return {false, "Failed to write " + path};
    out << pf.str();

    // Virtual mailbox map
    std::string vmb_path = config_dir() + "/generated/virtual_mailboxes";
    std::ofstream vmb_out(vmb_path);
    if (!vmb_out.is_open()) return {false, "Failed to write " + vmb_path};
    for (const auto& mb : mailboxes.list()) {
        std::string domain;
        for (const auto& d : domains) {
            if (d.id == mb.domain_id) { domain = d.domain_name; break; }
        }
        if (domain.empty()) continue;
        vmb_out << mb.local_part << "@" << domain << " "
                << domain << "/" << mb.local_part << "/\n";
    }
    return {true, ""};
}

core::OperationResult DockerMailProvider::write_dovecot_config(
    const std::vector<MailDomain>& domains,
    const MailboxManager& mailboxes) {
    std::ostringstream dv;
    dv << "# ContainerCP generated configuration\n"
       << "# Do not edit manually — changes will be overwritten.\n\n"
       << "mail_location = maildir:/var/mail/%d/%n\n"
       << "passdb {\n"
       << "  driver = passwd-file\n"
       << "  args = /etc/dovecot/passwd\n"
       << "}\n"
       << "userdb {\n"
       << "  driver = static\n"
       << "  args = uid=1000 gid=1000 home=/var/mail/%d/%n\n"
       << "}\n"
       << "protocols = imap pop3 lmtp\n";

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

core::OperationResult DockerMailProvider::write_configs(
    const std::vector<MailDomain>& domains,
    const MailboxManager& mailboxes,
    const MailAliasManager& aliases) {
    (void)aliases;
    auto pf = write_postfix_config(domains, mailboxes);
    if (!pf.success) return pf;
    auto dv = write_dovecot_config(domains, mailboxes);
    if (!dv.success) return dv;
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
        << "    network_mode: service:redis\n"
        << "    volumes:\n"
        << "      - " << config_dir() << "/generated/postfix-main.cf:/etc/postfix/main.cf:ro\n"
        << "      - " << config_dir() << "/custom/postfix/:/etc/postfix/custom/:ro\n"
        << "    depends_on:\n"
        << "      - redis\n"
        << "  dovecot:\n"
        << "    image: ghcr.io/containercp/mail-dovecot:latest\n"
        << "    container_name: containercp-mail-dovecot\n"
        << "    restart: unless-stopped\n"
        << "    network_mode: service:redis\n"
        << "    volumes:\n"
        << "      - " << config_dir() << "/generated/dovecot.conf:/etc/dovecot/dovecot.conf:ro\n"
        << "      - " << config_dir() << "/custom/dovecot/:/etc/dovecot/custom/:ro\n"
        << "      - " << config_dir() << "/state/:/var/mail/:rw\n"
        << "    depends_on:\n"
        << "      - redis\n"
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

} // namespace containercp::mail
