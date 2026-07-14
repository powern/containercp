#include "SiteMailOrchestrator.h"
#include "filesystem/SiteLayout.h"
#include "runtime/CommandExecutor.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace containercp::mail {
namespace {

// Repair a site's docker-compose.yml to have containercp-mail correctly
// configured: in php service networks + top-level networks section.
// Removes wrong entries from web service, volumes section, etc.
// Validates with 'docker compose config' before overwriting.
static core::OperationResult repair_compose_mail_network(
    const std::string& site_dir, uint64_t site_id, bool add) {

    std::string compose_path = site_dir + "docker-compose.yml";

    std::ifstream in(compose_path);
    if (!in) {
        return {true, ""};  // No compose file
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    // ── Step 1: Remove ALL wrong containercp-mail entries ──
    bool changed = false;

    // Remove from ANY service's networks (web, volumes — wherever it leaked)
    auto remove_all = [&](const std::string& pattern) {
        auto p = content.find(pattern);
        while (p != std::string::npos) {
            content.erase(p, pattern.length());
            p = content.find(pattern);
            changed = true;
        }
    };
    remove_all("      - containercp-mail\n");
    // Remove from volumes section or anywhere else as a top-level declaration
    remove_all("  containercp-mail:\n    external: true\n");
    remove_all("  containercp-mail:\n");

    // ── Step 2: If adding, insert correctly ──
    if (add) {
        // 2a. Add to PHP service networks.
        //     Find PHP by its container_name: site-{ID}-php
        std::string php_marker = "container_name: site-" + std::to_string(site_id) + "-php";
        auto php_pos = content.find(php_marker);
        if (php_pos == std::string::npos) {
            return {false, "Cannot find PHP container (" + php_marker + ") in compose file"};
        }
        // Find the "    networks:" line under the PHP service
        auto net_section = content.find("\n    networks:\n", php_pos);
        if (net_section == std::string::npos) {
            return {false, "Cannot find networks section in PHP service"};
        }
        // Insert at end of networks list (before next key at same indentation)
        auto net_end = content.find("\n    environment:", net_section);
        if (net_end == std::string::npos) {
            net_end = content.find("\n    labels:", net_section);
        }
        if (net_end == std::string::npos) {
            return {false, "Cannot find end of PHP networks section"};
        }
        content.insert(net_end, "      - containercp-mail\n");
        changed = true;

        // 2b. Add top-level network declaration under networks:
        //     Find the LAST \nnetworks: (top-level, after volumes:)
        auto top_net = content.rfind("\nnetworks:");
        if (top_net == std::string::npos) {
            return {false, "Cannot find top-level networks section in compose file"};
        }
        // Find the last indented entry under this networks section
        // and insert the new network after it
        auto last_line = content.rfind("\n  ", content.size() - 2);
        if (last_line == std::string::npos || last_line < top_net) {
            // No entries yet — insert after \nnetworks: line
            last_line = top_net;
        }
        auto after_line = content.find('\n', last_line + 1);
        if (after_line == std::string::npos) {
            after_line = content.size();
        }
        content.insert(after_line + 1,
            "  containercp-mail:\n"
            "    external: true\n");
        changed = true;
    }

    if (!changed) {
        return {true, ""};
    }

    // ── Step 3: Write to temp file and validate ──
    std::string tmp_path = compose_path + ".mail-tmp";
    {
        std::ofstream tmp(tmp_path);
        if (!tmp) {
            return {false, "Failed to write temporary compose file"};
        }
        tmp << content;
    }

    runtime::CommandExecutor exec;
    auto validate = exec.run({
        "docker", "compose", "-f", tmp_path, "config", "--quiet"
    });
    if (validate.exit_code != 0) {
        std::remove(tmp_path.c_str());
        return {false, "Compose validation failed: " + validate.err};
    }

    // ── Step 4: Atomic replace ──
    // docker-compose.yml is a generated artifact — no backup needed.
    // ContainerCP can always regenerate it from the internal data model.
    if (std::rename(tmp_path.c_str(), compose_path.c_str()) != 0) {
        std::remove(tmp_path.c_str());
        return {false, "Failed to replace compose file"};
    }

    // ── Step 5: docker compose up -d ──
    auto up = exec.run({
        "docker", "compose", "-f", compose_path, "up", "-d", "--no-recreate"
    });
    if (up.exit_code != 0) {
        return {false, "docker compose up failed after compose update: " + up.err};
    }

    return {true, "Compose file updated and validated"};
}

} // anonymous namespace

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
    // Ensure config/php/ directory exists (may be missing for sites created before Stage 3)
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

    // 5. Create MailDomain record
    // domain_id=0 is OK — linked by site_id
    mail_domains_.create(domain, mode, 0, site_id, "");

    // 6. Update site's docker-compose.yml so mail network persists across recreate
    auto compose_result = repair_compose_mail_network(site_dir, site_id, true);
    if (!compose_result.success) {
        credentials_.remove(site_id);
        std::remove(msmtprc_path.c_str());
        return compose_result;
    }

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
    credentials_.remove(site_id);

    // 2. Remove msmtprc
    if (!domain.empty()) {
        std::string site_dir = cfg_.sites_dir() + domain + "/";
        std::string msmtprc_path = site_dir + "config/php/msmtprc";
        std::remove(msmtprc_path.c_str());

        // 3. Remove containercp-mail from site's docker-compose.yml
        auto compose_result = repair_compose_mail_network(site_dir, site_id, false);
        if (!compose_result.success) {
            return compose_result;
        }
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
