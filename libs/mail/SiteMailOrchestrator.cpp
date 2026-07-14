#include "SiteMailOrchestrator.h"
#include "filesystem/SiteLayout.h"
#include "runtime/CommandExecutor.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

// Add or remove containercp-mail network from a site's docker-compose.yml.
// Ensures PHP container reconnects after docker compose up.
static bool update_compose_mail_network(const std::string& compose_path, bool add) {
    std::ifstream in(compose_path);
    if (!in) return false;

    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    bool changed = false;

    if (add) {
        // Check if containercp-mail is already in php networks
        if (content.find("- containercp-mail") == std::string::npos) {
            // Add to php service networks (after containercp-site-{ID})
            std::string marker = "      - containercp-site-";
            auto pos = content.find(marker);
            if (pos != std::string::npos) {
                auto nl = content.find('\n', pos);
                if (nl != std::string::npos) {
                    content.insert(nl + 1, "      - containercp-mail\n");
                    changed = true;
                }
            }
        }
        // Check if containercp-mail is in top-level networks
        if (content.find("  containercp-mail:") == std::string::npos) {
            auto net_pos = content.find("\nnetworks:");
            if (net_pos != std::string::npos) {
                content.insert(net_pos + 1,
                    "  containercp-mail:\n"
                    "    external: true\n");
                changed = true;
            }
        }
    } else {
        // Remove containercp-mail from php service networks
        std::string line = "      - containercp-mail\n";
        auto pos = content.find(line);
        while (pos != std::string::npos) {
            content.erase(pos, line.length());
            pos = content.find(line);
            changed = true;
        }
        // Remove from top-level networks
        std::string net_block = "  containercp-mail:\n    external: true\n";
        pos = content.find(net_block);
        if (pos != std::string::npos) {
            content.erase(pos, net_block.length());
            changed = true;
        }
    }

    if (changed) {
        std::ofstream out(compose_path);
        if (!out) return false;
        out << content;
    }
    return true;
}

} // anonymous namespace

namespace containercp::mail {

SiteMailOrchestrator::SiteMailOrchestrator(MailDomainManager& mail_domains,
                                             SiteMailCredentials& credentials,
                                             runtime::Runtime& rt,
                                             filesystem::Filesystem& fs,
                                             config::Config& cfg)
    : mail_domains_(mail_domains)
    , credentials_(credentials)
    , rt_(rt)
    , fs_(fs)
    , cfg_(cfg)
{
}

static std::string generate_msmtprc(const std::string& username,
                                     const std::string& password,
                                     const std::string& envelope_sender) {
    std::ostringstream ms;
    ms << "defaults\n"
       << "auth           on\n"
       << "tls            on\n"
       << "host           containercp-mail-postfix\n"
       << "port           587\n"
       << "user           " << username << "\n"
       << "password       " << password << "\n"
       << "from           " << envelope_sender << "\n"
       << "allow_from_override off\n"
       << "set_from_header auto\n";
    return ms.str();
}

core::OperationResult SiteMailOrchestrator::enable_mail(
    uint64_t site_id, const std::string& domain,
    const std::string& envelope_sender, MailDomainMode mode) {

    // 1. Find the site's directory
    std::string site_dir = cfg_.sites_dir() + domain + "/";
    if (!fs_.exists(site_dir)) {
        // Create config/php/ if it doesn't exist (e.g., for sites created before Stage 3)
        fs_.create_directory(site_dir + "config/php/");
    }

    // 2. Generate credentials
    auto cred = credentials_.generate(site_id, domain);
    auto apply_result = credentials_.apply(cred);
    if (!apply_result.success) {
        return apply_result;
    }

    // 3. Determine envelope sender (default: wordpress@domain)
    std::string sender = envelope_sender.empty()
        ? "wordpress@" + domain
        : envelope_sender;

    // 4. Write msmtprc
    std::string msmtprc_path = site_dir + "config/php/msmtprc";
    std::string msmtprc_content = generate_msmtprc(cred.username, cred.password, sender);
    if (!fs_.create_file(msmtprc_path, msmtprc_content)) {
        credentials_.revoke(cred);
        return {false, "Failed to write msmtprc"};
    }

    // 5. Create MailDomain record
    // domain_id=0 is OK — linked by site_id
    mail_domains_.create(domain, mode, 0, site_id, "");

    // 6. Update site's docker-compose.yml so mail network persists across recreate
    std::string compose_path = site_dir + "docker-compose.yml";
    update_compose_mail_network(compose_path, true);

    // 7. Connect PHP container to mail network (immediate, runtime)
    auto net_result = rt_.connect_mail_network(site_id, domain);
    if (!net_result.success) {
        return net_result;
    }

    // 8. Sync mail config (reload Postfix + Dovecot)
    auto sync_result = rt_.sync_site_mail(site_id);
    if (!sync_result.success) {
        return sync_result;
    }

    return {true, ""};
}

core::OperationResult SiteMailOrchestrator::disable_mail(uint64_t site_id) {
    // Find domain for this site (needed for file paths)
    std::string domain;
    for (const auto& md : mail_domains_.list()) {
        if (md.site_id == site_id) {
            domain = md.domain_name;
            break;
        }
    }

    // 1. Remove credentials from Dovecot + Postfix
    auto revoke_result = credentials_.revoke(
        SiteMailCredentials::Credential{});
    if (!revoke_result.success) {
        return revoke_result;
    }

    // 2. Remove msmtprc
    if (!domain.empty()) {
        std::string msmtprc_path = cfg_.sites_dir() + domain + "/config/php/msmtprc";
        std::remove(msmtprc_path.c_str());

        // 3. Remove containercp-mail from site's docker-compose.yml
        std::string compose_path = cfg_.sites_dir() + domain + "/docker-compose.yml";
        update_compose_mail_network(compose_path, false);
    }

    // 4. Find and update MailDomain to disabled
    for (auto& md : const_cast<std::vector<MailDomain>&>(mail_domains_.list())) {
        if (md.site_id == site_id) {
            MailDomain* d = mail_domains_.find(md.id);
            if (d != nullptr) {
                d->mode = MailDomainMode::Disabled;
                d->enabled = false;
            }
            break;
        }
    }

    // 5. Disconnect PHP container from mail network
    if (!domain.empty()) {
        rt_.disconnect_mail_network(site_id, domain);
    }

    // 6. Sync mail config
    rt_.sync_site_mail(site_id);

    return {true, ""};
}

SiteMailStatus SiteMailOrchestrator::get_status(uint64_t site_id,
                                                  const std::string& domain) {
    SiteMailStatus status;

    // Check if MailDomain exists
    for (const auto& md : mail_domains_.list()) {
        if (md.site_id == site_id) {
            status.enabled = (md.mode != MailDomainMode::Disabled);
            status.domain_id = md.id;
            break;
        }
    }

    // Check credentials
    auto cred = credentials_.find(site_id);
    if (cred.has_value()) {
        status.credential_exists = true;
        status.username = cred->username;
    }

    // Check msmtprc
    std::string msmtprc_path = cfg_.sites_dir() + domain + "/config/php/msmtprc";
    status.msmtprc_exists = fs_.exists(msmtprc_path);

    // Read envelope sender from msmtprc
    if (status.msmtprc_exists) {
        std::ifstream in(msmtprc_path);
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("from ") == 0) {
                status.envelope_sender = line.substr(5);
                break;
            }
        }
    }

    // Check network
    runtime::CommandExecutor exec;
    auto net_check = exec.run({
        "docker", "inspect", "site-" + std::to_string(site_id) + "-php",
        "--format", "{{range $k,$v:=.NetworkSettings.Networks}}{{$k}} {{end}}"
    });
    if (net_check.exit_code == 0 &&
        net_check.out.find("containercp-mail") != std::string::npos) {
        status.network_connected = true;
    }

    return status;
}

} // namespace containercp::mail
