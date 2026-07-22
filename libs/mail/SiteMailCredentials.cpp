#include "SiteMailCredentials.h"
#include "mail/MailPasswordHasher.h"
#include "runtime/CommandExecutor.h"
#include "config/Config.h"
#include "security/SecureRandom.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace containercp::mail {

static std::string generate_password(size_t length = 32) {
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    auto pw = security::SecureRandom::string(length, chars);
    return pw.value_or("");
}

SiteMailCredentials::Credential SiteMailCredentials::generate(
    uint64_t site_id, const std::string& domain) {
    Credential cred;
    cred.username = "site-" + std::to_string(site_id) + "@php.containercp.internal";
    cred.password = generate_password();
    cred.password_hash = MailPasswordHasher::hash(cred.password);
    cred.domain = domain;
    return cred;
}

bool SiteMailCredentials::remove(uint64_t site_id) {
    std::string username = "site-" + std::to_string(site_id) + "@php.containercp.internal";
    runtime::CommandExecutor exec;

    // Remove from Dovecot PHP credentials file
    std::string passwd_path = "/srv/containercp/mail/config/generated/passwd-php";
    std::string old_content;
    {
        std::ifstream in(passwd_path);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            old_content = ss.str();
        }
    }
    if (old_content.empty()) return true;

    std::string new_content;
    std::istringstream stream(old_content);
    std::string line;
    bool found = false;
    while (std::getline(stream, line)) {
        if (line.find(username) == std::string::npos) {
            new_content += line + "\n";
        } else {
            found = true;
        }
    }
    if (found) {
        std::ofstream out(passwd_path);
        if (out) out << new_content;
    }

    // Remove from sender_login map
    std::string login_path = "/srv/containercp/mail/config/generated/sender_login";
    {
        std::ifstream in(login_path);
        old_content.clear();
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            old_content = ss.str();
        }
    }
    if (!old_content.empty()) {
        new_content.clear();
        stream.clear();
        stream.str(old_content);
        while (std::getline(stream, line)) {
            if (line.find(username) == std::string::npos) {
                new_content += line + "\n";
            }
        }
        std::ofstream out(login_path);
        if (out) out << new_content;
    }

    // Regenerate postmap
    exec.run({"docker", "exec", "containercp-mail-postfix", "postmap",
              "/etc/postfix/sender_login"});
    exec.run({"docker", "exec", "containercp-mail-postfix", "postfix", "reload"});

    return true;
}

std::optional<SiteMailCredentials::Credential> SiteMailCredentials::find(
    uint64_t site_id) {
    std::string username = "site-" + std::to_string(site_id) + "@php.containercp.internal";
    std::string passwd_path = "/srv/containercp/mail/config/generated/passwd-php";
    std::ifstream in(passwd_path);
    if (!in) return std::nullopt;

    std::string line;
    while (std::getline(in, line)) {
        if (line.find(username) == 0) {
            Credential cred;
            cred.username = username;
            // Extract domain from sender_login if exists
            std::string login_path = "/srv/containercp/mail/config/generated/sender_login";
            std::ifstream lin(login_path);
            std::string l;
            while (std::getline(lin, l)) {
                if (l.find(username) == 0) {
                    auto pos = l.find_last_of('\t');
                    if (pos == std::string::npos) pos = l.find(' ');
                    if (pos != std::string::npos) {
                        std::string val = l.substr(pos + 1);
                        // val is either @domain or user@domain — extract domain part
                        auto at = val.find('@');
                        if (at != std::string::npos) cred.domain = val.substr(at + 1);
                        else cred.domain = val;
                    }
                    break;
                }
            }
            return cred;
        }
    }
    return std::nullopt;
}

core::OperationResult SiteMailCredentials::apply(const Credential& cred) {
    runtime::CommandExecutor exec;

    // 1. Add to Dovecot PHP credentials file (separate from mailbox passwd)
    //    This file is NOT overwritten by write_dovecot_config()
    std::string passwd_path = "/srv/containercp/mail/config/generated/passwd-php";

    // Ensure the path is a regular file (Docker bind mount may create a directory)
    struct stat pw_stat;
    if (stat(passwd_path.c_str(), &pw_stat) == 0 && !S_ISREG(pw_stat.st_mode)) {
        std::remove(passwd_path.c_str());
        std::ofstream init(passwd_path);
        if (init) init << "# ContainerCP PHP SMTP credentials\n";
    }

    // Read existing entries, remove old ones for this username, then write all.
    // Prevents duplicate entries on repeated enable (idempotent).
    {
        std::string old_pw;
        {
            std::ifstream in(passwd_path);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                old_pw = ss.str();
            }
        }
        std::string new_pw;
        {
            std::istringstream stream(old_pw);
            std::string line;
            while (std::getline(stream, line)) {
                if (line.find(cred.username) == std::string::npos) {
                    new_pw += line + "\n";
                }
            }
        }
        new_pw += cred.username + ":" + cred.password_hash
                  + ":65534:65534::/nonexistent::\n";
        std::ofstream passwd_out(passwd_path);
        if (!passwd_out) {
            return {false, "Failed to write PHP credentials file: " + passwd_path};
        }
        passwd_out << new_pw;
    }

    // 2. Update sender_login map (key=envelope sender, value=SASL username).
    //    First remove old entries for this site, then add current ones.
    //    Idempotent: repeated Enable does not duplicate entries.
    std::string login_path = "/srv/containercp/mail/config/generated/sender_login";
    {
        std::string old_content;
        {
            std::ifstream in(login_path);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                old_content = ss.str();
            }
        }
        // Remove lines where the VALUE contains this site's SASL username
        // (the format is: sender_address \t SASL_username)
        std::string new_content;
        std::istringstream stream(old_content);
        std::string line;
        while (std::getline(stream, line)) {
            // Keep lines that do NOT contain this site's credentials
            if (line.find(cred.username) == std::string::npos) {
                new_content += line + "\n";
            }
        }
        // Add current entry (exact-match: envelope sender → SASL username)
        new_content += "wordpress@" + cred.domain + "\t" + cred.username + "\n";
        // Also add @domain wildcard for future Postfix upgrades (currently inactive)
        new_content += "@" + cred.domain + "\t" + cred.username + "\n";

        std::ofstream login_out(login_path);
        if (!login_out) {
            return {false, "Failed to open sender_login file: " + login_path};
        }
        login_out << new_content;
    }

    // 3. Regenerate postmap and reload Postfix
    auto pm = exec.run({"docker", "exec", "containercp-mail-postfix", "postmap",
                        "/etc/postfix/sender_login"});
    if (pm.exit_code != 0) {
        return {false, "Failed to regenerate sender_login map: " + pm.err};
    }

    auto reload = exec.run({"docker", "exec", "containercp-mail-postfix", "postfix", "reload"});
    if (reload.exit_code != 0) {
        return {false, "Failed to reload Postfix: " + reload.err};
    }

    return {true, ""};
}

core::OperationResult SiteMailCredentials::revoke(const Credential& cred) {
    // Extract site_id from username: "site-{ID}@php.containercp.internal"
    uint64_t site_id = 0;
    if (cred.username.size() > 5 && cred.username.substr(0, 5) == "site-") {
        auto at_pos = cred.username.find('@');
        if (at_pos != std::string::npos) {
            std::string id_str = cred.username.substr(5, at_pos - 5);
            try { site_id = std::stoull(id_str); } catch (...) {}
        }
    }
    bool ok = remove(site_id);
    return ok ? core::OperationResult{true, ""}
              : core::OperationResult{false, "Failed to revoke credentials"};
}

} // namespace containercp::mail
