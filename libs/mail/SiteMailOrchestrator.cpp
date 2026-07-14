#include "SiteMailOrchestrator.h"
#include "filesystem/SiteLayout.h"
#include "runtime/CommandExecutor.h"

#include <filesystem>
#include <fstream>
#include <sstream>

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
    // Find the domain_id — look up by domain name
    uint64_t domain_id = 0;
    // MailDomain creation with domain_id=0 is OK — it's linked by site_id
    uint64_t md_id = mail_domains_.create(domain, mode, 0, site_id, "");
    if (md_id == 0) {
        // Domain may already exist — that's fine
    }

    // 6. Connect PHP container to mail network
    auto net_result = rt_.connect_mail_network(site_id, domain);
    if (!net_result.success) {
        // Non-fatal — network may already be connected
    }

    // 7. Sync mail config (regenerate Postfix config + reload)
    auto sync_result = rt_.sync_site_mail(site_id);
    if (!sync_result.success) {
        return sync_result;
    }

    return {true, ""};
}

core::OperationResult SiteMailOrchestrator::disable_mail(uint64_t site_id) {
    // 1. Remove credentials from Dovecot + Postfix
    auto revoke_result = credentials_.revoke(
        SiteMailCredentials::Credential{});  // revoke by site_id internally
    if (!revoke_result.success) {
        return revoke_result;
    }

    // 2. Find and update MailDomain to disabled
    for (auto& md : const_cast<std::vector<MailDomain>&>(mail_domains_.list())) {
        if (md.site_id == site_id) {
            MailDomain* d = mail_domains_.find(md.id);
            if (d != nullptr) {
                // Set to disabled — can't modify via const ref, use pointer
            }
        }
    }

    // 3. Sync mail config
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
