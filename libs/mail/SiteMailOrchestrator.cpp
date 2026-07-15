#include "SiteMailOrchestrator.h"
#include "filesystem/SiteLayout.h"
#include "runtime/CommandExecutor.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace containercp::mail {

SiteMailOrchestrator::SiteMailOrchestrator(SiteMailCredentials& credentials,
                                             runtime::Runtime& rt,
                                             filesystem::Filesystem& fs,
                                             config::Config& cfg)
    : credentials_(credentials)
    , rt_(rt)
    , fs_(fs)
    , cfg_(cfg)
{
}

static std::string generate_msmtprc(const std::string& username,
                                     const std::string& password,
                                     const std::string& envelope_sender) {
    std::ostringstream ms;
    ms << "# ContainerCP PHP msmtp configuration\n"
       << "# Managed by SiteMailOrchestrator — do not edit manually\n"
       << "\n"
       << "defaults\n"
       << "auth           on\n"
       << "tls            on\n"
       << "tls_certcheck  off\n"
       << "tls_starttls   on\n"
       << "\n"
       << "account        default\n"
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
    const std::string& envelope_sender) {

    // 1. Ensure config/php/ directory exists
    std::string site_dir = cfg_.sites_dir() + domain + "/";
    fs_.create_directory(site_dir + "config/");
    fs_.create_directory(site_dir + "config/php/");

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

    // 5. Connect PHP container to mail network
    auto net_result = rt_.connect_mail_network(site_id, domain);
    if (!net_result.success) {
        return net_result;
    }

    // 6. Sync mail config (reload Postfix + Dovecot)
    auto sync_result = rt_.sync_site_mail(site_id);
    if (!sync_result.success) {
        return sync_result;
    }

    return {true, ""};
}

core::OperationResult SiteMailOrchestrator::disable_mail(
    uint64_t site_id, const std::string& domain) {

    // 1. Remove credentials from Dovecot + Postfix
    bool cred_removed = credentials_.remove(site_id);
    if (!cred_removed) {
        // Non-fatal: credentials may not exist; continue cleanup
    }

    // 2. Remove msmtprc
    if (!domain.empty()) {
        std::string site_dir = cfg_.sites_dir() + domain + "/";
        std::string msmtprc_path = site_dir + "config/php/msmtprc";
        std::remove(msmtprc_path.c_str());
    }

    // 3. Disconnect PHP container from mail network
    if (!domain.empty()) {
        auto net = rt_.disconnect_mail_network(site_id, domain);
        if (!net.success) {
            return net;
        }
    }

    // 4. Sync mail config (reload Postfix + Dovecot)
    auto sync = rt_.sync_site_mail(site_id);
    if (!sync.success) {
        return sync;
    }

    return {true, ""};
}

SiteMailStatus SiteMailOrchestrator::get_status(uint64_t site_id,
                                                  const std::string& domain,
                                                  bool enabled) {
    SiteMailStatus status;
    status.enabled = enabled;

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
