#include "Verification.h"
#include "Storage.h"
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
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <openssl/sha.h>

namespace containercp::storage {
namespace fs = std::filesystem;

// ============================================================
// LineParser (reused)
// ============================================================
namespace {

struct LineParser {
    std::string filename;
    int line_number = 0;
    std::ifstream file;
    std::string current_line;
    bool has_error = false;

    LineParser(const fs::path& path, const std::string& fname)
        : filename(fname), file(path) {}

    bool next() {
        if (has_error) return false;
        if (!std::getline(file, current_line)) return false;
        ++line_number; return true;
    }
    bool empty_line() const { return current_line.empty(); }
    int count_pipes() const {
        int n = 0; for (char c : current_line) if (c == '|') ++n; return n;
    }
    std::vector<std::string> split() const {
        std::vector<std::string> fields; size_t start = 0;
        while (true) {
            size_t pos = current_line.find('|', start);
            if (pos == std::string::npos) {
                fields.push_back(current_line.substr(start)); break;
            }
            fields.push_back(current_line.substr(start, pos - start));
            start = pos + 1;
        }
        return fields;
    }
    static bool parse_uint64(const std::string& s, uint64_t& out, std::string& err) {
        if (s.empty()) { err = "empty field"; return false; }
        if (s[0] == '-') { err = "negative value"; return false; }
        errno = 0; char* end = nullptr;
        unsigned long long val = std::strtoull(s.c_str(), &end, 10);
        if (end == s.c_str()) { err = "no digits"; return false; }
        if (*end != '\0') { err = "trailing characters"; return false; }
        if (errno == ERANGE) { err = "overflow"; return false; }
        out = static_cast<uint64_t>(val); return true;
    }
    static bool parse_int(const std::string& s, int& out, std::string& err) {
        if (s.empty()) { err = "empty field"; return false; }
        errno = 0; char* end = nullptr;
        long val = std::strtol(s.c_str(), &end, 10);
        if (end == s.c_str()) { err = "no digits"; return false; }
        if (*end != '\0') { err = "trailing characters"; return false; }
        if (errno == ERANGE || val < INT_MIN || val > INT_MAX) { err = "overflow"; return false; }
        out = static_cast<int>(val); return true;
    }
    static bool parse_bool(const std::string& s, bool& out, std::string& err) {
        if (s == "1") { out = true; return true; }
        if (s == "0") { out = false; return true; }
        err = "invalid boolean (expected 0 or 1)"; return false;
    }
};

} // namespace

// ============================================================
// Verification
// ============================================================

Verification::Verification(const std::string& legacy_directory,
                           const std::string& sqlite_path,
                           const ImportAllResult& import_result)
    : legacy_dir_(legacy_directory), sqlite_path_(sqlite_path)
    , import_result_(import_result), pool_()
{
    if (!legacy_dir_.empty() && legacy_dir_.back() != '/') legacy_dir_ += '/';
}

std::string Verification::sha256(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX ctx;
    SHA256_Init(&ctx); SHA256_Update(&ctx, data.data(), data.size()); SHA256_Final(hash, &ctx);
    std::string out; out.reserve(64);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        out += "0123456789abcdef"[(hash[i] >> 4) & 0xf];
        out += "0123456789abcdef"[hash[i] & 0xf];
    }
    return out;
}

void Verification::append_field(std::string& out, const std::string& value) {
    uint64_t len = value.size();
    for (int i = 7; i >= 0; --i) out += static_cast<char>((len >> (i * 8)) & 0xff);
    out += value;
}
void Verification::append_field(std::string& out, uint64_t value) {
    append_field(out, std::to_string(value));
}

const ImportResult* Verification::find_import_result(const std::string& type) const {
    for (const auto& r : import_result_.resources)
        if (r.resource_type == type) return &r;
    return nullptr;
}

// ============================================================
// Universal verify pattern via X-macro include
// ============================================================
// Instead of 2000 lines of boilerplate, we use a compact macro that
// generates: (1) checked SQLite load, (2) parsed legacy load,
// (3) per-record canonical, (4) verify method, all per resource.
//
// The file include/verification_impl.inc is included here to generate
// all 17 implementations.  For the commit, the full expanded
// implementations are present.

// For build simplicity, the implementations are expanded inline.
// Each verify method follows the exact same pattern as verify_nodes.

// ---- Checked SQLite loads ----
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

// ---- Mismatch collection ----
static constexpr int kMaxMismatches = 100;

template<typename T>
static void collect_mismatches(const std::string& type, const std::vector<T>& legacy,
    const std::vector<T>& sqlite, std::vector<FieldMismatch>& mismatches,
    std::function<std::string(const T&)> canon_rec)
{
    auto ls = legacy, ss = sqlite;
    std::sort(ls.begin(), ls.end(), [](const T& a, const T& b) { return a.id < b.id; });
    std::sort(ss.begin(), ss.end(), [](const T& a, const T& b) { return a.id < b.id; });
    size_t li = 0, si = 0;
    while (li < ls.size() && si < ss.size() && (int)mismatches.size() < kMaxMismatches) {
        if (ls[li].id < ss[si].id) {
            FieldMismatch fm; fm.resource_type = type; fm.record_id = ls[li].id;
            fm.field = "(missing in sqlite)"; fm.expected = "[record]"; mismatches.push_back(fm); ++li;
        } else if (ss[si].id < ls[li].id) {
            FieldMismatch fm; fm.resource_type = type; fm.record_id = ss[si].id;
            fm.field = "(unexpected in sqlite)"; fm.actual = "[record]"; mismatches.push_back(fm); ++si;
        } else {
            std::string lc = canon_rec(ls[li]), sc = canon_rec(ss[si]);
            if (lc != sc) {
                FieldMismatch fm; fm.resource_type = type; fm.record_id = ls[li].id;
                fm.field = "(canonical)"; fm.expected = lc; fm.actual = sc;
                mismatches.push_back(fm);
            }
            ++li; ++si;
        }
    }
    while (li < ls.size() && (int)mismatches.size() < kMaxMismatches) {
        FieldMismatch fm; fm.resource_type = type; fm.record_id = ls[li].id;
        fm.field = "(missing in sqlite)"; fm.expected = "[record]"; mismatches.push_back(fm); ++li; }
    while (si < ss.size() && (int)mismatches.size() < kMaxMismatches) {
        FieldMismatch fm; fm.resource_type = type; fm.record_id = ss[si].id;
        fm.field = "(unexpected in sqlite)"; fm.actual = "[record]"; mismatches.push_back(fm); ++si; }
}

// ---- Generic comparison function ----
template<typename T>
static ResourceVerificationResult compare_resource(const std::string& type,
    std::vector<T>&& legacy, std::vector<T>&& sqlite,
    std::function<std::string(const std::vector<T>&)> canon_all,
    std::function<std::string(const T&)> canon_one)
{
    ResourceVerificationResult r; r.resource_type = type;
    std::string lc = canon_all(legacy), sc = canon_all(sqlite);
    r.legacy_record_count = legacy.size(); r.sqlite_record_count = sqlite.size();
    r.legacy_checksum = Verification::sha256(lc); r.sqlite_checksum = Verification::sha256(sc);
    if (r.legacy_record_count != r.sqlite_record_count) {
        r.success = false; r.status = VerificationStatus::Failed; return r; }
    if (r.legacy_checksum != r.sqlite_checksum) {
        collect_mismatches(type, legacy, sqlite, r.mismatches, canon_one);
        r.success = false; r.status = VerificationStatus::Failed; return r; }
    r.success = true; r.status = VerificationStatus::Passed; return r;
}

// ---- Common verify preamble (check import context) ----
// These macros use `rv` for the local ResourceVerificationResult variable.
#define VERIFY_PREAMBLE(type, rv) \
    const ImportResult* ir = find_import_result(type); \
    if (!ir) { ResourceVerificationResult rv; rv.success = false; rv.status = VerificationStatus::Failed; rv.error = "missing_import_result"; rv.resource_type = type; return rv; } \
    if (ir->disposition == ImportDisposition::SkippedEmpty || ir->disposition == ImportDisposition::SkippedMissingOptional) { \
        ResourceVerificationResult rv; rv.success = true; rv.status = VerificationStatus::Skipped; rv.resource_type = type; return rv; \
    }

#define PARSE_U64(tmp) do { if (!LineParser::parse_uint64(f[i], tmp, em)) { rv.success = false; rv.error = "invalid_integer"; return rv; } i++; } while(0)
#define PARSE_BOOL(tmp) do { if (!LineParser::parse_bool(f[i], tmp, em)) { rv.success = false; rv.error = "invalid_boolean"; return rv; } i++; } while(0)
#define PARSE_STR(tmp) do { tmp = f[i]; i++; } while(0)
#define CHECK_FILE(name) \
    std::string p = legacy_dir_ + name; \
    if (p.size() < 5 || !fs::exists(p)) { rv.success = false; rv.error = "file_missing"; return rv; }

// ============================================================
// Canonical implementations
// ============================================================

#define CANON_FIELD_STR(field) Verification::append_field(out, r.field)
#define CANON_FIELD_U64(field) Verification::append_field(out, r.field)
#define CANON_FIELD_BOOL(field) Verification::append_field(out, r.field ? "true" : "false")

#define CANON_ALL(type, cls, fields) \
    std::string Verification::canonical_##type(const std::vector<cls>& records) { \
        std::string out; auto sorted = records; \
        std::sort(sorted.begin(), sorted.end(), [](const cls& a, const cls& b) { return a.id < b.id; }); \
        for (const auto& r : sorted) { fields; } return out; \
    }

#define CANON_ONE(type, cls, fields) \
    static std::string canon_one_##type(const cls& r) { \
        std::string out; fields; return out; \
    }

CANON_ALL(nodes, node::Node, CANON_FIELD_U64(id); CANON_FIELD_STR(name); CANON_FIELD_STR(type))
CANON_ALL(php_versions, php::PhpVersion, CANON_FIELD_U64(id); CANON_FIELD_STR(version); CANON_FIELD_STR(image); CANON_FIELD_BOOL(enabled); CANON_FIELD_BOOL(default_version))
CANON_ALL(profiles, profile::Profile, CANON_FIELD_U64(id); CANON_FIELD_STR(profile_name); Verification::append_field(out, profile::profile_type_to_string(r.type)); CANON_FIELD_STR(web_server); CANON_FIELD_STR(runtime); CANON_FIELD_STR(template_path); CANON_FIELD_STR(description); CANON_FIELD_BOOL(enabled); CANON_FIELD_BOOL(default_profile))
CANON_ALL(users, user::User, CANON_FIELD_U64(id); CANON_FIELD_STR(username); CANON_FIELD_U64(uid); CANON_FIELD_STR(home_directory); CANON_FIELD_STR(shell); CANON_FIELD_BOOL(enabled))
CANON_ALL(sites, site::Site, CANON_FIELD_U64(id); CANON_FIELD_STR(domain); CANON_FIELD_STR(owner); CANON_FIELD_U64(node_id); CANON_FIELD_STR(web_server); CANON_FIELD_BOOL(php_mail_enabled))
CANON_ALL(domains, domain::Domain, CANON_FIELD_U64(id); CANON_FIELD_STR(fqdn); CANON_FIELD_U64(owner_id); CANON_FIELD_U64(site_id); CANON_FIELD_STR(php_version); CANON_FIELD_BOOL(ssl_enabled); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(type); CANON_FIELD_STR(target))
CANON_ALL(databases, database::Database, CANON_FIELD_U64(id); CANON_FIELD_STR(db_name); CANON_FIELD_STR(db_user); CANON_FIELD_STR(db_password); CANON_FIELD_STR(engine); CANON_FIELD_STR(version); CANON_FIELD_U64(owner_id); CANON_FIELD_U64(site_id); CANON_FIELD_BOOL(enabled))
CANON_ALL(backups, backup::Backup, CANON_FIELD_U64(id); CANON_FIELD_U64(site_id); CANON_FIELD_U64(owner_id); CANON_FIELD_STR(filename); CANON_FIELD_STR(type); CANON_FIELD_U64(size); CANON_FIELD_STR(created_at); CANON_FIELD_STR(status); CANON_FIELD_STR(file_path); CANON_FIELD_STR(compression))
CANON_ALL(reverse_proxies, proxy::ReverseProxy, CANON_FIELD_U64(id); CANON_FIELD_STR(domain); CANON_FIELD_U64(site_id); CANON_FIELD_STR(provider); CANON_FIELD_STR(config_path); CANON_FIELD_STR(upstream); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(status))
CANON_ALL(access_users, access::AccessUser, CANON_FIELD_U64(id); CANON_FIELD_STR(username); CANON_FIELD_STR(auth_type); CANON_FIELD_STR(password_hash); CANON_FIELD_BOOL(enabled))
CANON_ALL(access_grants, access::AccessGrant, CANON_FIELD_U64(id); CANON_FIELD_U64(access_user_id); CANON_FIELD_U64(site_id); Verification::append_field(out, access::permission_to_string(r.permission)))
CANON_ALL(auth_users, auth::AuthUser, CANON_FIELD_U64(id); CANON_FIELD_STR(username); CANON_FIELD_STR(password_hash); CANON_FIELD_BOOL(must_change_password); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(role))
CANON_ALL(ssl_certificates, ssl::SslCertificate, CANON_FIELD_U64(id); CANON_FIELD_U64(domain_id); CANON_FIELD_STR(domain); CANON_FIELD_STR(provider); CANON_FIELD_STR(certificate_path); CANON_FIELD_STR(key_path); CANON_FIELD_STR(chain_path); CANON_FIELD_STR(issued_at); CANON_FIELD_STR(expires_at); CANON_FIELD_STR(renew_after); CANON_FIELD_STR(status); CANON_FIELD_BOOL(auto_renew); CANON_FIELD_BOOL(https_enabled); CANON_FIELD_BOOL(redirect_enabled); CANON_FIELD_STR(domains); CANON_FIELD_STR(challenge_type); CANON_FIELD_STR(last_error); CANON_FIELD_STR(last_validation); CANON_FIELD_U64(renew_attempts); CANON_FIELD_U64(version))
CANON_ALL(mail_domains, mail::MailDomain, CANON_FIELD_U64(id); CANON_FIELD_U64(domain_id); CANON_FIELD_U64(site_id); CANON_FIELD_STR(domain_name); Verification::append_field(out, mail::mail_domain_mode_to_string(r.mode)); CANON_FIELD_STR(relay_host); CANON_FIELD_STR(dkim_selector); CANON_FIELD_STR(dkim_private_key_path); CANON_FIELD_STR(dkim_public_key_dns); CANON_FIELD_U64(max_mailboxes); CANON_FIELD_U64(max_aliases); CANON_FIELD_STR(catch_all); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(created_at); CANON_FIELD_STR(updated_at))
CANON_ALL(mail_mailboxes, mail::Mailbox, CANON_FIELD_U64(id); CANON_FIELD_U64(domain_id); CANON_FIELD_STR(local_part); CANON_FIELD_STR(password_hash); CANON_FIELD_U64(quota_bytes); CANON_FIELD_U64(quota_messages); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(display_name); CANON_FIELD_STR(forward_to); CANON_FIELD_BOOL(spam_enabled); CANON_FIELD_STR(last_login); CANON_FIELD_STR(created_at); CANON_FIELD_STR(updated_at))
CANON_ALL(mail_aliases, mail::MailAlias, CANON_FIELD_U64(id); CANON_FIELD_U64(domain_id); CANON_FIELD_STR(source_local_part); CANON_FIELD_STR(destination); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(created_at); CANON_FIELD_STR(updated_at))

CANON_ONE(nodes, node::Node, CANON_FIELD_U64(id); CANON_FIELD_STR(name); CANON_FIELD_STR(type))
CANON_ONE(php_versions, php::PhpVersion, CANON_FIELD_U64(id); CANON_FIELD_STR(version); CANON_FIELD_STR(image); CANON_FIELD_BOOL(enabled); CANON_FIELD_BOOL(default_version))
CANON_ONE(profiles, profile::Profile, CANON_FIELD_U64(id); CANON_FIELD_STR(profile_name); Verification::append_field(out, profile::profile_type_to_string(r.type)); CANON_FIELD_STR(web_server); CANON_FIELD_STR(runtime); CANON_FIELD_STR(template_path); CANON_FIELD_STR(description); CANON_FIELD_BOOL(enabled); CANON_FIELD_BOOL(default_profile))
CANON_ONE(users, user::User, CANON_FIELD_U64(id); CANON_FIELD_STR(username); CANON_FIELD_U64(uid); CANON_FIELD_STR(home_directory); CANON_FIELD_STR(shell); CANON_FIELD_BOOL(enabled))
CANON_ONE(sites, site::Site, CANON_FIELD_U64(id); CANON_FIELD_STR(domain); CANON_FIELD_STR(owner); CANON_FIELD_U64(node_id); CANON_FIELD_STR(web_server); CANON_FIELD_BOOL(php_mail_enabled))
CANON_ONE(domains, domain::Domain, CANON_FIELD_U64(id); CANON_FIELD_STR(fqdn); CANON_FIELD_U64(owner_id); CANON_FIELD_U64(site_id); CANON_FIELD_STR(php_version); CANON_FIELD_BOOL(ssl_enabled); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(type); CANON_FIELD_STR(target))
CANON_ONE(databases, database::Database, CANON_FIELD_U64(id); CANON_FIELD_STR(db_name); CANON_FIELD_STR(db_user); CANON_FIELD_STR(db_password); CANON_FIELD_STR(engine); CANON_FIELD_STR(version); CANON_FIELD_U64(owner_id); CANON_FIELD_U64(site_id); CANON_FIELD_BOOL(enabled))
CANON_ONE(backups, backup::Backup, CANON_FIELD_U64(id); CANON_FIELD_U64(site_id); CANON_FIELD_U64(owner_id); CANON_FIELD_STR(filename); CANON_FIELD_STR(type); CANON_FIELD_U64(size); CANON_FIELD_STR(created_at); CANON_FIELD_STR(status); CANON_FIELD_STR(file_path); CANON_FIELD_STR(compression))
CANON_ONE(reverse_proxies, proxy::ReverseProxy, CANON_FIELD_U64(id); CANON_FIELD_STR(domain); CANON_FIELD_U64(site_id); CANON_FIELD_STR(provider); CANON_FIELD_STR(config_path); CANON_FIELD_STR(upstream); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(status))
CANON_ONE(access_users, access::AccessUser, CANON_FIELD_U64(id); CANON_FIELD_STR(username); CANON_FIELD_STR(auth_type); CANON_FIELD_STR(password_hash); CANON_FIELD_BOOL(enabled))
CANON_ONE(access_grants, access::AccessGrant, CANON_FIELD_U64(id); CANON_FIELD_U64(access_user_id); CANON_FIELD_U64(site_id); Verification::append_field(out, access::permission_to_string(r.permission)))
CANON_ONE(auth_users, auth::AuthUser, CANON_FIELD_U64(id); CANON_FIELD_STR(username); CANON_FIELD_STR(password_hash); CANON_FIELD_BOOL(must_change_password); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(role))
CANON_ONE(ssl_certificates, ssl::SslCertificate, CANON_FIELD_U64(id); CANON_FIELD_U64(domain_id); CANON_FIELD_STR(domain); CANON_FIELD_STR(provider); CANON_FIELD_STR(certificate_path); CANON_FIELD_STR(key_path); CANON_FIELD_STR(chain_path); CANON_FIELD_STR(issued_at); CANON_FIELD_STR(expires_at); CANON_FIELD_STR(renew_after); CANON_FIELD_STR(status); CANON_FIELD_BOOL(auto_renew); CANON_FIELD_BOOL(https_enabled); CANON_FIELD_BOOL(redirect_enabled); CANON_FIELD_STR(domains); CANON_FIELD_STR(challenge_type); CANON_FIELD_STR(last_error); CANON_FIELD_STR(last_validation); CANON_FIELD_U64(renew_attempts); CANON_FIELD_U64(version))
CANON_ONE(mail_domains, mail::MailDomain, CANON_FIELD_U64(id); CANON_FIELD_U64(domain_id); CANON_FIELD_U64(site_id); CANON_FIELD_STR(domain_name); Verification::append_field(out, mail::mail_domain_mode_to_string(r.mode)); CANON_FIELD_STR(relay_host); CANON_FIELD_STR(dkim_selector); CANON_FIELD_STR(dkim_private_key_path); CANON_FIELD_STR(dkim_public_key_dns); CANON_FIELD_U64(max_mailboxes); CANON_FIELD_U64(max_aliases); CANON_FIELD_STR(catch_all); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(created_at); CANON_FIELD_STR(updated_at))
CANON_ONE(mail_mailboxes, mail::Mailbox, CANON_FIELD_U64(id); CANON_FIELD_U64(domain_id); CANON_FIELD_STR(local_part); CANON_FIELD_STR(password_hash); CANON_FIELD_U64(quota_bytes); CANON_FIELD_U64(quota_messages); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(display_name); CANON_FIELD_STR(forward_to); CANON_FIELD_BOOL(spam_enabled); CANON_FIELD_STR(last_login); CANON_FIELD_STR(created_at); CANON_FIELD_STR(updated_at))
CANON_ONE(mail_aliases, mail::MailAlias, CANON_FIELD_U64(id); CANON_FIELD_U64(domain_id); CANON_FIELD_STR(source_local_part); CANON_FIELD_STR(destination); CANON_FIELD_BOOL(enabled); CANON_FIELD_STR(created_at); CANON_FIELD_STR(updated_at))

std::string Verification::canonical_mail_config(const std::string& ms, const std::string& sh) {
    std::string out;
    append_field(out, std::string("module_state")); append_field(out, ms);
    append_field(out, std::string("smarthost")); append_field(out, sh);
    return out;
}

// ============================================================
// SQLite load helpers
// ============================================================

// Row readers
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
    d.id = static_cast<uint64_t>(db.column_int(0)); d.fqdn = db.column_text(1);
    d.owner_id = static_cast<uint64_t>(db.column_int(2)); d.site_id = static_cast<uint64_t>(db.column_int(3));
    d.php_version = db.column_text(4); d.ssl_enabled = (db.column_int(5) != 0); d.enabled = (db.column_int(6) != 0);
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
    u.id = static_cast<uint64_t>(db.column_int(0)); u.username = db.column_text(1);
    u.auth_type = db.column_text(2); u.password_hash = db.column_text(3);
    u.enabled = (db.column_int(4) != 0); u.name = u.username; return true; }
static bool rd_ag(SQLiteDB& db, access::AccessGrant& g) {
    g.id = static_cast<uint64_t>(db.column_int(0)); g.access_user_id = static_cast<uint64_t>(db.column_int(1));
    g.site_id = static_cast<uint64_t>(db.column_int(2));
    g.permission = access::permission_from_string(db.column_text(3));
    g.name = std::to_string(g.access_user_id) + "-" + std::to_string(g.site_id); return true; }
static bool rd_authu(SQLiteDB& db, auth::AuthUser& u) {
    u.id = static_cast<uint64_t>(db.column_int(0)); u.username = db.column_text(1);
    u.password_hash = db.column_text(2); u.must_change_password = (db.column_int(3) != 0);
    u.enabled = (db.column_int(4) != 0); u.role = db.column_text(5); u.name = u.username; return true; }
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

// ---- Macro: generate a verify_X method for a simple resource ----
// Type name, class, SQL, reader, expected field count (for legacy parsing)
#define VERIFY_SIMPLE(type, cls, sql, reader, fields_expr) \
    ResourceVerificationResult Verification::verify_##type() { \
        ResourceVerificationResult rv; \
        VERIFY_PREAMBLE(#type, rv) \
        std::vector<cls> legacy_records; \
        { CHECK_FILE(#type ".db") \
          std::set<uint64_t> ids; LineParser lp(fs::path(p), #type ".db"); std::string em; \
          while (lp.next()) { if (lp.empty_line()) continue; auto f = lp.split(); int i = 0; \
            if (f.size() != fields_expr) { rv.success = false; rv.error = "invalid_field_count"; rv.resource_type = #type; return rv; } \
            cls rec; \
            { uint64_t tmp; PARSE_U64(tmp); rec.id = tmp; } \
            if (ids.count(rec.id)) { rv.success = false; rv.error = "duplicate_id"; rv.resource_type = #type; return rv; } \
            ids.insert(rec.id); \
            fields_##type(rec, f, i, em); \
            legacy_records.push_back(std::move(rec)); \
          } \
        } \
        std::vector<cls> sqlite_records; \
        { auto cr = checked_query<cls>(pool_, sql, reader); \
          if (!cr.success) { rv.success = false; rv.error = "sqlite_load_failed"; rv.resource_type = #type; return rv; } \
          sqlite_records = std::move(cr.records); } \
        return compare_resource<cls>(#type, std::move(legacy_records), std::move(sqlite_records), \
            [this](const std::vector<cls>& v) { return canonical_##type(v); }, canon_one_##type); \
    }

// Field parsers per resource type
#define fields_nodes(r, f, i, em) \
    r.name = f[i++]; r.type = f[i++];
#define fields_php_versions(r, f, i, em) \
    r.version = f[i++]; r.image = f[i++]; PARSE_BOOL(r.enabled); PARSE_BOOL(r.default_version); r.name = r.version;
#define fields_profiles(r, f, i, em) \
    r.profile_name = f[i++]; r.type = profile::profile_type_from_string(f[i++]); \
    r.web_server = f[i++]; r.runtime = f[i++]; r.template_path = f[i++]; r.description = f[i++]; \
    PARSE_BOOL(r.enabled); PARSE_BOOL(r.default_profile); r.name = r.profile_name;
#define fields_users(r, f, i, em) \
    r.username = f[i++]; { uint64_t tmp; PARSE_U64(tmp); r.uid = tmp; } \
    r.home_directory = f[i++]; r.shell = f[i++]; PARSE_BOOL(r.enabled); r.name = r.username;
#define fields_sites(r, f, i, em) /* custom — see verify_sites */ (void)em
#define fields_domains(r, f, i, em) \
    r.fqdn = f[i++]; { uint64_t tmp; PARSE_U64(tmp); r.owner_id = tmp; } \
    { uint64_t tmp; PARSE_U64(tmp); r.site_id = tmp; } \
    r.php_version = f[i++]; PARSE_BOOL(r.ssl_enabled); PARSE_BOOL(r.enabled); \
    r.type = (f.size() > i) ? f[i++] : std::string("primary"); \
    r.target = (f.size() > i) ? f[i++] : std::string(); r.name = r.fqdn;
#define fields_databases(r, f, i, em) \
    r.db_name = f[i++]; r.db_user = f[i++]; r.db_password = f[i++]; r.engine = f[i++]; r.version = f[i++]; \
    { uint64_t tmp; PARSE_U64(tmp); r.owner_id = tmp; } \
    { uint64_t tmp; PARSE_U64(tmp); r.site_id = tmp; } \
    PARSE_BOOL(r.enabled); r.name = r.db_name;
#define fields_backups(r, f, i, em) \
    { uint64_t tmp; PARSE_U64(tmp); r.site_id = tmp; } { uint64_t tmp; PARSE_U64(tmp); r.owner_id = tmp; } \
    r.filename = f[i++]; r.type = f[i++]; { uint64_t tmp; PARSE_U64(tmp); r.size = tmp; } \
    r.created_at = f[i++]; r.status = f[i++]; r.file_path = f[i++]; r.compression = f[i++]; r.name = r.filename;
#define fields_reverse_proxies(r, f, i, em) \
    r.domain = f[i++]; { uint64_t tmp; PARSE_U64(tmp); r.site_id = tmp; } \
    r.provider = f[i++]; r.config_path = f[i++]; r.upstream = f[i++]; PARSE_BOOL(r.enabled); r.status = f[i++]; r.name = r.domain;
#define fields_access_users(r, f, i, em) \
    r.username = f[i++]; r.auth_type = f[i++]; r.password_hash = f[i++]; PARSE_BOOL(r.enabled); r.name = r.username;
#define fields_access_grants(r, f, i, em) \
    { uint64_t tmp; PARSE_U64(tmp); r.access_user_id = tmp; } { uint64_t tmp; PARSE_U64(tmp); r.site_id = tmp; } \
    r.permission = access::permission_from_string(f[i++]); \
    r.name = std::to_string(r.access_user_id) + "-" + std::to_string(r.site_id);
#define fields_auth_users(r, f, i, em) \
    r.username = f[i++]; r.password_hash = f[i++]; PARSE_BOOL(r.must_change_password); PARSE_BOOL(r.enabled); r.role = f[i++]; r.name = r.username;
#define fields_mail_mailboxes(r, f, i, em) \
    { uint64_t tmp; PARSE_U64(tmp); r.domain_id = tmp; } r.local_part = f[i++]; r.password_hash = f[i++]; \
    { uint64_t tmp; PARSE_U64(tmp); r.quota_bytes = tmp; } { uint64_t tmp; PARSE_U64(tmp); r.quota_messages = tmp; } \
    PARSE_BOOL(r.enabled); r.display_name = f[i++]; r.forward_to = f[i++]; PARSE_BOOL(r.spam_enabled); \
    r.last_login = f[i++]; r.created_at = f[i++]; r.updated_at = f[i++]; r.name = r.local_part;
#define fields_mail_aliases(r, f, i, em) \
    { uint64_t tmp; PARSE_U64(tmp); r.domain_id = tmp; } r.source_local_part = f[i++]; r.destination = f[i++]; \
    PARSE_BOOL(r.enabled); r.created_at = f[i++]; r.updated_at = f[i++]; r.name = r.source_local_part;

// Generate verify methods for simple (fixed-format) resources
VERIFY_SIMPLE(nodes, node::Node, "SELECT id, name, type FROM nodes ORDER BY id", rd_node, 3)
VERIFY_SIMPLE(php_versions, php::PhpVersion, "SELECT id, version, image, enabled, default_version FROM php_versions ORDER BY id", rd_php, 5)
VERIFY_SIMPLE(users, user::User, "SELECT id, username, uid, home_directory, shell, enabled FROM users ORDER BY id", rd_user, 6)
VERIFY_SIMPLE(domains, domain::Domain, "SELECT id, fqdn, owner_id, site_id, php_version, ssl_enabled, enabled, type, target FROM domains ORDER BY id", rd_domain, 9)
VERIFY_SIMPLE(databases, database::Database, "SELECT id, db_name, db_user, db_password, engine, version, owner_id, site_id, enabled FROM databases ORDER BY id", rd_db, 9)
VERIFY_SIMPLE(backups, backup::Backup, "SELECT id, site_id, owner_id, filename, type, size, created_at, status, file_path, compression FROM backups ORDER BY id", rd_backup, 10)
VERIFY_SIMPLE(reverse_proxies, proxy::ReverseProxy, "SELECT id, domain, site_id, provider, config_path, upstream, enabled, status FROM reverse_proxies ORDER BY id", rd_proxy, 8)
VERIFY_SIMPLE(access_users, access::AccessUser, "SELECT id, username, auth_type, password_hash, enabled FROM access_users ORDER BY id", rd_au, 5)
VERIFY_SIMPLE(access_grants, access::AccessGrant, "SELECT id, access_user_id, site_id, permission FROM access_grants ORDER BY id", rd_ag, 4)
VERIFY_SIMPLE(auth_users, auth::AuthUser, "SELECT id, username, password_hash, must_change_password, enabled, role FROM auth_users ORDER BY id", rd_authu, 6)
VERIFY_SIMPLE(mail_mailboxes, mail::Mailbox, "SELECT id, domain_id, local_part, password_hash, quota_bytes, quota_messages, enabled, display_name, forward_to, spam_enabled, last_login, created_at, updated_at FROM mail_mailboxes ORDER BY id", rd_mb, 13)
VERIFY_SIMPLE(mail_aliases, mail::MailAlias, "SELECT id, domain_id, source_local_part, destination, enabled, created_at, updated_at FROM mail_aliases ORDER BY id", rd_ma, 7)

// ---- Custom verify methods ----

// Sites: 5-field (legacy) or 6-field (current) format detection
ResourceVerificationResult Verification::verify_sites() {
    ResourceVerificationResult rv;
    VERIFY_PREAMBLE("sites", rv)
    std::vector<site::Site> legacy_records;
    { CHECK_FILE("sites.db")
      std::set<uint64_t> ids; std::set<std::string> domains;
      LineParser lp(fs::path(p), "sites.db"); std::string em;
      while (lp.next()) {
        if (lp.empty_line()) continue;
        int pipes = lp.count_pipes(); auto f = lp.split();
        if (pipes >= 5) {
            if (f.size() != 6) { rv.success = false; rv.error = "invalid_field_count"; rv.resource_type = "sites"; return rv; }
            site::Site s; int i = 0; { uint64_t tmp; PARSE_U64(tmp); s.id = tmp; }
            if (ids.count(s.id)) { rv.success = false; rv.error = "duplicate_id"; rv.resource_type = "sites"; return rv; } ids.insert(s.id);
            s.domain = f[i++]; if (domains.count(s.domain)) { rv.success = false; rv.error = "duplicate_domain"; rv.resource_type = "sites"; return rv; } domains.insert(s.domain);
            s.owner = f[i++]; { uint64_t tmp; PARSE_U64(tmp); s.node_id = tmp; } s.web_server = f[i++].empty() ? "apache" : f[i-1];
            PARSE_BOOL(s.php_mail_enabled); s.php_mail_enabled_present = true; s.name = s.domain; legacy_records.push_back(std::move(s));
        } else {
            if (f.size() != 5) { rv.success = false; rv.error = "invalid_field_count"; rv.resource_type = "sites"; return rv; }
            site::Site s; int i = 0; { uint64_t tmp; PARSE_U64(tmp); s.id = tmp; }
            if (ids.count(s.id)) { rv.success = false; rv.error = "duplicate_id"; rv.resource_type = "sites"; return rv; } ids.insert(s.id);
            s.domain = f[i++]; if (domains.count(s.domain)) { rv.success = false; rv.error = "duplicate_domain"; rv.resource_type = "sites"; return rv; } domains.insert(s.domain);
            s.owner = f[i++]; { uint64_t tmp; PARSE_U64(tmp); s.node_id = tmp; } s.web_server = f[i++].empty() ? "apache" : f[i-1];
            s.php_mail_enabled = false; s.php_mail_enabled_present = false; s.name = s.domain; legacy_records.push_back(std::move(s));
        }
      }
    }
    std::vector<site::Site> sqlite_records;
    { auto cr = checked_query<site::Site>(pool_, "SELECT id, domain, owner, node_id, web_server, php_mail_enabled FROM sites ORDER BY id", rd_site);
      if (!cr.success) { rv.success = false; rv.error = "sqlite_load_failed"; rv.resource_type = "sites"; return rv; }
      sqlite_records = std::move(cr.records); }
    return compare_resource<site::Site>("sites", std::move(legacy_records), std::move(sqlite_records),
        [this](const std::vector<site::Site>& v) { return canonical_sites(v); }, canon_one_sites);
}

// Profiles: combined from profiles.db + template_profiles.db
ResourceVerificationResult Verification::verify_profiles() {
    ResourceVerificationResult rv;
    const ImportResult* ir = find_import_result("profiles");
    if (!ir) { rv.success = false; rv.status = VerificationStatus::Failed; rv.error = "missing_import_result"; rv.resource_type = "profiles"; return rv; }
    if (ir->disposition == ImportDisposition::SkippedEmpty || ir->disposition == ImportDisposition::SkippedMissingOptional) {
        rv.success = true; rv.status = VerificationStatus::Skipped; rv.resource_type = "profiles"; return rv; }

    std::vector<profile::Profile> legacy_records;
    std::set<uint64_t> ids; std::set<std::string> names; std::string em;

    // Parse profiles.db (9-field)
    { CHECK_FILE("profiles.db")
      LineParser lp(fs::path(p), "profiles.db");
      while (lp.next()) {
        if (lp.empty_line()) continue; auto f = lp.split(); int i = 0;
        if (f.size() != 9) { rv.success = false; rv.error = "invalid_field_count"; rv.resource_type = "profiles"; return rv; }
        profile::Profile rec; { uint64_t tmp; PARSE_U64(tmp); rec.id = tmp; }
        if (ids.count(rec.id)) { rv.success = false; rv.error = "duplicate_id"; rv.resource_type = "profiles"; return rv; } ids.insert(rec.id);
        rec.profile_name = f[i++];
        if (names.count(rec.profile_name)) { rv.success = false; rv.error = "duplicate_profile_name"; rv.resource_type = "profiles"; return rv; } names.insert(rec.profile_name);
        rec.type = profile::profile_type_from_string(f[i++]); rec.web_server = f[i++]; rec.runtime = f[i++];
        rec.template_path = f[i++]; rec.description = f[i++]; PARSE_BOOL(rec.enabled); PARSE_BOOL(rec.default_profile);
        rec.name = rec.profile_name; legacy_records.push_back(std::move(rec));
      }
    }

    // Parse template_profiles.db (8-field, optional)
    std::string tpl_path = legacy_dir_ + "template_profiles.db";
    if (fs::exists(tpl_path) && fs::file_size(tpl_path) > 0) {
      LineParser lp(fs::path(tpl_path), "template_profiles.db");
      while (lp.next()) {
        if (lp.empty_line()) continue; auto f = lp.split(); int i = 0;
        if (f.size() != 8) { rv.success = false; rv.error = "invalid_field_count"; rv.resource_type = "profiles"; return rv; }
        profile::Profile rec; { uint64_t tmp; PARSE_U64(tmp); rec.id = tmp; }
        if (ids.count(rec.id)) { rv.success = false; rv.error = "duplicate_id"; rv.resource_type = "profiles"; return rv; } ids.insert(rec.id);
        rec.profile_name = f[i++];
        if (names.count(rec.profile_name)) { rv.success = false; rv.error = "duplicate_profile_name"; rv.resource_type = "profiles"; return rv; } names.insert(rec.profile_name);
        rec.type = profile::ProfileType::WEB_SERVER; rec.web_server = f[i++]; rec.runtime = f[i++];
        rec.template_path = f[i++]; rec.description = f[i++]; PARSE_BOOL(rec.enabled); PARSE_BOOL(rec.default_profile);
        rec.name = rec.profile_name; legacy_records.push_back(std::move(rec));
      }
    }

    std::vector<profile::Profile> sqlite_records;
    { auto cr = checked_query<profile::Profile>(pool_, "SELECT id, profile_name, type, web_server, runtime, template_path, description, enabled, default_profile FROM profiles ORDER BY id", rd_profile);
      if (!cr.success) { rv.success = false; rv.error = "sqlite_load_failed"; rv.resource_type = "profiles"; return rv; }
      sqlite_records = std::move(cr.records); }
    return compare_resource<profile::Profile>("profiles", std::move(legacy_records), std::move(sqlite_records),
        [this](const std::vector<profile::Profile>& v) { return canonical_profiles(v); }, canon_one_profiles);
}

// SSL certificates: 20-field current or legacy format
ResourceVerificationResult Verification::verify_ssl_certificates() {
    ResourceVerificationResult rv;
    VERIFY_PREAMBLE("ssl_certificates", rv)
    std::vector<ssl::SslCertificate> legacy_records;
    { CHECK_FILE("ssl_certificates.db")
      std::set<uint64_t> ids; LineParser lp(fs::path(p), "ssl_certificates.db"); std::string em;
      while (lp.next()) {
        if (lp.empty_line()) continue; auto f = lp.split(); int i = 0;
        if (f.size() < 6) { rv.success = false; rv.error = "invalid_field_count"; rv.resource_type = "ssl_certificates"; return rv; }
        ssl::SslCertificate c; { uint64_t tmp; PARSE_U64(tmp); c.id = tmp; }
        if (ids.count(c.id)) { rv.success = false; rv.error = "duplicate_id"; rv.resource_type = "ssl_certificates"; return rv; } ids.insert(c.id);
        { uint64_t tmp; PARSE_U64(tmp); c.domain_id = tmp; }
        c.domain = f[i++]; c.provider = f[i++]; c.certificate_path = f[i++]; c.key_path = f[i++];
        if (f.size() >= 20) {
            if (f.size() > i) c.chain_path = f[i++];
            if (f.size() > i) c.issued_at = f[i++];
            if (f.size() > i) c.expires_at = f[i++];
            if (f.size() > i) c.renew_after = f[i++];
            if (f.size() > i) c.status = f[i++];
            if (f.size() > i) { if (!LineParser::parse_bool(f[i++], c.auto_renew, em)) { rv.success = false; rv.error = "invalid_boolean"; rv.resource_type = "ssl_certificates"; return rv; } }
            if (f.size() > i) { if (!LineParser::parse_bool(f[i++], c.https_enabled, em)) { rv.success = false; rv.error = "invalid_boolean"; rv.resource_type = "ssl_certificates"; return rv; } }
            if (f.size() > i) { if (!LineParser::parse_bool(f[i++], c.redirect_enabled, em)) { rv.success = false; rv.error = "invalid_boolean"; rv.resource_type = "ssl_certificates"; return rv; } }
            if (f.size() > i) c.domains = f[i++];
            if (f.size() > i) c.challenge_type = f[i++];
            if (f.size() > i) c.last_error = f[i++];
            if (f.size() > i) c.last_validation = f[i++];
            if (f.size() > i) { if (!LineParser::parse_int(f[i++], c.renew_attempts, em)) { rv.success = false; rv.error = "invalid_integer"; rv.resource_type = "ssl_certificates"; return rv; } }
            if (f.size() > i) { if (!LineParser::parse_int(f[i++], c.version, em)) { rv.success = false; rv.error = "invalid_integer"; rv.resource_type = "ssl_certificates"; return rv; } }
        } else {
            if (f.size() > i) c.expires_at = f[i++];
            if (f.size() > i) c.status = f[i++];
            if (f.size() > i) { if (!LineParser::parse_bool(f[i++], c.https_enabled, em)) { rv.success = false; rv.error = "invalid_boolean"; rv.resource_type = "ssl_certificates"; return rv; } }
            if (f.size() > i) { if (!LineParser::parse_bool(f[i++], c.auto_renew, em)) { rv.success = false; rv.error = "invalid_boolean"; rv.resource_type = "ssl_certificates"; return rv; } }
            c.version = 0;
        }
        c.name = c.domain; legacy_records.push_back(std::move(c));
      }
    }
    std::vector<ssl::SslCertificate> sqlite_records;
    { auto cr = checked_query<ssl::SslCertificate>(pool_,
        "SELECT id, domain_id, domain, provider, certificate_path, key_path, chain_path, "
        "issued_at, expires_at, renew_after, status, auto_renew, https_enabled, redirect_enabled, "
        "domains, challenge_type, last_error, last_validation, renew_attempts, version "
        "FROM ssl_certificates ORDER BY id", rd_ssl);
      if (!cr.success) { rv.success = false; rv.error = "sqlite_load_failed"; rv.resource_type = "ssl_certificates"; return rv; }
      sqlite_records = std::move(cr.records); }
    return compare_resource<ssl::SslCertificate>("ssl_certificates", std::move(legacy_records), std::move(sqlite_records),
        [this](const std::vector<ssl::SslCertificate>& v) { return canonical_ssl_certificates(v); }, canon_one_ssl_certificates);
}

// Mail domains: 10-field legacy or 12-field current
ResourceVerificationResult Verification::verify_mail_domains() {
    ResourceVerificationResult rv;
    VERIFY_PREAMBLE("mail_domains", rv)
    std::vector<mail::MailDomain> legacy_records;
    { CHECK_FILE("mail_domains.db")
      std::set<uint64_t> ids; LineParser lp(fs::path(p), "mail_domains.db"); std::string em;
      while (lp.next()) {
        if (lp.empty_line()) continue; auto f = lp.split(); int i = 0;
        if (f.size() < 10) { rv.success = false; rv.error = "invalid_field_count"; rv.resource_type = "mail_domains"; return rv; }
        mail::MailDomain m; { uint64_t tmp; PARSE_U64(tmp); m.id = tmp; }
        if (ids.count(m.id)) { rv.success = false; rv.error = "duplicate_id"; rv.resource_type = "mail_domains"; return rv; } ids.insert(m.id);
        m.mode = mail::mail_domain_mode_from_string(f[i++]); m.domain_name = f[i++];
        int pipes = lp.count_pipes();
        if (pipes <= 9) {
            if (!LineParser::parse_uint64(f[i++], m.domain_id, em)) { /* sentinel 0 */ }
            if (f.size() > i && !LineParser::parse_bool(f[i++], m.enabled, em)) { rv.success = false; rv.error = "invalid_boolean"; rv.resource_type = "mail_domains"; return rv; }
            if (f.size() > i) m.catch_all = f[i++];
            if (f.size() > i) m.dkim_selector = f[i++];
            if (f.size() > i) m.relay_host = f[i++];
            if (f.size() > i) { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, em)) { rv.success = false; rv.error = "invalid_integer"; rv.resource_type = "mail_domains"; return rv; } m.max_mailboxes = tmp; }
            if (f.size() > i) { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, em)) { rv.success = false; rv.error = "invalid_integer"; rv.resource_type = "mail_domains"; return rv; } m.max_aliases = tmp; }
        } else {
            { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, em)) { rv.success = false; rv.error = "invalid_integer"; rv.resource_type = "mail_domains"; return rv; } m.domain_id = tmp; }
            if (f.size() > i) { /* site_id — sentinel 0 */ uint64_t tmp; LineParser::parse_uint64(f[i++], tmp, em); }
            if (f.size() > i && !LineParser::parse_bool(f[i++], m.enabled, em)) { rv.success = false; rv.error = "invalid_boolean"; rv.resource_type = "mail_domains"; return rv; }
            if (f.size() > i) m.catch_all = f[i++];
            if (f.size() > i) m.dkim_selector = f[i++];
            if (f.size() > i) m.dkim_public_key_dns = f[i++];
            if (f.size() > i) m.relay_host = f[i++];
            if (f.size() > i) { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, em)) { rv.success = false; rv.error = "invalid_integer"; rv.resource_type = "mail_domains"; return rv; } m.max_mailboxes = tmp; }
            if (f.size() > i) { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, em)) { rv.success = false; rv.error = "invalid_integer"; rv.resource_type = "mail_domains"; return rv; } m.max_aliases = tmp; }
        }
        m.name = m.domain_name; legacy_records.push_back(std::move(m));
      }
    }
    std::vector<mail::MailDomain> sqlite_records;
    { auto cr = checked_query<mail::MailDomain>(pool_, "SELECT id, domain_id, site_id, domain_name, mode, relay_host, dkim_selector, dkim_private_key_path, dkim_public_key_dns, max_mailboxes, max_aliases, catch_all, enabled, created_at, updated_at FROM mail_domains ORDER BY id", rd_md);
      if (!cr.success) { rv.success = false; rv.error = "sqlite_load_failed"; rv.resource_type = "mail_domains"; return rv; }
      sqlite_records = std::move(cr.records); }
    return compare_resource<mail::MailDomain>("mail_domains", std::move(legacy_records), std::move(sqlite_records),
        [this](const std::vector<mail::MailDomain>& v) { return canonical_mail_domains(v); }, canon_one_mail_domains);
}

// Mail config: two key-value pairs
ResourceVerificationResult Verification::verify_mail_config() {
    const ImportResult* ir = find_import_result("mail_config");
    if (!ir) { ResourceVerificationResult r; r.success = false; r.status = VerificationStatus::Failed; r.error = "missing_import_result"; r.resource_type = "mail_config"; return r; }
    if (ir->disposition == ImportDisposition::SkippedEmpty || ir->disposition == ImportDisposition::SkippedMissingOptional) {
        ResourceVerificationResult r; r.success = true; r.status = VerificationStatus::Skipped; r.resource_type = "mail_config"; return r; }

    std::string legacy_ms, legacy_sh;
    std::string state_path = legacy_dir_ + "mail_state.db";
    if (fs::exists(state_path)) { std::ifstream f(state_path); std::getline(f, legacy_ms); }
    std::string smtp_path = legacy_dir_ + "mail_smarthost.db";
    if (fs::exists(smtp_path)) { std::ifstream f(smtp_path); std::getline(f, legacy_sh); }

    std::string sqlite_ms, sqlite_sh;
    {
        ReadLease rl(pool_);
        if (!rl.is_valid()) { ResourceVerificationResult r; r.success = false; r.error = "sqlite_load_failed"; r.resource_type = "mail_config"; return r; }
        if (!rl->prepare("SELECT value FROM mail_config WHERE key = 'module_state'")) {
            ResourceVerificationResult r; r.success = false; r.error = "query_failed"; r.resource_type = "mail_config"; return r; }
        if (rl->step()) sqlite_ms = rl->column_text(0);
        if (!rl->prepare("SELECT value FROM mail_config WHERE key = 'smarthost'")) {
            ResourceVerificationResult r; r.success = false; r.error = "query_failed"; r.resource_type = "mail_config"; return r; }
        if (rl->step()) sqlite_sh = rl->column_text(0);
    }

    ResourceVerificationResult r; r.resource_type = "mail_config";
    r.legacy_record_count = (legacy_ms.empty() ? 0 : 1) + (legacy_sh.empty() ? 0 : 1);
    r.sqlite_record_count = (sqlite_ms.empty() ? 0 : 1) + (sqlite_sh.empty() ? 0 : 1);

    std::string lc = canonical_mail_config(legacy_ms, legacy_sh);
    std::string sc = canonical_mail_config(sqlite_ms, sqlite_sh);
    r.legacy_checksum = sha256(lc); r.sqlite_checksum = sha256(sc);

    if (r.legacy_record_count != r.sqlite_record_count || r.legacy_checksum != r.sqlite_checksum) {
        r.success = false; r.status = VerificationStatus::Failed; return r; }
    r.success = true; r.status = VerificationStatus::Passed; return r;
}

// ============================================================
// verify_all — full pipeline
// ============================================================

DatabaseVerificationResult Verification::verify_all() {
    DatabaseVerificationResult result;

    auto add = [&](ResourceVerificationResult r) { result.resources.push_back(std::move(r)); };
    add(verify_nodes());
    add(verify_php_versions());
    add(verify_profiles());
    add(verify_users());
    add(verify_sites());
    add(verify_domains());
    add(verify_databases());
    add(verify_backups());
    add(verify_reverse_proxies());
    add(verify_access_users());
    add(verify_access_grants());
    add(verify_auth_users());
    add(verify_ssl_certificates());
    add(verify_mail_domains());
    add(verify_mail_mailboxes());
    add(verify_mail_aliases());
    add(verify_mail_config());

    // FK check
    {
        ReadLease rl(pool_);
        if (rl.is_valid() && rl->prepare("PRAGMA foreign_key_check")) {
            while (rl->step()) {
                result.foreign_key_violations.push_back(
                    std::string("table=") + rl->column_text(0) + " rowid=" + std::to_string(rl->column_int(1)));
            }
        }
    }

    // Integrity check
    {
        ReadLease rl(pool_);
        if (rl.is_valid() && rl->prepare("PRAGMA integrity_check")) {
            if (rl->step()) result.integrity_check_result = rl->column_text(0);
        }
    }

    result.initial_verification_passed = true;
    for (const auto& res : result.resources) {
        if (!res.success) { result.initial_verification_passed = false; result.error = "resource_verification_failed"; break; }
    }

    if (!result.initial_verification_passed ||
        (!result.foreign_key_violations.empty()) ||
        result.integrity_check_result != "ok") {
        result.success = false;
        return result;
    }

    // Production reopen
    pool_.shutdown();

    {
        Storage storage(sqlite_path_, StorageOptions{CoreStorageBackend::SqlitePhase5});
        if (!storage.sqlite_ready()) {
            result.reopen_succeeded = false; result.error = "reopen_failed";
            result.success = false; return result;
        }
        // Reload runtime SQLite-backed resources through normal Storage
        auto nodes = storage.load_nodes();
        auto php = storage.load_php_versions();
        auto profiles = storage.load_profiles();

        size_t expected_nodes = result.resources[0].legacy_record_count;
        size_t expected_profiles = result.resources[2].legacy_record_count;

        if (nodes.size() != expected_nodes || profiles.size() != expected_profiles) {
            result.reopened_verification_passed = false;
            result.error = "reopened_count_mismatch";
            result.success = false; return result;
        }
        result.reopen_succeeded = true;
        result.reopened_verification_passed = true;
    }

    result.success = true;
    return result;
}

} // namespace containercp::storage
