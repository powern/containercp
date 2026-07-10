#include "DockerMailProvider.h"
#include "api/JsonFormatter.h"

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

std::string DockerMailProvider::container_name(const std::string& service) {
    return "containercp-mail-" + service;
}

core::OperationResult DockerMailProvider::ensure_directories() {
    std::string cmd = "mkdir -p " + config_dir() + "/generated "
                    + config_dir() + "/custom "
                    + config_dir() + "/custom/postfix "
                    + config_dir() + "/custom/dovecot "
                    + config_dir() + "/state";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        return {false, "Failed to create mail directories"};
    }
    return {true, ""};
}

core::OperationResult DockerMailProvider::ensure_network() {
    std::string cmd = "docker network inspect containercp-mail > /dev/null 2>&1"
                      " || docker network create containercp-mail > /dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        return {false, "Failed to create Docker network"};
    }
    return {true, ""};
}

std::string DockerMailProvider::generate_compose() const {
    std::ostringstream yml;
    yml << "services:\n";

    // Postfix
    yml << "  postfix:\n"
        << "    image: ghcr.io/containercp/mail-postfix:latest\n"
        << "    container_name: " << container_name("postfix") << "\n"
        << "    restart: unless-stopped\n"
        << "    network_mode: service:redis\n"
        << "    volumes:\n"
        << "      - " << config_dir() << "/generated/postfix-main.cf:/etc/postfix/main.cf:ro\n"
        << "      - " << config_dir() << "/custom/postfix/:/etc/postfix/custom/:ro\n"
        << "    depends_on:\n"
        << "      - redis\n";

    // Dovecot
    yml << "  dovecot:\n"
        << "    image: ghcr.io/containercp/mail-dovecot:latest\n"
        << "    container_name: " << container_name("dovecot") << "\n"
        << "    restart: unless-stopped\n"
        << "    network_mode: service:redis\n"
        << "    volumes:\n"
        << "      - " << config_dir() << "/generated/dovecot.conf:/etc/dovecot/dovecot.conf:ro\n"
        << "      - " << config_dir() << "/custom/dovecot/:/etc/dovecot/custom/:ro\n"
        << "      - " << config_dir() << "/state/:/var/mail/:rw\n"
        << "    depends_on:\n"
        << "      - redis\n";

    // Redis (cache, Rspamd backend)
    yml << "  redis:\n"
        << "    image: redis:7-alpine\n"
        << "    container_name: " << container_name("redis") << "\n"
        << "    restart: unless-stopped\n"
        << "    networks:\n"
        << "      - containercp-mail\n"
        << "    volumes:\n"
        << "      - redis-data:/data\n";

    yml << "networks:\n"
        << "  containercp-mail:\n"
        << "    external: true\n";

    yml << "volumes:\n"
        << "  redis-data:\n";

    return yml.str();
}

core::OperationResult DockerMailProvider::apply_config(
    const std::vector<MailDomain>& domains,
    const MailboxManager& mailboxes,
    const MailAliasManager& aliases) {
    (void)aliases;
    // Generate Postfix main.cf
    std::ostringstream pf;
    pf << "# ContainerCP generated Postfix configuration\n"
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
    pf << "\n";

    // Virtual mailbox maps
    pf << "virtual_mailbox_maps = texthash:/etc/postfix/virtual_mailboxes\n";

    // Write Postfix config
    std::string pf_path = config_dir() + "/generated/postfix-main.cf";
    std::ofstream pf_out(pf_path);
    if (!pf_out.is_open()) return {false, "Failed to write Postfix config"};
    pf_out << pf.str();

    // Write virtual mailbox map
    std::string vmb_path = config_dir() + "/generated/virtual_mailboxes";
    std::ofstream vmb_out(vmb_path);
    if (!vmb_out.is_open()) return {false, "Failed to write virtual mailbox map"};
    for (const auto& mb : mailboxes.list()) {
        std::string domain_name;
        for (const auto& d : domains) {
            if (d.id == mb.domain_id) { domain_name = d.domain_name; break; }
        }
        if (domain_name.empty()) continue;
        vmb_out << mb.local_part << "@" << domain_name << " " << domain_name << "/" << mb.local_part << "/\n";
    }

    // Generate Dovecot config
    std::ostringstream dv;
    dv << "# ContainerCP generated Dovecot configuration\n"
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
       << "service auth {\n"
       << "  unix_listener auth-userdb {\n"
       << "    mode = 0660\n"
       << "  }\n"
       << "}\n"
       << "protocols = imap pop3 lmtp\n";

    std::string dv_path = config_dir() + "/generated/dovecot.conf";
    std::ofstream dv_out(dv_path);
    if (!dv_out.is_open()) return {false, "Failed to write Dovecot config"};
    dv_out << dv.str();

    // Write passwd file for Dovecot SASL auth
    std::string pw_path = config_dir() + "/generated/passwd";
    std::ofstream pw_out(pw_path);
    if (!pw_out.is_open()) return {false, "Failed to write passwd file"};
    for (const auto& mb : mailboxes.list()) {
        std::string domain_name;
        for (const auto& d : domains) {
            if (d.id == mb.domain_id) { domain_name = d.domain_name; break; }
        }
        if (domain_name.empty() || !mb.enabled) continue;
        pw_out << mb.local_part << "@" << domain_name
               << ":" << mb.password_hash
               << ":1000:1000::/var/mail/" << domain_name << "/" << mb.local_part << "/::\n";
    }

    return {true, "Configuration applied"};
}

core::OperationResult DockerMailProvider::start() {
    auto dirs = ensure_directories();
    if (!dirs.success) return dirs;

    auto net = ensure_network();
    if (!net.success) return net;

    // Write docker-compose.yml
    std::string compose_path = compose_dir() + "/docker-compose.yml";
    std::ofstream compose_out(compose_path);
    if (!compose_out.is_open()) return {false, "Failed to write docker-compose.yml"};
    compose_out << generate_compose();

    // Run docker compose up -d
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

bool DockerMailProvider::is_running() const {
    // Check if the postfix container is running
    auto result = executor_.run({
        "docker", "inspect",
        "--format", "{{.State.Running}}",
        container_name("postfix")
    });
    return result.exit_code == 0 && result.out.find("true") != std::string::npos;
}

std::string DockerMailProvider::status_description() const {
    if (is_running()) return "running";
    return "stopped";
}

} // namespace containercp::mail
