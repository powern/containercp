#include "Verification.h"
#include "LegacyDatasetReader.h"
#include "Storage.h"
#include "StorageCanonicalizer.h"
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
#include "profile/ProfileType.h"
#include "proxy/ReverseProxy.h"
#include "site/Site.h"
#include "ssl/SslCertificate.h"
#include "user/User.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <openssl/sha.h>

namespace containercp::storage {
namespace fs = std::filesystem;

// ============================================================
// Verification
// ============================================================

Verification::Verification(const std::string& legacy_directory,
                           const std::string& sqlite_path,
                           const ImportAllResult& import_result,
                           const std::string& storage_directory)
    : legacy_dir_(legacy_directory), sqlite_path_(sqlite_path)
    , storage_dir_(storage_directory.empty() ? fs::path(sqlite_path).parent_path().string() : storage_directory)
    , import_result_(import_result), pool_()
{
    if (!legacy_dir_.empty() && legacy_dir_.back() != '/') legacy_dir_ += '/';
    if (!storage_dir_.empty() && storage_dir_.back() != '/') storage_dir_ += '/';
}

std::string Verification::sha256(const std::string& data) {
    return StorageCanonicalizer::sha256(data);
}

void Verification::append_field(std::string& out, const std::string& value) {
    StorageCanonicalizer::append_field(out, value);
}
void Verification::append_field(std::string& out, uint64_t value) {
    StorageCanonicalizer::append_field(out, std::to_string(value));
}

const ImportResult* Verification::find_import_result(const std::string& type) const {
    for (const auto& r : import_result_.resources)
        if (r.resource_type == type) return &r;
    return nullptr;
}

// ============================================================
// Checked SQLite query helpers
// ============================================================

template<typename T>
struct CheckedLoad { bool success = false; std::vector<T> records; std::string error; };

template<typename T>
static CheckedLoad<T> checked_query(ConnectionPool& pool, const std::string& sql,
    std::function<bool(SQLiteDB&, T&)> read_row) {
    CheckedLoad<T> r;
    ReadLease rl(pool);
    if (!rl.is_valid()) { r.error = "no read connection"; return r; }
    if (!rl->prepare(sql)) { r.error = "prepare: " + rl->error_message(); return r; }
    while (true) {
        if (!rl->step()) {
            if (rl->error_code() != 0) { r.error = "step: " + rl->error_message(); return r; }
            break;
        }
        T rec;
        if (!read_row(rl.db(), rec)) { r.error = "row read failed"; return r; }
        r.records.push_back(std::move(rec));
    }
    r.success = true;
    return r;
}

// ============================================================
// Field-by-field comparison helpers
// ============================================================

struct FieldCheck {
    std::string name;
    std::string expected;
    std::string actual;
    bool sensitive = false;
};

#define CHECK_STR(fname, expr_l, expr_s) do { \
    std::string v_l = (expr_l); std::string v_s = (expr_s); \
    if (v_l != v_s) { FieldCheck fc; fc.name = (fname); \
    fc.expected = sensitive ? "[REDACTED]" : v_l; \
    fc.actual = sensitive ? "[REDACTED]" : v_s; \
    fc.sensitive = sensitive; checks.push_back(fc); } } while(0)

#define CHECK_U64(fname, expr_l, expr_s) do { \
    auto v_l = (expr_l); auto v_s = (expr_s); \
    std::string sv_l = std::to_string(v_l); std::string sv_s = std::to_string(v_s); \
    if (sv_l != sv_s) { FieldCheck fc; fc.name = (fname); \
    fc.expected = sensitive ? "[REDACTED]" : sv_l; \
    fc.actual = sensitive ? "[REDACTED]" : sv_s; \
    fc.sensitive = sensitive; checks.push_back(fc); } } while(0)

#define CHECK_BOOL(fname, expr_l, expr_s) CHECK_STR(fname, ((expr_l) ? "true" : "false"), ((expr_s) ? "true" : "false"))

static std::vector<FieldCheck> compare_node(const node::Node& l, const node::Node& s, bool) {
    std::vector<FieldCheck> checks; bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_STR("name", l.name, s.name); CHECK_STR("type", l.type, s.type);
    return checks;
}
static std::vector<FieldCheck> compare_php(const php::PhpVersion& l, const php::PhpVersion& s, bool) {
    std::vector<FieldCheck> checks; bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_STR("version", l.version, s.version); CHECK_STR("image", l.image, s.image);
    CHECK_BOOL("enabled", l.enabled, s.enabled); CHECK_BOOL("default_version", l.default_version, s.default_version);
    return checks;
}
static std::vector<FieldCheck> compare_profile(const profile::Profile& l, const profile::Profile& s, bool) {
    std::vector<FieldCheck> checks; bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_STR("profile_name", l.profile_name, s.profile_name);
    CHECK_STR("type", profile::profile_type_to_string(l.type), profile::profile_type_to_string(s.type));
    CHECK_STR("web_server", l.web_server, s.web_server); CHECK_STR("runtime", l.runtime, s.runtime);
    CHECK_STR("template_path", l.template_path, s.template_path); CHECK_STR("description", l.description, s.description);
    CHECK_BOOL("enabled", l.enabled, s.enabled); CHECK_BOOL("default_profile", l.default_profile, s.default_profile);
    return checks;
}
static std::vector<FieldCheck> compare_user(const user::User& l, const user::User& s, bool) {
    std::vector<FieldCheck> checks; bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_STR("username", l.username, s.username); CHECK_U64("uid", l.uid, s.uid);
    CHECK_STR("home_directory", l.home_directory, s.home_directory); CHECK_STR("shell", l.shell, s.shell);
    CHECK_BOOL("enabled", l.enabled, s.enabled);
    return checks;
}
static std::vector<FieldCheck> compare_site(const site::Site& l, const site::Site& s, bool) {
    std::vector<FieldCheck> checks; bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_STR("domain", l.domain, s.domain); CHECK_STR("owner", l.owner, s.owner);
    CHECK_U64("node_id", l.node_id, s.node_id); CHECK_STR("web_server", l.web_server, s.web_server);
    CHECK_BOOL("php_mail_enabled", l.php_mail_enabled, s.php_mail_enabled);
    return checks;
}
static std::vector<FieldCheck> compare_domain(const domain::Domain& l, const domain::Domain& s, bool) {
    std::vector<FieldCheck> checks; bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_STR("fqdn", l.fqdn, s.fqdn);
    CHECK_U64("owner_id", l.owner_id, s.owner_id); CHECK_U64("site_id", l.site_id, s.site_id);
    CHECK_STR("php_version", l.php_version, s.php_version); CHECK_BOOL("ssl_enabled", l.ssl_enabled, s.ssl_enabled);
    CHECK_BOOL("enabled", l.enabled, s.enabled); CHECK_STR("type", l.type, s.type); CHECK_STR("target", l.target, s.target);
    return checks;
}
static std::vector<FieldCheck> compare_database(const database::Database& l, const database::Database& s, bool) {
    std::vector<FieldCheck> checks;
    bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_STR("db_name", l.db_name, s.db_name); CHECK_STR("db_user", l.db_user, s.db_user);
    sensitive = true; CHECK_STR("db_password", l.db_password, s.db_password); sensitive = false;
    CHECK_STR("engine", l.engine, s.engine); CHECK_STR("version", l.version, s.version);
    CHECK_U64("owner_id", l.owner_id, s.owner_id); CHECK_U64("site_id", l.site_id, s.site_id);
    CHECK_BOOL("enabled", l.enabled, s.enabled);
    return checks;
}
static std::vector<FieldCheck> compare_backup(const backup::Backup& l, const backup::Backup& s, bool) {
    std::vector<FieldCheck> checks; bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_U64("site_id", l.site_id, s.site_id); CHECK_U64("owner_id", l.owner_id, s.owner_id);
    CHECK_STR("filename", l.filename, s.filename); CHECK_STR("type", l.type, s.type); CHECK_U64("size", l.size, s.size);
    CHECK_STR("created_at", l.created_at, s.created_at); CHECK_STR("status", l.status, s.status);
    CHECK_STR("file_path", l.file_path, s.file_path); CHECK_STR("compression", l.compression, s.compression);
    return checks;
}
static std::vector<FieldCheck> compare_proxy(const proxy::ReverseProxy& l, const proxy::ReverseProxy& s, bool) {
    std::vector<FieldCheck> checks; bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_STR("domain", l.domain, s.domain); CHECK_U64("site_id", l.site_id, s.site_id);
    CHECK_STR("provider", l.provider, s.provider); CHECK_STR("config_path", l.config_path, s.config_path);
    CHECK_STR("upstream", l.upstream, s.upstream); CHECK_BOOL("enabled", l.enabled, s.enabled);
    CHECK_STR("status", l.status, s.status);
    return checks;
}
static std::vector<FieldCheck> compare_access_user(const access::AccessUser& l, const access::AccessUser& s, bool) {
    std::vector<FieldCheck> checks;
    bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_STR("username", l.username, s.username); CHECK_STR("auth_type", l.auth_type, s.auth_type);
    sensitive = true; CHECK_STR("password_hash", l.password_hash, s.password_hash); sensitive = false;
    CHECK_BOOL("enabled", l.enabled, s.enabled);
    return checks;
}
static std::vector<FieldCheck> compare_access_grant(const access::AccessGrant& l, const access::AccessGrant& s, bool) {
    std::vector<FieldCheck> checks; bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_U64("access_user_id", l.access_user_id, s.access_user_id);
    CHECK_U64("site_id", l.site_id, s.site_id); CHECK_STR("permission", access::permission_to_string(l.permission), access::permission_to_string(s.permission));
    return checks;
}
static std::vector<FieldCheck> compare_auth_user(const auth::AuthUser& l, const auth::AuthUser& s, bool) {
    std::vector<FieldCheck> checks;
    bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_STR("username", l.username, s.username);
    sensitive = true; CHECK_STR("password_hash", l.password_hash, s.password_hash); sensitive = false;
    CHECK_BOOL("must_change_password", l.must_change_password, s.must_change_password);
    CHECK_BOOL("enabled", l.enabled, s.enabled); CHECK_STR("role", l.role, s.role);
    return checks;
}
static std::vector<FieldCheck> compare_ssl(const ssl::SslCertificate& l, const ssl::SslCertificate& s, bool) {
    std::vector<FieldCheck> checks;
    bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_U64("domain_id", l.domain_id, s.domain_id); CHECK_STR("domain", l.domain, s.domain);
    CHECK_STR("provider", l.provider, s.provider); CHECK_STR("certificate_path", l.certificate_path, s.certificate_path);
    sensitive = true; CHECK_STR("key_path", l.key_path, s.key_path); sensitive = false;
    CHECK_STR("chain_path", l.chain_path, s.chain_path); CHECK_STR("issued_at", l.issued_at, s.issued_at);
    CHECK_STR("expires_at", l.expires_at, s.expires_at); CHECK_STR("renew_after", l.renew_after, s.renew_after);
    CHECK_STR("status", l.status, s.status); CHECK_BOOL("auto_renew", l.auto_renew, s.auto_renew);
    CHECK_BOOL("https_enabled", l.https_enabled, s.https_enabled); CHECK_BOOL("redirect_enabled", l.redirect_enabled, s.redirect_enabled);
    CHECK_STR("domains", l.domains, s.domains); CHECK_STR("challenge_type", l.challenge_type, s.challenge_type);
    CHECK_STR("last_error", l.last_error, s.last_error); CHECK_STR("last_validation", l.last_validation, s.last_validation);
    CHECK_U64("renew_attempts", static_cast<uint64_t>(l.renew_attempts), static_cast<uint64_t>(s.renew_attempts));
    CHECK_U64("version", static_cast<uint64_t>(l.version), static_cast<uint64_t>(s.version));
    return checks;
}
static std::vector<FieldCheck> compare_mail_domain(const mail::MailDomain& l, const mail::MailDomain& s, bool) {
    std::vector<FieldCheck> checks;
    bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_U64("domain_id", l.domain_id, s.domain_id); CHECK_U64("site_id", l.site_id, s.site_id);
    CHECK_STR("domain_name", l.domain_name, s.domain_name); CHECK_STR("mode", mail::mail_domain_mode_to_string(l.mode), mail::mail_domain_mode_to_string(s.mode));
    CHECK_STR("relay_host", l.relay_host, s.relay_host); CHECK_STR("dkim_selector", l.dkim_selector, s.dkim_selector);
    sensitive = true; CHECK_STR("dkim_private_key_path", l.dkim_private_key_path, s.dkim_private_key_path); sensitive = false;
    CHECK_STR("dkim_public_key_dns", l.dkim_public_key_dns, s.dkim_public_key_dns);
    CHECK_U64("max_mailboxes", l.max_mailboxes, s.max_mailboxes); CHECK_U64("max_aliases", l.max_aliases, s.max_aliases);
    CHECK_STR("catch_all", l.catch_all, s.catch_all); CHECK_BOOL("enabled", l.enabled, s.enabled);
    CHECK_STR("created_at", l.created_at, s.created_at); CHECK_STR("updated_at", l.updated_at, s.updated_at);
    return checks;
}
static std::vector<FieldCheck> compare_mailbox(const mail::Mailbox& l, const mail::Mailbox& s, bool) {
    std::vector<FieldCheck> checks;
    bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_U64("domain_id", l.domain_id, s.domain_id); CHECK_STR("local_part", l.local_part, s.local_part);
    sensitive = true; CHECK_STR("password_hash", l.password_hash, s.password_hash); sensitive = false;
    CHECK_U64("quota_bytes", l.quota_bytes, s.quota_bytes); CHECK_U64("quota_messages", l.quota_messages, s.quota_messages);
    CHECK_BOOL("enabled", l.enabled, s.enabled); CHECK_STR("display_name", l.display_name, s.display_name);
    CHECK_STR("forward_to", l.forward_to, s.forward_to); CHECK_BOOL("spam_enabled", l.spam_enabled, s.spam_enabled);
    CHECK_STR("last_login", l.last_login, s.last_login); CHECK_STR("created_at", l.created_at, s.created_at);
    CHECK_STR("updated_at", l.updated_at, s.updated_at);
    return checks;
}
static std::vector<FieldCheck> compare_mail_alias(const mail::MailAlias& l, const mail::MailAlias& s, bool) {
    std::vector<FieldCheck> checks; bool sensitive = false;
    CHECK_U64("id", l.id, s.id); CHECK_U64("domain_id", l.domain_id, s.domain_id);
    CHECK_STR("source_local_part", l.source_local_part, s.source_local_part); CHECK_STR("destination", l.destination, s.destination);
    CHECK_BOOL("enabled", l.enabled, s.enabled); CHECK_STR("created_at", l.created_at, s.created_at);
    CHECK_STR("updated_at", l.updated_at, s.updated_at);
    return checks;
}

// ---- Generic comparison helper using field adapters ----
static constexpr int kMaxMismatches = 100;

template<typename T>
static ResourceVerificationResult compare_resource(
    const std::string& type,
    std::vector<T>&& legacy,
    std::vector<T>&& sqlite,
    std::function<std::string(const std::vector<T>&)> canon_fn,
    std::function<std::vector<FieldCheck>(const T&, const T&, bool)> field_fn,
    std::function<bool(const T&)> transient_fn = nullptr)
{
    ResourceVerificationResult r; r.resource_type = type;

    // Sort and canonicalize
    auto legacy_sorted = legacy, sqlite_sorted = sqlite;
    std::sort(legacy_sorted.begin(), legacy_sorted.end(), [](const T& a, const T& b) { return a.id < b.id; });
    std::sort(sqlite_sorted.begin(), sqlite_sorted.end(), [](const T& a, const T& b) { return a.id < b.id; });

    r.legacy_record_count = legacy_sorted.size();
    r.sqlite_record_count = sqlite_sorted.size();
    r.legacy_checksum = Verification::sha256(canon_fn(legacy_sorted));
    r.sqlite_checksum = Verification::sha256(canon_fn(sqlite_sorted));

    if (r.legacy_record_count != r.sqlite_record_count) {
        r.success = false; r.status = VerificationStatus::Failed; return r;
    }

    // Transient validation
    if (transient_fn) {
        for (const auto& rec : sqlite_sorted) {
            if (!transient_fn(rec)) {
                FieldMismatch fm; fm.resource_type = type; fm.record_id = rec.id;
                fm.field = "(transient)"; fm.expected = "[VALID]"; fm.actual = "[INVALID]";
                r.mismatches.push_back(fm);
            }
        }
    }

    // Checksum and field comparison
    if (r.legacy_checksum != r.sqlite_checksum) {
        size_t li = 0, si = 0;
        bool any_sensitive = false;
        while (li < legacy_sorted.size() && si < sqlite_sorted.size() &&
               (int)r.mismatches.size() < kMaxMismatches) {
            if (legacy_sorted[li].id < sqlite_sorted[si].id) {
                FieldMismatch fm; fm.resource_type = type; fm.record_id = legacy_sorted[li].id;
                fm.field = "(missing in sqlite)"; fm.expected = "[RECORD]"; r.mismatches.push_back(fm); ++li;
            } else if (sqlite_sorted[si].id < legacy_sorted[li].id) {
                FieldMismatch fm; fm.resource_type = type; fm.record_id = sqlite_sorted[si].id;
                fm.field = "(unexpected in sqlite)"; fm.actual = "[RECORD]"; r.mismatches.push_back(fm); ++si;
            } else {
                auto fields = field_fn(legacy_sorted[li], sqlite_sorted[si], any_sensitive);
                for (const auto& fc : fields) {
                    FieldMismatch fm; fm.resource_type = type; fm.record_id = legacy_sorted[li].id;
                    fm.field = fc.name; fm.expected = fc.expected; fm.actual = fc.actual;
                    r.mismatches.push_back(fm);
                    if (fc.sensitive) any_sensitive = true;
                }
                ++li; ++si;
            }
        }
        while (li < legacy_sorted.size() && (int)r.mismatches.size() < kMaxMismatches) {
            FieldMismatch fm; fm.resource_type = type; fm.record_id = legacy_sorted[li].id;
            fm.field = "(missing in sqlite)"; fm.expected = "[RECORD]"; r.mismatches.push_back(fm); ++li; }
        while (si < sqlite_sorted.size() && (int)r.mismatches.size() < kMaxMismatches) {
            FieldMismatch fm; fm.resource_type = type; fm.record_id = sqlite_sorted[si].id;
            fm.field = "(unexpected in sqlite)"; fm.actual = "[RECORD]"; r.mismatches.push_back(fm); ++si; }

        r.success = false; r.status = VerificationStatus::Failed;
        return r;
    }

    // Checksum match plus any transient failures
    if (!r.mismatches.empty()) {
        r.success = false; r.status = VerificationStatus::Failed; return r;
    }
    r.success = true; r.status = VerificationStatus::Passed;
    return r;
}

// ---- Canonical serialization ----
#define CANON_BEGIN(cls) std::string out; auto sorted = records; \
    std::sort(sorted.begin(), sorted.end(), [](const cls& a, const cls& b) { return a.id < b.id; }); \
    for (const auto& r : sorted) {
#define CANON_FIELD_STR(field) Verification::append_field(out, r.field)
#define CANON_FIELD_U64(field) Verification::append_field(out, r.field)
#define CANON_FIELD_BOOL(field) Verification::append_field(out, r.field ? "true" : "false")
#define CANON_END() } return out

std::string Verification::canonical_nodes(const std::vector<node::Node>& records)
{ return StorageCanonicalizer::canonical_nodes(records); }
std::string Verification::canonical_php_versions(const std::vector<php::PhpVersion>& records)
{ return StorageCanonicalizer::canonical_php_versions(records); }
std::string Verification::canonical_profiles(const std::vector<profile::Profile>& records)
{ return StorageCanonicalizer::canonical_profiles(records); }
std::string Verification::canonical_users(const std::vector<user::User>& records)
{ return StorageCanonicalizer::canonical_users(records); }
std::string Verification::canonical_sites(const std::vector<site::Site>& records)
{ return StorageCanonicalizer::canonical_sites(records); }
std::string Verification::canonical_domains(const std::vector<domain::Domain>& records)
{ return StorageCanonicalizer::canonical_domains(records); }
std::string Verification::canonical_databases(const std::vector<database::Database>& records)
{ return StorageCanonicalizer::canonical_databases(records); }
std::string Verification::canonical_backups(const std::vector<backup::Backup>& records)
{ return StorageCanonicalizer::canonical_backups(records); }
std::string Verification::canonical_reverse_proxies(const std::vector<proxy::ReverseProxy>& records)
{ return StorageCanonicalizer::canonical_reverse_proxies(records); }
std::string Verification::canonical_access_users(const std::vector<access::AccessUser>& records)
{ return StorageCanonicalizer::canonical_access_users(records); }
std::string Verification::canonical_access_grants(const std::vector<access::AccessGrant>& records)
{ return StorageCanonicalizer::canonical_access_grants(records); }
std::string Verification::canonical_auth_users(const std::vector<auth::AuthUser>& records)
{ return StorageCanonicalizer::canonical_auth_users(records); }
std::string Verification::canonical_ssl_certificates(const std::vector<ssl::SslCertificate>& records)
{ return StorageCanonicalizer::canonical_ssl_certificates(records); }
std::string Verification::canonical_mail_domains(const std::vector<mail::MailDomain>& records)
{ return StorageCanonicalizer::canonical_mail_domains(records); }
std::string Verification::canonical_mail_mailboxes(const std::vector<mail::Mailbox>& records)
{ return StorageCanonicalizer::canonical_mail_mailboxes(records); }
std::string Verification::canonical_mail_aliases(const std::vector<mail::MailAlias>& records)
{ return StorageCanonicalizer::canonical_mail_aliases(records); }
std::string Verification::canonical_mail_config(const std::string& ms, const std::string& sh) {
    return StorageCanonicalizer::canonical_mail_config(!ms.empty(), ms, !sh.empty(), sh);
}

// ---- SQLite row readers ----
static bool rd_node(SQLiteDB& db, node::Node& n) {
    n.id = static_cast<uint64_t>(db.column_int(0)); n.name = db.column_text(1); n.type = db.column_text(2); return true; }
static bool rd_php(SQLiteDB& db, php::PhpVersion& pv) {
    pv.id = static_cast<uint64_t>(db.column_int(0)); pv.version = db.column_text(1); pv.image = db.column_text(2);
    pv.enabled = (db.column_int(3) != 0); pv.default_version = (db.column_int(4) != 0); pv.name = pv.version; return true; }
static bool rd_profile(SQLiteDB& db, profile::Profile& p) {
    p.id = static_cast<uint64_t>(db.column_int(0)); p.profile_name = db.column_text(1);
    p.type = profile::profile_type_from_string(db.column_text(2)); p.web_server = db.column_text(3);
    p.runtime = db.column_text(4); p.template_path = db.column_text(5); p.description = db.column_text(6);
    p.enabled = (db.column_int(7) != 0); p.default_profile = (db.column_int(8) != 0); p.name = p.profile_name; return true; }
static bool rd_user(SQLiteDB& db, user::User& u) {
    u.id = static_cast<uint64_t>(db.column_int(0)); u.username = db.column_text(1);
    u.uid = static_cast<uint64_t>(db.column_int(2)); u.home_directory = db.column_text(3);
    u.shell = db.column_text(4); u.enabled = (db.column_int(5) != 0); u.name = u.username; return true; }
static bool rd_site(SQLiteDB& db, site::Site& s) {
    s.id = static_cast<uint64_t>(db.column_int(0)); s.domain = db.column_text(1); s.owner = db.column_text(2);
    s.node_id = static_cast<uint64_t>(db.column_int(3)); s.web_server = db.column_text(4);
    s.php_mail_enabled = (db.column_int(5) != 0); s.php_mail_enabled_present = true; s.name = s.domain; return true; }
static bool rd_domain(SQLiteDB& db, domain::Domain& d) {
    d.id = static_cast<uint64_t>(db.column_int(0)); d.fqdn = db.column_text(1); d.owner_id = static_cast<uint64_t>(db.column_int(2));
    d.site_id = static_cast<uint64_t>(db.column_int(3)); d.php_version = db.column_text(4);
    d.ssl_enabled = (db.column_int(5) != 0); d.enabled = (db.column_int(6) != 0);
    d.type = db.column_text(7); d.target = db.column_text(8); d.name = d.fqdn; return true; }
static bool rd_db(SQLiteDB& db, database::Database& d) {
    d.id = static_cast<uint64_t>(db.column_int(0)); d.db_name = db.column_text(1); d.db_user = db.column_text(2);
    d.db_password = db.column_text(3); d.engine = db.column_text(4); d.version = db.column_text(5);
    d.owner_id = static_cast<uint64_t>(db.column_int(6)); d.site_id = static_cast<uint64_t>(db.column_int(7));
    d.enabled = (db.column_int(8) != 0); d.name = d.db_name; return true; }
static bool rd_backup(SQLiteDB& db, backup::Backup& b) {
    b.id = static_cast<uint64_t>(db.column_int(0)); b.site_id = static_cast<uint64_t>(db.column_int(1));
    b.owner_id = static_cast<uint64_t>(db.column_int(2)); b.filename = db.column_text(3); b.type = db.column_text(4);
    b.size = static_cast<uint64_t>(db.column_int(5)); b.created_at = db.column_text(6); b.status = db.column_text(7);
    b.file_path = db.column_text(8); b.compression = db.column_text(9); b.name = b.filename; return true; }
static bool rd_proxy(SQLiteDB& db, proxy::ReverseProxy& p) {
    p.id = static_cast<uint64_t>(db.column_int(0)); p.domain = db.column_text(1);
    p.site_id = static_cast<uint64_t>(db.column_int(2)); p.provider = db.column_text(3);
    p.config_path = db.column_text(4); p.upstream = db.column_text(5); p.enabled = (db.column_int(6) != 0);
    p.status = db.column_text(7); p.name = p.domain; return true; }
static bool rd_au(SQLiteDB& db, access::AccessUser& u) {
    u.id = static_cast<uint64_t>(db.column_int(0)); u.username = db.column_text(1); u.auth_type = db.column_text(2);
    u.password_hash = db.column_text(3); u.enabled = (db.column_int(4) != 0); u.name = u.username; return true; }
static bool rd_ag(SQLiteDB& db, access::AccessGrant& g) {
    g.id = static_cast<uint64_t>(db.column_int(0)); g.access_user_id = static_cast<uint64_t>(db.column_int(1));
    g.site_id = static_cast<uint64_t>(db.column_int(2));
    g.permission = access::permission_from_string(db.column_text(3));
    g.name = std::to_string(g.access_user_id) + "-" + std::to_string(g.site_id); return true; }
static bool rd_authu(SQLiteDB& db, auth::AuthUser& u) {
    u.id = static_cast<uint64_t>(db.column_int(0)); u.username = db.column_text(1); u.password_hash = db.column_text(2);
    u.must_change_password = (db.column_int(3) != 0); u.enabled = (db.column_int(4) != 0);
    u.role = db.column_text(5); u.name = u.username; return true; }
static bool rd_ssl(SQLiteDB& db, ssl::SslCertificate& c) {
    c.id = static_cast<uint64_t>(db.column_int(0)); c.domain_id = static_cast<uint64_t>(db.column_int(1));
    c.domain = db.column_text(2); c.provider = db.column_text(3); c.certificate_path = db.column_text(4);
    c.key_path = db.column_text(5); c.chain_path = db.column_text(6); c.issued_at = db.column_text(7);
    c.expires_at = db.column_text(8); c.renew_after = db.column_text(9); c.status = db.column_text(10);
    c.auto_renew = (db.column_int(11) != 0); c.https_enabled = (db.column_int(12) != 0);
    c.redirect_enabled = (db.column_int(13) != 0); c.domains = db.column_text(14);
    c.challenge_type = db.column_text(15); c.last_error = db.column_text(16);
    c.last_validation = db.column_text(17); c.renew_attempts = static_cast<int>(db.column_int(18));
    c.version = static_cast<int>(db.column_int(19)); c.name = c.domain; return true; }
static bool rd_md(SQLiteDB& db, mail::MailDomain& m) {
    m.id = static_cast<uint64_t>(db.column_int(0)); m.domain_id = static_cast<uint64_t>(db.column_int(1));
    m.site_id = static_cast<uint64_t>(db.column_int(2)); m.domain_name = db.column_text(3);
    m.mode = mail::mail_domain_mode_from_string(db.column_text(4)); m.relay_host = db.column_text(5);
    m.dkim_selector = db.column_text(6); m.dkim_private_key_path = db.column_text(7);
    m.dkim_public_key_dns = db.column_text(8); m.max_mailboxes = static_cast<uint64_t>(db.column_int(9));
    m.max_aliases = static_cast<uint64_t>(db.column_int(10)); m.catch_all = db.column_text(11);
    m.enabled = (db.column_int(12) != 0); m.created_at = db.column_text(13);
    m.updated_at = db.column_text(14); m.name = m.domain_name; return true; }
static bool rd_mb(SQLiteDB& db, mail::Mailbox& mb) {
    mb.id = static_cast<uint64_t>(db.column_int(0)); mb.domain_id = static_cast<uint64_t>(db.column_int(1));
    mb.local_part = db.column_text(2); mb.password_hash = db.column_text(3);
    mb.quota_bytes = static_cast<uint64_t>(db.column_int(4)); mb.quota_messages = static_cast<uint64_t>(db.column_int(5));
    mb.enabled = (db.column_int(6) != 0); mb.display_name = db.column_text(7); mb.forward_to = db.column_text(8);
    mb.spam_enabled = (db.column_int(9) != 0); mb.last_login = db.column_text(10);
    mb.created_at = db.column_text(11); mb.updated_at = db.column_text(12); mb.name = mb.local_part; return true; }
static bool rd_ma(SQLiteDB& db, mail::MailAlias& a) {
    a.id = static_cast<uint64_t>(db.column_int(0)); a.domain_id = static_cast<uint64_t>(db.column_int(1));
    a.source_local_part = db.column_text(2); a.destination = db.column_text(3);
    a.enabled = (db.column_int(4) != 0); a.created_at = db.column_text(5);
    a.updated_at = db.column_text(6); a.name = a.source_local_part; return true; }

// ---- Checked SQLite load helpers ----
#define LOAD_HELPER(type, cls, sql, reader) \
    static ResourceVerificationResult load_##type(ConnectionPool& p, std::vector<cls>& out) { \
        auto cr = checked_query<cls>(p, sql, reader); \
        if (!cr.success) { ResourceVerificationResult r; r.success = false; r.error = "sqlite_load_failed"; r.diagnostics = cr.error; return r; } \
        out = std::move(cr.records); ResourceVerificationResult r; r.success = true; return r; }
#define LOAD_HELPER_CALL(type, p, out) load_##type(p, out)

LOAD_HELPER(nodes, node::Node, "SELECT id, name, type FROM nodes ORDER BY id", rd_node)
LOAD_HELPER(php_versions, php::PhpVersion, "SELECT id, version, image, enabled, default_version FROM php_versions ORDER BY id", rd_php)
LOAD_HELPER(profiles, profile::Profile, "SELECT id, profile_name, type, web_server, runtime, template_path, description, enabled, default_profile FROM profiles ORDER BY id", rd_profile)
LOAD_HELPER(users, user::User, "SELECT id, username, uid, home_directory, shell, enabled FROM users ORDER BY id", rd_user)
LOAD_HELPER(sites, site::Site, "SELECT id, domain, owner, node_id, web_server, php_mail_enabled FROM sites ORDER BY id", rd_site)
LOAD_HELPER(domains, domain::Domain, "SELECT id, fqdn, owner_id, site_id, php_version, ssl_enabled, enabled, type, target FROM domains ORDER BY id", rd_domain)
LOAD_HELPER(databases, database::Database, "SELECT id, db_name, db_user, db_password, engine, version, owner_id, site_id, enabled FROM databases ORDER BY id", rd_db)
LOAD_HELPER(backups, backup::Backup, "SELECT id, site_id, owner_id, filename, type, size, created_at, status, file_path, compression FROM backups ORDER BY id", rd_backup)
LOAD_HELPER(reverse_proxies, proxy::ReverseProxy, "SELECT id, domain, site_id, provider, config_path, upstream, enabled, status FROM reverse_proxies ORDER BY id", rd_proxy)
LOAD_HELPER(access_users, access::AccessUser, "SELECT id, username, auth_type, password_hash, enabled FROM access_users ORDER BY id", rd_au)
LOAD_HELPER(access_grants, access::AccessGrant, "SELECT id, access_user_id, site_id, permission FROM access_grants ORDER BY id", rd_ag)
LOAD_HELPER(auth_users, auth::AuthUser, "SELECT id, username, password_hash, must_change_password, enabled, role FROM auth_users ORDER BY id", rd_authu)
LOAD_HELPER(ssl_certificates, ssl::SslCertificate, "SELECT id, domain_id, domain, provider, certificate_path, key_path, chain_path, issued_at, expires_at, renew_after, status, auto_renew, https_enabled, redirect_enabled, domains, challenge_type, last_error, last_validation, renew_attempts, version FROM ssl_certificates ORDER BY id", rd_ssl)
LOAD_HELPER(mail_domains, mail::MailDomain, "SELECT id, domain_id, site_id, domain_name, mode, relay_host, dkim_selector, dkim_private_key_path, dkim_public_key_dns, max_mailboxes, max_aliases, catch_all, enabled, created_at, updated_at FROM mail_domains ORDER BY id", rd_md)
LOAD_HELPER(mail_mailboxes, mail::Mailbox, "SELECT id, domain_id, local_part, password_hash, quota_bytes, quota_messages, enabled, display_name, forward_to, spam_enabled, last_login, created_at, updated_at FROM mail_mailboxes ORDER BY id", rd_mb)
LOAD_HELPER(mail_aliases, mail::MailAlias, "SELECT id, domain_id, source_local_part, destination, enabled, created_at, updated_at FROM mail_aliases ORDER BY id", rd_ma)

// ---- Common verify helper using LegacyDatasetReader ----
#define VERIFY_COMMON(type, cls, canon_fn, field_fn, transient_fn, reader_call, load_call, required) \
    ResourceVerificationResult Verification::verify_##type() { \
        { ReadLease _rl(pool_); if (!_rl.is_valid()) { if (!pool_.initialize(sqlite_path_)) { \
            ResourceVerificationResult r; r.success = false; r.error = "pool_not_initialized"; r.resource_type = #type; return r; } } } \
        const ImportResult* ir = find_import_result(#type); \
        if (!ir) { ResourceVerificationResult r; r.success = false; r.status = VerificationStatus::Failed; r.error = "missing_import_result"; r.resource_type = #type; return r; } \
        if (ir->disposition == ImportDisposition::SkippedEmpty || ir->disposition == ImportDisposition::SkippedMissingOptional) { \
            /* Checked full-state load and compare with baseline */ \
            std::vector<cls> sqlite_records; \
            auto lr = load_call(pool_, sqlite_records); \
            if (!lr.success) { ResourceVerificationResult r; r.success = false; r.status = VerificationStatus::Failed; r.error = "baseline_load_failed"; r.resource_type = #type; return r; } \
            uint64_t curr_count = sqlite_records.size(); \
            std::string curr_checksum = sha256(canon_fn(sqlite_records)); \
            if (curr_count != ir->baseline.record_count || curr_checksum != ir->baseline.canonical_checksum) { \
                ResourceVerificationResult r; r.success = false; r.status = VerificationStatus::Failed; r.error = "baseline_mismatch"; r.resource_type = #type; \
                r.legacy_record_count = ir->baseline.record_count; r.sqlite_record_count = curr_count; \
                r.legacy_checksum = ir->baseline.canonical_checksum; r.sqlite_checksum = curr_checksum; return r; } \
            ResourceVerificationResult r; r.success = true; r.status = VerificationStatus::Skipped; r.resource_type = #type; \
            r.legacy_record_count = ir->baseline.record_count; r.sqlite_record_count = curr_count; \
            r.legacy_checksum = ir->baseline.canonical_checksum; r.sqlite_checksum = curr_checksum; return r; \
        } \
        LegacyDatasetReader reader(legacy_dir_); \
        auto dr = reader.reader_call; \
        if (!dr.success) { ResourceVerificationResult r; r.success = false; r.error = dr.error; r.resource_type = #type; return r; } \
        std::vector<cls> sqlite_records; \
        auto lr = load_call(pool_, sqlite_records); \
        if (!lr.success) { lr.resource_type = #type; return lr; } \
        return compare_resource<cls>(#type, std::move(dr.records), std::move(sqlite_records), \
            [this](const std::vector<cls>& v) { return canon_fn(v); }, field_fn, transient_fn); \
    }

#define FIELD_ADAPTOR(type, func) \
    [](const type& a, const type& b, bool sens) { return func(a, b, sens); }
#define TRANSIENT_NULL nullptr

// ---- Transient validation ----
static bool transient_php(const php::PhpVersion& pv) { return pv.name == pv.version; }
static bool transient_profile(const profile::Profile& p) { return p.name == p.profile_name; }
static bool transient_user(const user::User& u) { return u.name == u.username; }
static bool transient_site(const site::Site& s) { return s.name == s.domain && s.php_mail_enabled_present; }
static bool transient_domain(const domain::Domain& d) { return d.name == d.fqdn; }
static bool transient_database(const database::Database& d) { return d.name == d.db_name; }
static bool transient_proxy(const proxy::ReverseProxy& p) { return p.name == p.domain; }
static bool transient_au(const access::AccessUser& u) { return u.name == u.username; }
static bool transient_authu(const auth::AuthUser& u) { return u.name == u.username; }
static bool transient_ssl(const ssl::SslCertificate& c) { return c.name == c.domain; }
static bool transient_md(const mail::MailDomain& m) { return m.name == m.domain_name; }
static bool transient_mb(const mail::Mailbox& mb) { return mb.name == mb.local_part; }
static bool transient_ma(const mail::MailAlias& a) { return a.name == a.source_local_part; }

// ---- Per-resource verify methods ----
VERIFY_COMMON(nodes, node::Node, canonical_nodes, FIELD_ADAPTOR(node::Node, compare_node), TRANSIENT_NULL, read_nodes(), load_nodes, true)
VERIFY_COMMON(php_versions, php::PhpVersion, canonical_php_versions, FIELD_ADAPTOR(php::PhpVersion, compare_php), transient_php, read_php_versions(), load_php_versions, true)
VERIFY_COMMON(profiles, profile::Profile, canonical_profiles, FIELD_ADAPTOR(profile::Profile, compare_profile), transient_profile, read_combined_profiles(), load_profiles, true)
VERIFY_COMMON(users, user::User, canonical_users, FIELD_ADAPTOR(user::User, compare_user), transient_user, read_users(), load_users, true)
VERIFY_COMMON(sites, site::Site, canonical_sites, FIELD_ADAPTOR(site::Site, compare_site), transient_site, read_sites(), load_sites, true)
VERIFY_COMMON(domains, domain::Domain, canonical_domains, FIELD_ADAPTOR(domain::Domain, compare_domain), transient_domain, read_domains(), load_domains, true)
VERIFY_COMMON(databases, database::Database, canonical_databases, FIELD_ADAPTOR(database::Database, compare_database), transient_database, read_databases(), load_databases, true)
VERIFY_COMMON(backups, backup::Backup, canonical_backups, FIELD_ADAPTOR(backup::Backup, compare_backup), TRANSIENT_NULL, read_backups(), load_backups, true)
VERIFY_COMMON(reverse_proxies, proxy::ReverseProxy, canonical_reverse_proxies, FIELD_ADAPTOR(proxy::ReverseProxy, compare_proxy), transient_proxy, read_reverse_proxies(), load_reverse_proxies, true)
VERIFY_COMMON(access_users, access::AccessUser, canonical_access_users, FIELD_ADAPTOR(access::AccessUser, compare_access_user), transient_au, read_access_users(), load_access_users, false)
VERIFY_COMMON(access_grants, access::AccessGrant, canonical_access_grants, FIELD_ADAPTOR(access::AccessGrant, compare_access_grant), TRANSIENT_NULL, read_access_grants(), load_access_grants, false)
VERIFY_COMMON(auth_users, auth::AuthUser, canonical_auth_users, FIELD_ADAPTOR(auth::AuthUser, compare_auth_user), transient_authu, read_auth_users(), load_auth_users, false)
VERIFY_COMMON(ssl_certificates, ssl::SslCertificate, canonical_ssl_certificates, FIELD_ADAPTOR(ssl::SslCertificate, compare_ssl), transient_ssl, read_ssl_certificates(), load_ssl_certificates, false)
VERIFY_COMMON(mail_domains, mail::MailDomain, canonical_mail_domains, FIELD_ADAPTOR(mail::MailDomain, compare_mail_domain), transient_md, read_mail_domains(), load_mail_domains, false)
VERIFY_COMMON(mail_mailboxes, mail::Mailbox, canonical_mail_mailboxes, FIELD_ADAPTOR(mail::Mailbox, compare_mailbox), transient_mb, read_mailboxes(), load_mail_mailboxes, false)
VERIFY_COMMON(mail_aliases, mail::MailAlias, canonical_mail_aliases, FIELD_ADAPTOR(mail::MailAlias, compare_mail_alias), transient_ma, read_mail_aliases(), load_mail_aliases, false)

// ---- mail_config custom verify ----
ResourceVerificationResult Verification::verify_mail_config() {
    { ReadLease _rl(pool_); if (!_rl.is_valid()) { if (!pool_.initialize(sqlite_path_)) {
        ResourceVerificationResult r; r.success = false; r.error = "pool_not_initialized"; r.resource_type = "mail_config"; return r; } } }
    const ImportResult* ir = find_import_result("mail_config");
    if (!ir) { ResourceVerificationResult r; r.success = false; r.status = VerificationStatus::Failed; r.error = "missing_import_result"; r.resource_type = "mail_config"; return r; }
        if (ir->disposition == ImportDisposition::SkippedEmpty || ir->disposition == ImportDisposition::SkippedMissingOptional) {
        // Checked full-state mail_config baseline verification
        std::string curr_ms, curr_sh;
        { ReadLease rl(pool_);
          if (!rl.is_valid()) { ResourceVerificationResult r; r.success = false; r.status = VerificationStatus::Failed; r.error = "baseline_load_failed"; r.resource_type = "mail_config"; return r; }
          if (!rl->prepare("SELECT value FROM mail_config WHERE key = 'module_state'")) { ResourceVerificationResult r; r.success = false; r.error = "baseline_load_failed"; r.resource_type = "mail_config"; return r; }
          if (rl->step()) curr_ms = rl->column_text(0);
          if (!rl->prepare("SELECT value FROM mail_config WHERE key = 'smarthost'")) { ResourceVerificationResult r; r.success = false; r.error = "baseline_load_failed"; r.resource_type = "mail_config"; return r; }
          if (rl->step()) curr_sh = rl->column_text(0);
        }
        std::string curr_checksum = sha256(canonical_mail_config(curr_ms, curr_sh));
        if (curr_checksum != ir->baseline.canonical_checksum) {
            ResourceVerificationResult r; r.success = false; r.status = VerificationStatus::Failed;
            r.error = "baseline_mismatch"; r.resource_type = "mail_config";
            r.legacy_checksum = ir->baseline.canonical_checksum; r.sqlite_checksum = curr_checksum; return r; }
        ResourceVerificationResult r; r.success = true; r.status = VerificationStatus::Skipped; r.resource_type = "mail_config";
        r.legacy_record_count = ir->baseline.record_count; r.sqlite_record_count = ir->baseline.record_count;
        r.legacy_checksum = ir->baseline.canonical_checksum; r.sqlite_checksum = ir->baseline.canonical_checksum; return r; }
    ResourceVerificationResult r; r.resource_type = "mail_config";

    LegacyDatasetReader reader(legacy_dir_);
    auto dr = reader.read_mail_config();
    if (!dr.success) { r.error = dr.error; return r; }

    // SQLite config via checked query
    std::string module_state, smarthost;
    {
        ReadLease rl(pool_);
        if (!rl.is_valid()) { r.error = "sqlite_load_failed"; return r; }
        if (!rl->prepare("SELECT value FROM mail_config WHERE key = 'module_state'")) {
            r.error = "query_failed:module_state"; return r; }
        if (rl->step()) module_state = rl->column_text(0);
        if (!rl->prepare("SELECT value FROM mail_config WHERE key = 'smarthost'")) {
            r.error = "query_failed:smarthost"; return r; }
        if (rl->step()) smarthost = rl->column_text(0);
    }

    r.legacy_record_count = (dr.module_state.empty() ? 0 : 1) + (dr.smarthost.empty() ? 0 : 1);
    r.sqlite_record_count = (module_state.empty() ? 0 : 1) + (smarthost.empty() ? 0 : 1);
    r.legacy_checksum = sha256(canonical_mail_config(dr.module_state, dr.smarthost));
    r.sqlite_checksum = sha256(canonical_mail_config(module_state, smarthost));

    if (r.legacy_record_count != r.sqlite_record_count || r.legacy_checksum != r.sqlite_checksum) {
        // Field-by-field mail_config comparison
        if (dr.module_state != module_state) {
            FieldMismatch fm; fm.resource_type = "mail_config";
            fm.field = "module_state"; fm.expected = "[REDACTED]"; fm.actual = "[REDACTED]";
            r.mismatches.push_back(fm);
        }
        if (dr.smarthost != smarthost) {
            FieldMismatch fm; fm.resource_type = "mail_config";
            fm.field = "smarthost"; fm.expected = "[REDACTED]"; fm.actual = "[REDACTED]";
            r.mismatches.push_back(fm);
        }
        r.success = false; r.status = VerificationStatus::Failed; return r;
    }
    r.success = true; r.status = VerificationStatus::Passed; return r;
}

// ============================================================
// Shared PRAGMA helpers
// ============================================================

static bool checked_fk_check(ConnectionPool& pool, std::vector<std::string>& violations, std::string& error) {
    ReadLease rl(pool);
    if (!rl.is_valid()) { error = "fk_no_lease"; return false; }
    if (!rl->prepare("PRAGMA foreign_key_check")) { error = "fk_prepare_failed"; return false; }
    while (true) {
        if (!rl->step()) {
            if (rl->error_code() != 0) { error = "fk_step_failed"; return false; }
            break;
        }
        violations.push_back(std::string("table=") + rl->column_text(0) +
            " rowid=" + std::to_string(rl->column_int(1)));
    }
    return true;
}

static bool checked_integrity(ConnectionPool& pool, std::string& result, std::string& error) {
    ReadLease rl(pool);
    if (!rl.is_valid()) { error = "integrity_no_lease"; return false; }
    if (!rl->prepare("PRAGMA integrity_check")) { error = "integrity_prepare_failed"; return false; }
    if (!rl->step()) { error = "integrity_no_result"; return false; }
    result = rl->column_text(0);
    return true;
}

// ============================================================
// verify_all — complete pipeline
// ============================================================

DatabaseVerificationResult Verification::verify_all() {
    DatabaseVerificationResult result;

    // Validate import context
    const std::vector<std::string> kExpectedResources = {
        "nodes", "php_versions", "profiles", "users", "sites", "domains",
        "databases", "backups", "reverse_proxies", "access_users",
        "access_grants", "auth_users", "ssl_certificates", "mail_domains",
        "mail_mailboxes", "mail_aliases", "mail_config"
    };
    std::set<std::string> expected_set(kExpectedResources.begin(), kExpectedResources.end());
    std::set<std::string> seen;

    if (!import_result_.success) { result.error = "import_failed"; result.success = false; return result; }
    for (const auto& ir : import_result_.resources) {
        if (ir.disposition == ImportDisposition::Failed) { result.error = "incomplete_import_context:" + ir.resource_type; result.success = false; return result; }
        if (ir.resource_type == "template_profiles") { result.error = "unexpected_resource:template_profiles"; result.success = false; return result; }
        if (seen.count(ir.resource_type)) { result.error = "duplicate_import_result:" + ir.resource_type; result.success = false; return result; }
        if (!expected_set.count(ir.resource_type)) { result.error = "unknown_import_resource:" + ir.resource_type; result.success = false; return result; }
        seen.insert(ir.resource_type);
    }
    for (const auto& exp : kExpectedResources) {
        if (!seen.count(exp)) { result.error = "missing_import_result:" + exp; result.success = false; return result; }
    }

    // Initialize pool
    if (!pool_.initialize(sqlite_path_)) { result.error = "sqlite_open_failed"; result.success = false; return result; }
    {
        ReadLease rl(pool_);
        if (!rl.is_valid() || !rl->prepare("SELECT COUNT(*) FROM nodes")) { result.error = "schema_not_ready"; result.success = false; return result; }
    }

    // Verify all resources
    auto add = [&](ResourceVerificationResult r) { result.resources.push_back(std::move(r)); };
    add(verify_nodes()); add(verify_php_versions()); add(verify_profiles());
    add(verify_users()); add(verify_sites()); add(verify_domains());
    add(verify_databases()); add(verify_backups()); add(verify_reverse_proxies());
    add(verify_access_users()); add(verify_access_grants()); add(verify_auth_users());
    add(verify_ssl_certificates()); add(verify_mail_domains());
    add(verify_mail_mailboxes()); add(verify_mail_aliases()); add(verify_mail_config());

    result.initial_verification_passed = true;
    for (const auto& res : result.resources) {
        if (!res.success) { result.initial_verification_passed = false; result.error = "resource_verification_failed:" + res.resource_type; break; }
    }

    // FK check
    if (!checked_fk_check(pool_, result.initial_foreign_key_violations, result.error)) {
        result.success = false; return result;
    }

    // Integrity check
    if (!checked_integrity(pool_, result.initial_integrity_check_result, result.error) || result.initial_integrity_check_result != "ok") {
        result.success = false; return result;
    }

    if (!result.initial_verification_passed || !result.initial_foreign_key_violations.empty()) {
        result.success = false; return result;
    }

    // Capture immutable initial evidence from successful verification
    for (const auto& res : result.resources) {
        InitialEvidence ev;
        ev.legacy_record_count = res.legacy_record_count;
        ev.legacy_checksum = res.legacy_checksum;
        initial_evidence_[res.resource_type] = ev;
    }

    // Freeze typed evidence for reopened field comparison
    {
        auto& te = typed_evidence_;
        if (initial_evidence_.count("nodes")) {
            std::vector<node::Node> v; auto lr = LOAD_HELPER_CALL(nodes, pool_, v); if (lr.success) te.nodes = std::move(v); }
        if (initial_evidence_.count("php_versions")) {
            std::vector<php::PhpVersion> v; auto lr = LOAD_HELPER_CALL(php_versions, pool_, v); if (lr.success) te.php_versions = std::move(v); }
        if (initial_evidence_.count("profiles")) {
            std::vector<profile::Profile> v; auto lr = LOAD_HELPER_CALL(profiles, pool_, v); if (lr.success) te.profiles = std::move(v); }
        if (initial_evidence_.count("users")) {
            std::vector<user::User> v; auto lr = LOAD_HELPER_CALL(users, pool_, v); if (lr.success) te.users = std::move(v); }
        if (initial_evidence_.count("sites")) {
            std::vector<site::Site> v; auto lr = LOAD_HELPER_CALL(sites, pool_, v); if (lr.success) te.sites = std::move(v); }
        if (initial_evidence_.count("domains")) {
            std::vector<domain::Domain> v; auto lr = LOAD_HELPER_CALL(domains, pool_, v); if (lr.success) te.domains = std::move(v); }
        if (initial_evidence_.count("databases")) {
            std::vector<database::Database> v; auto lr = LOAD_HELPER_CALL(databases, pool_, v); if (lr.success) te.databases = std::move(v); }
        if (initial_evidence_.count("backups")) {
            std::vector<backup::Backup> v; auto lr = LOAD_HELPER_CALL(backups, pool_, v); if (lr.success) te.backups = std::move(v); }
        if (initial_evidence_.count("reverse_proxies")) {
            std::vector<proxy::ReverseProxy> v; auto lr = LOAD_HELPER_CALL(reverse_proxies, pool_, v); if (lr.success) te.reverse_proxies = std::move(v); }
        if (initial_evidence_.count("access_users")) {
            std::vector<access::AccessUser> v; auto lr = LOAD_HELPER_CALL(access_users, pool_, v); if (lr.success) te.access_users = std::move(v); }
        if (initial_evidence_.count("access_grants")) {
            std::vector<access::AccessGrant> v; auto lr = LOAD_HELPER_CALL(access_grants, pool_, v); if (lr.success) te.access_grants = std::move(v); }
        if (initial_evidence_.count("auth_users")) {
            std::vector<auth::AuthUser> v; auto lr = LOAD_HELPER_CALL(auth_users, pool_, v); if (lr.success) te.auth_users = std::move(v); }
        if (initial_evidence_.count("ssl_certificates")) {
            std::vector<ssl::SslCertificate> v; auto lr = LOAD_HELPER_CALL(ssl_certificates, pool_, v); if (lr.success) te.ssl_certificates = std::move(v); }
        if (initial_evidence_.count("mail_domains")) {
            std::vector<mail::MailDomain> v; auto lr = LOAD_HELPER_CALL(mail_domains, pool_, v); if (lr.success) te.mail_domains = std::move(v); }
        if (initial_evidence_.count("mail_mailboxes")) {
            std::vector<mail::Mailbox> v; auto lr = LOAD_HELPER_CALL(mail_mailboxes, pool_, v); if (lr.success) te.mail_mailboxes = std::move(v); }
        if (initial_evidence_.count("mail_aliases")) {
            std::vector<mail::MailAlias> v; auto lr = LOAD_HELPER_CALL(mail_aliases, pool_, v); if (lr.success) te.mail_aliases = std::move(v); }
        { ReadLease rl(pool_);
          if (rl.is_valid()) {
              if (rl->prepare("SELECT value FROM mail_config WHERE key = 'module_state'") && rl->step()) te.mail_module_state = rl->column_text(0);
              if (rl->prepare("SELECT value FROM mail_config WHERE key = 'smarthost'") && rl->step()) te.mail_smarthost = rl->column_text(0);
          }
        }
    }

    // Production reopen: shut down original pool, use real Storage
    pool_.shutdown();

    auto make_pool = [&](ConnectionPool& p, const std::string& label) -> bool {
        if (!p.initialize(sqlite_path_)) { result.error = label + "_pool_failed"; return false; }
        return true;
    };

    {
        Storage reopen_storage(storage_dir_, StorageOptions{CoreStorageBackend::SqlitePhase5});
        if (!reopen_storage.sqlite_ready()) {
            result.reopen_succeeded = false; result.error = "reopen_failed";
            result.success = false; return result;
        }

        bool reopen_pass = true;

        // Three-way reopen comparison: initial_evidence == Storage == checked pool
        auto reopen_compare = [&](const std::string& name,
            uint64_t storage_count, const std::string& storage_checksum,
            bool checked_ok, const std::string& checked_checksum) {
            ResourceVerificationResult rr; rr.resource_type = name;
            rr.sqlite_record_count = storage_count;
            rr.sqlite_checksum = storage_checksum;

            // Check initial evidence
            auto it = initial_evidence_.find(name);
            uint64_t expected_count = (it != initial_evidence_.end()) ? it->second.legacy_record_count : 0;
            std::string expected_checksum = (it != initial_evidence_.end()) ? it->second.legacy_checksum : "";
            rr.legacy_record_count = expected_count;
            rr.legacy_checksum = expected_checksum;

            if (!checked_ok) {
                rr.success = false; rr.error = "reopen_checked_load_failed";
            } else if (storage_count != expected_count) {
                rr.success = false; rr.error = "reopen_count_mismatch";
            } else if (storage_checksum != expected_checksum) {
                rr.success = false; rr.error = "reopen_initial_mismatch";
            } else if (storage_checksum != checked_checksum) {
                rr.success = false; rr.error = "reopen_checksum_mismatch";
            } else {
                rr.success = true;
            }
            if (!rr.success) { result.error = "reopen_" + name; reopen_pass = false; }
            result.reopened_resources.push_back(rr);
        };

        // Helper for one resource reopen comparison
        #define REOPEN_ONE(name, storage_vec, typed_vec, checked_load, canon_fn, field_fn, transient_fn) \
        { \
            auto storage_records = storage_vec; \
            std::string storage_checksum = sha256(canon_fn(storage_records)); \
            ConnectionPool cp; \
            bool checked_ok = false; std::string checked_checksum; \
            if (make_pool(cp, "reopen_" name)) { \
                std::vector<std::decay_t<decltype(*storage_records.begin())>> confirm_records; \
                auto cr = checked_load(cp, confirm_records); \
                cp.shutdown(); \
                checked_ok = cr.success; \
                if (checked_ok) checked_checksum = sha256(canon_fn(confirm_records)); \
            } \
            reopen_compare(name, storage_records.size(), storage_checksum, checked_ok, checked_checksum); \
            /* If mismatch, add field comparison vs typed evidence */ \
            auto& rr = result.reopened_resources.back(); \
            if (!rr.success && !typed_vec.empty()) { \
                auto fres = compare_resource<std::decay_t<decltype(*typed_vec.begin())>>( \
                    name, std::vector<std::decay_t<decltype(*typed_vec.begin())>>(typed_vec), \
                    std::move(storage_records), canon_fn, field_fn, transient_fn); \
                rr.mismatches = std::move(fres.mismatches); \
                rr.legacy_checksum = fres.legacy_checksum; \
            } \
        }

        REOPEN_ONE("nodes", reopen_storage.load_nodes(), typed_evidence_.nodes, load_nodes, [this](const std::vector<node::Node>& v) { return canonical_nodes(v); }, FIELD_ADAPTOR(node::Node, compare_node), TRANSIENT_NULL);
        REOPEN_ONE("php_versions", reopen_storage.load_php_versions(), typed_evidence_.php_versions, load_php_versions, [this](const std::vector<php::PhpVersion>& v) { return canonical_php_versions(v); }, FIELD_ADAPTOR(php::PhpVersion, compare_php), transient_php);
        REOPEN_ONE("profiles", reopen_storage.load_profiles(), typed_evidence_.profiles, load_profiles, [this](const std::vector<profile::Profile>& v) { return canonical_profiles(v); }, FIELD_ADAPTOR(profile::Profile, compare_profile), transient_profile);
        REOPEN_ONE("users", reopen_storage.load_users(), typed_evidence_.users, load_users, [this](const std::vector<user::User>& v) { return canonical_users(v); }, FIELD_ADAPTOR(user::User, compare_user), transient_user);
        REOPEN_ONE("sites", reopen_storage.load_sites(), typed_evidence_.sites, load_sites, [this](const std::vector<site::Site>& v) { return canonical_sites(v); }, FIELD_ADAPTOR(site::Site, compare_site), transient_site);
        REOPEN_ONE("domains", reopen_storage.load_domains(), typed_evidence_.domains, load_domains, [this](const std::vector<domain::Domain>& v) { return canonical_domains(v); }, FIELD_ADAPTOR(domain::Domain, compare_domain), transient_domain);
        REOPEN_ONE("databases", reopen_storage.load_databases(), typed_evidence_.databases, load_databases, [this](const std::vector<database::Database>& v) { return canonical_databases(v); }, FIELD_ADAPTOR(database::Database, compare_database), transient_database);
        REOPEN_ONE("reverse_proxies", reopen_storage.load_reverse_proxies(), typed_evidence_.reverse_proxies, load_reverse_proxies, [this](const std::vector<proxy::ReverseProxy>& v) { return canonical_reverse_proxies(v); }, FIELD_ADAPTOR(proxy::ReverseProxy, compare_proxy), transient_proxy);
        REOPEN_ONE("access_users", reopen_storage.load_access_users(), typed_evidence_.access_users, load_access_users, [this](const std::vector<access::AccessUser>& v) { return canonical_access_users(v); }, FIELD_ADAPTOR(access::AccessUser, compare_access_user), transient_au);
        REOPEN_ONE("access_grants", reopen_storage.load_access_grants(), typed_evidence_.access_grants, load_access_grants, [this](const std::vector<access::AccessGrant>& v) { return canonical_access_grants(v); }, FIELD_ADAPTOR(access::AccessGrant, compare_access_grant), TRANSIENT_NULL);
        REOPEN_ONE("ssl_certificates", reopen_storage.load_ssl_certificates(), typed_evidence_.ssl_certificates, load_ssl_certificates, [this](const std::vector<ssl::SslCertificate>& v) { return canonical_ssl_certificates(v); }, FIELD_ADAPTOR(ssl::SslCertificate, compare_ssl), transient_ssl);
        REOPEN_ONE("mail_domains", reopen_storage.load_mail_domains(), typed_evidence_.mail_domains, load_mail_domains, [this](const std::vector<mail::MailDomain>& v) { return canonical_mail_domains(v); }, FIELD_ADAPTOR(mail::MailDomain, compare_mail_domain), transient_md);
        REOPEN_ONE("mail_mailboxes", reopen_storage.load_mailboxes(), typed_evidence_.mail_mailboxes, load_mail_mailboxes, [this](const std::vector<mail::Mailbox>& v) { return canonical_mail_mailboxes(v); }, FIELD_ADAPTOR(mail::Mailbox, compare_mailbox), transient_mb);
        REOPEN_ONE("mail_aliases", reopen_storage.load_mail_aliases(), typed_evidence_.mail_aliases, load_mail_aliases, [this](const std::vector<mail::MailAlias>& v) { return canonical_mail_aliases(v); }, FIELD_ADAPTOR(mail::MailAlias, compare_mail_alias), transient_ma);

        // mail_config
        {
            auto storage_ms = reopen_storage.load_mail_module_state();
            auto storage_sh = reopen_storage.load_mail_smarthost();
            std::string storage_checksum = sha256(canonical_mail_config(storage_ms, storage_sh));
            ConnectionPool cp;
            bool checked_ok = false; std::string checked_checksum;
            if (make_pool(cp, "reopen_mc")) {
                std::string confirm_ms, confirm_sh;
                ReadLease rl(cp);
                if (!rl.is_valid()) { cp.shutdown(); }
                else if (!rl->prepare("SELECT value FROM mail_config WHERE key = 'module_state'")) { cp.shutdown(); }
                else {
                    if (rl->step()) confirm_ms = rl->column_text(0);
                    if (!rl->prepare("SELECT value FROM mail_config WHERE key = 'smarthost'")) { cp.shutdown(); }
                    else {
                        if (rl->step()) confirm_sh = rl->column_text(0);
                        cp.shutdown();
                        checked_ok = true;
                        checked_checksum = sha256(canonical_mail_config(confirm_ms, confirm_sh));
                    }
                }
            }
            reopen_compare("mail_config", 0, storage_checksum, checked_ok, checked_checksum);
        }

        // Importer-only: backups, auth_users via direct SQLite (not Storage)
        {
            ConnectionPool io_pool;
            if (!make_pool(io_pool, "reopen_importer_only")) { result.success = false; return result; }
            // backups
            {
                std::vector<backup::Backup> backup_records;
                auto blr = load_backups(io_pool, backup_records);
                reopen_compare("backups", backup_records.size(),
                    blr.success ? sha256(canonical_backups(backup_records)) : "",
                    blr.success, blr.success ? sha256(canonical_backups(backup_records)) : "");
            }
            // auth_users
            {
                std::vector<auth::AuthUser> auth_records;
                auto alr = load_auth_users(io_pool, auth_records);
                reopen_compare("auth_users", auth_records.size(),
                    alr.success ? sha256(canonical_auth_users(auth_records)) : "",
                    alr.success, alr.success ? sha256(canonical_auth_users(auth_records)) : "");
            }
            io_pool.shutdown();
        }

        std::set<std::string> reopened_seen;
        if (result.reopened_resources.size() != 17) {
            result.error = "invalid_reopened_count:" + std::to_string(result.reopened_resources.size());
            reopen_pass = false;
        }
        for (const auto& rr : result.reopened_resources) {
            if (reopened_seen.count(rr.resource_type)) {
                result.error = std::string("duplicate_reopened:") + rr.resource_type;
                reopen_pass = false;
            }
            reopened_seen.insert(rr.resource_type);
        }
        std::vector<std::string> required_reopened = {
            "nodes","php_versions","profiles","users","sites","domains","databases",
            "reverse_proxies","access_users","access_grants","ssl_certificates",
            "mail_domains","mail_mailboxes","mail_aliases","mail_config","backups","auth_users"
        };
        for (const auto& r : required_reopened) {
            if (!reopened_seen.count(r)) {
                result.error = std::string("missing_reopened:") + r; reopen_pass = false;
            }
        }
        for (const auto& seen : reopened_seen) {
            if (std::find(required_reopened.begin(), required_reopened.end(), seen) == required_reopened.end()) {
                result.error = std::string("unknown_reopened:") + seen; reopen_pass = false;
            }
        }

        // Post-reopen FK and integrity via shared helpers on fresh pool
        {
            ConnectionPool post_pool;
            if (!make_pool(post_pool, "reopen_pragma")) { result.success = false; return result; }
            std::string fk_err;
            if (!checked_fk_check(post_pool, result.reopened_foreign_key_violations, fk_err)) {
                result.error = "post_reopen_" + fk_err; reopen_pass = false;
            } else if (!result.reopened_foreign_key_violations.empty()) {
                result.error = "post_reopen_fk_violation"; reopen_pass = false;
            }
            std::string integrity_err;
            if (!checked_integrity(post_pool, result.reopened_integrity_check_result, integrity_err) ||
                result.reopened_integrity_check_result != "ok") {
                result.error = "post_reopen_integrity_failed"; reopen_pass = false;
            }
            post_pool.shutdown();
        }

        result.reopen_succeeded = true;
        result.reopened_verification_passed = reopen_pass;
        if (!reopen_pass) { result.success = false; return result; }
    }

    result.success = true;
    return result;
}

} // namespace containercp::storage
