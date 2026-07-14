#include "SiteMailOrchestrator.h"
#include "filesystem/SiteLayout.h"
#include "runtime/CommandExecutor.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace containercp::mail {
namespace {

// Regenerate a site's docker-compose.yml with the correct
// containercp-mail network configuration. Handles corrupted files by
// rebuilding the YAML from scratch using the existing file structure.
static core::OperationResult regenerate_compose_mail_network(
    const std::string& site_dir, uint64_t site_id, bool add) {

    std::string compose_path = site_dir + "docker-compose.yml";

    // ── Step 1: Read current file into memory ──
    std::ifstream in(compose_path);
    if (!in) {
        return {true, ""};  // No compose file
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    in.close();

    // ── Step 2: Remove ALL lines containing containercp-mail ──
    // (from web service, volumes section, or any leaked location)
    std::vector<std::string> out;
    for (const auto& l : lines) {
        if (l.find("containercp-mail") == std::string::npos) {
            out.push_back(l);
        }
    }

    // ── Step 3: If adding, insert correct entries ──
    if (add) {
        // 3a. Insert containercp-mail into PHP service's network list.
        //     Strategy: find the PHP service section by container_name,
        //     then find the line just before "    environment:" within it.
        bool inserted_php_net = false;
        std::string php_marker = "site-" + std::to_string(site_id) + "-php";
        int php_section_start = -1;

        // Find which line starts the PHP service section
        for (int i = 0; i < (int)out.size(); i++) {
            if (out[i].find(php_marker) != std::string::npos) {
                php_section_start = i;
                break;
            }
        }

        if (php_section_start >= 0) {
            // Find "    environment:" or "    labels:" AFTER the PHP section start
            for (int i = php_section_start; i < (int)out.size(); i++) {
                if (out[i].find("    environment:") != std::string::npos ||
                    out[i].find("    labels:") != std::string::npos) {
                    // Insert containercp-mail at end of network list (before this line)
                    out.insert(out.begin() + i, "      - containercp-mail");
                    inserted_php_net = true;
                    break;
                }
            }
            if (!inserted_php_net) {
                // Fallback: insert after the last line in PHP section
                out.insert(out.begin() + php_section_start + 3, "      - containercp-mail");
            }
        }

        // 3b. Add top-level network declaration.
        //     Find the top-level "networks:" line and add an entry after it.
        bool inserted_top_net = false;
        for (int i = 0; i < (int)out.size(); i++) {
            const std::string& l = out[i];
            // Top-level networks: non-indented line exactly "networks:"
            if (l.size() >= 9 && l[0] != ' ' && l.substr(0, 9) == "networks:") {
                // Insert at the end of the file (after all entries under networks)
                // Find the last line of the networks section
                int insert_at = i + 1;
                // Skip indented entries under networks
                while (insert_at < (int)out.size() &&
                       out[insert_at].size() > 0 && out[insert_at][0] == ' ') {
                    insert_at++;
                }
                // Insert the new network entry
                out.insert(out.begin() + insert_at, "  containercp-mail:");
                out.insert(out.begin() + insert_at + 1, "    external: true");
                inserted_top_net = true;
                break;
            }
        }
        if (!inserted_top_net) {
            // No networks section — add it at the end
            out.push_back("");
            out.push_back("networks:");
            out.push_back("  containercp-mail:");
            out.push_back("    external: true");
        }
    }

    // ── Step 4: Write to temp file and validate ──
    std::string tmp_path = compose_path + ".mail-tmp";
    {
        std::ofstream tmp(tmp_path);
        if (!tmp) {
            return {false, "Failed to write temporary compose file"};
        }
        for (const auto& l : out) {
            tmp << l << "\n";
        }
    }

    // ── Step 5: Validate with docker compose config ──
    runtime::CommandExecutor exec;
    auto validate = exec.run({
        "docker", "compose", "-f", tmp_path, "config", "--quiet"
    });
    if (validate.exit_code != 0) {
        std::remove(tmp_path.c_str());
        return {false, "Compose validation failed after repair: " + validate.err};
    }

    // ── Step 6: Atomic replace ──
    if (std::rename(tmp_path.c_str(), compose_path.c_str()) != 0) {
        std::remove(tmp_path.c_str());
        return {false, "Failed to replace compose file"};
    }

    // ── Step 7: docker compose up -d ──
    auto up = exec.run({
        "docker", "compose", "-f", compose_path, "up", "-d", "--no-recreate"
    });
    if (up.exit_code != 0) {
        return {false, "docker compose up failed after compose update: " + up.err};
    }

    return {true, "Compose file regenerated and validated"};
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
    auto compose_result = regenerate_compose_mail_network(site_dir, site_id, true);
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
        auto compose_result = regenerate_compose_mail_network(site_dir, site_id, false);
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
