#ifndef CONTAINERCP_STORAGE_CANONICALIZER_H
#define CONTAINERCP_STORAGE_CANONICALIZER_H

#include "access/AccessGrant.h"
#include "access/AccessUser.h"
#include "auth/AuthUser.h"
#include "backup/Backup.h"
#include "database/Database.h"
#include "domain/Domain.h"
#include "mail/MailAlias.h"
#include "mail/Mailbox.h"
#include "mail/MailDomain.h"
#include "node/Node.h"
#include "php/PhpVersion.h"
#include "profile/Profile.h"
#include "proxy/ReverseProxy.h"
#include "site/Site.h"
#include "ssl/SslCertificate.h"
#include "user/User.h"

#include <algorithm>
#include <string>
#include <vector>
#include <openssl/sha.h>

namespace containercp::storage {

struct StorageCanonicalizer {
    static void append_field(std::string& out, const std::string& value) {
        uint64_t len = value.size();
        for (int i = 7; i >= 0; --i) out += static_cast<char>((len >> (i * 8)) & 0xff);
        out += value;
    }
    static void append_field(std::string& out, uint64_t value) {
        append_field(out, std::to_string(value));
    }
    static std::string sha256(const std::string& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH]; SHA256_CTX ctx;
        SHA256_Init(&ctx); SHA256_Update(&ctx, data.data(), data.size()); SHA256_Final(hash, &ctx);
        std::string out; out.reserve(64);
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            out += "0123456789abcdef"[(hash[i] >> 4) & 0xf];
            out += "0123456789abcdef"[hash[i] & 0xf];
        }
        return out;
    }

#define CANON_DEF(type, cls, ...) \
    static std::string canonical_##type(const std::vector<cls>& records) { \
        std::string out; auto s = records; \
        std::sort(s.begin(), s.end(), [](const cls& a, const cls& b) { return a.id < b.id; }); \
        for (const auto& r : s) { __VA_ARGS__ } return out; }

CANON_DEF(nodes, node::Node,
    append_field(out, r.id); append_field(out, r.name); append_field(out, r.type);)

CANON_DEF(php_versions, php::PhpVersion,
    append_field(out, r.id); append_field(out, r.version); append_field(out, r.image);
    append_field(out, r.enabled ? "true" : "false"); append_field(out, r.default_version ? "true" : "false");)

CANON_DEF(profiles, profile::Profile,
    append_field(out, r.id); append_field(out, r.profile_name); append_field(out, profile::profile_type_to_string(r.type));
    append_field(out, r.web_server); append_field(out, r.runtime); append_field(out, r.template_path);
    append_field(out, r.description); append_field(out, r.enabled ? "true" : "false"); append_field(out, r.default_profile ? "true" : "false");)

CANON_DEF(users, user::User,
    append_field(out, r.id); append_field(out, r.username); append_field(out, r.uid);
    append_field(out, r.home_directory); append_field(out, r.shell); append_field(out, r.enabled ? "true" : "false");)

CANON_DEF(sites, site::Site,
    append_field(out, r.id); append_field(out, r.domain); append_field(out, r.owner);
    append_field(out, r.node_id); append_field(out, r.web_server); append_field(out, r.php_mail_enabled ? "true" : "false");)

CANON_DEF(domains, domain::Domain,
    append_field(out, r.id); append_field(out, r.fqdn); append_field(out, r.owner_id); append_field(out, r.site_id);
    append_field(out, r.php_version); append_field(out, r.ssl_enabled ? "true" : "false"); append_field(out, r.enabled ? "true" : "false");
    append_field(out, r.type); append_field(out, r.target);)

CANON_DEF(databases, database::Database,
    append_field(out, r.id); append_field(out, r.db_name); append_field(out, r.db_user);
    append_field(out, r.db_password); append_field(out, r.engine); append_field(out, r.version);
    append_field(out, r.owner_id); append_field(out, r.site_id); append_field(out, r.enabled ? "true" : "false");)

CANON_DEF(backups, backup::Backup,
    append_field(out, r.id); append_field(out, r.site_id); append_field(out, r.owner_id);
    append_field(out, r.filename); append_field(out, r.type); append_field(out, r.size);
    append_field(out, r.created_at); append_field(out, r.status); append_field(out, r.file_path);
    append_field(out, r.compression);)

CANON_DEF(reverse_proxies, proxy::ReverseProxy,
    append_field(out, r.id); append_field(out, r.domain); append_field(out, r.site_id);
    append_field(out, r.provider); append_field(out, r.config_path); append_field(out, r.upstream);
    append_field(out, r.enabled ? "true" : "false"); append_field(out, r.status);)

CANON_DEF(access_users, access::AccessUser,
    append_field(out, r.id); append_field(out, r.username); append_field(out, r.auth_type);
    append_field(out, r.password_hash); append_field(out, r.enabled ? "true" : "false");)

CANON_DEF(access_grants, access::AccessGrant,
    append_field(out, r.id); append_field(out, r.access_user_id); append_field(out, r.site_id);
    append_field(out, access::permission_to_string(r.permission));)

CANON_DEF(auth_users, auth::AuthUser,
    append_field(out, r.id); append_field(out, r.username); append_field(out, r.password_hash);
    append_field(out, r.must_change_password ? "true" : "false"); append_field(out, r.enabled ? "true" : "false");
    append_field(out, r.role);)

CANON_DEF(ssl_certificates, ssl::SslCertificate,
    append_field(out, r.id); append_field(out, r.domain_id); append_field(out, r.domain);
    append_field(out, r.provider); append_field(out, r.certificate_path); append_field(out, r.key_path);
    append_field(out, r.chain_path); append_field(out, r.issued_at); append_field(out, r.expires_at); append_field(out, r.renew_after);
    append_field(out, r.status); append_field(out, r.auto_renew ? "true" : "false");
    append_field(out, r.https_enabled ? "true" : "false"); append_field(out, r.redirect_enabled ? "true" : "false");
    append_field(out, r.domains); append_field(out, r.challenge_type); append_field(out, r.last_error);
    append_field(out, r.last_validation);
    append_field(out, static_cast<uint64_t>(r.renew_attempts)); append_field(out, static_cast<uint64_t>(r.version));)

CANON_DEF(mail_domains, mail::MailDomain,
    append_field(out, r.id); append_field(out, r.domain_id); append_field(out, r.site_id);
    append_field(out, r.domain_name); append_field(out, mail::mail_domain_mode_to_string(r.mode));
    append_field(out, r.relay_host); append_field(out, r.dkim_selector); append_field(out, r.dkim_private_key_path);
    append_field(out, r.dkim_public_key_dns); append_field(out, r.max_mailboxes); append_field(out, r.max_aliases);
    append_field(out, r.catch_all); append_field(out, r.enabled ? "true" : "false");
    append_field(out, r.created_at); append_field(out, r.updated_at);)

CANON_DEF(mail_mailboxes, mail::Mailbox,
    append_field(out, r.id); append_field(out, r.domain_id); append_field(out, r.local_part);
    append_field(out, r.password_hash); append_field(out, r.quota_bytes); append_field(out, r.quota_messages);
    append_field(out, r.enabled ? "true" : "false"); append_field(out, r.display_name); append_field(out, r.forward_to);
    append_field(out, r.spam_enabled ? "true" : "false"); append_field(out, r.last_login);
    append_field(out, r.created_at); append_field(out, r.updated_at);)

CANON_DEF(mail_aliases, mail::MailAlias,
    append_field(out, r.id); append_field(out, r.domain_id); append_field(out, r.source_local_part);
    append_field(out, r.destination); append_field(out, r.enabled ? "true" : "false");
    append_field(out, r.created_at); append_field(out, r.updated_at);)

    static std::string canonical_mail_config(bool ms_present, const std::string& ms,
                                              bool sh_present, const std::string& sh) {
        std::string out;
        append_field(out, ms_present ? "present" : "absent"); append_field(out, ms);
        append_field(out, sh_present ? "present" : "absent"); append_field(out, sh);
        return out;
    }
};

} // namespace containercp::storage
#endif
