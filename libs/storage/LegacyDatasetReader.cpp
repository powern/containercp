#include "LegacyDatasetReader.h"
#include "LineParser.h"
#include "profile/ProfileType.h"
#include "mail/MailModuleState.h"

#include <filesystem>
#include <fstream>
#include <set>

namespace containercp::storage {
namespace fs = std::filesystem;
using parser::LineParser;

LegacyDatasetReader::LegacyDatasetReader(const std::string& legacy_directory)
    : legacy_dir_(legacy_directory)
{
    if (!legacy_dir_.empty() && legacy_dir_.back() != '/') legacy_dir_ += '/';
}

LegacyDatasetReader::FileInfo LegacyDatasetReader::check_file(const std::string& filename) const {
    FileInfo info;
    fs::path p(legacy_dir_ + filename);
    if (!fs::exists(p)) return info;
    info.exists = true;
    info.empty = (fs::file_size(p) == 0);
    return info;
}

// ---- read_nodes ----
DatasetResult<node::Node> LegacyDatasetReader::read_nodes() {
    DatasetResult<node::Node> r;
    fs::path p(legacy_dir_ + "nodes.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "nodes.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 3) { r.error = "invalid_field_count"; return r; }
        node::Node n; std::string err;
        if (!LineParser::parse_uint64(f[0], n.id, err)) { r.error = "invalid_integer" + err; return r; }
        if (ids.count(n.id)) { r.error = "duplicate_id"; return r; }
        ids.insert(n.id);
        n.name = f[1]; n.type = f[2];
        r.records.push_back(std::move(n));
    }
    r.success = true;
    return r;
}

// ---- read_php_versions ----
DatasetResult<php::PhpVersion> LegacyDatasetReader::read_php_versions() {
    DatasetResult<php::PhpVersion> r;
    fs::path p(legacy_dir_ + "php_versions.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "php_versions.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 5) { r.error = "invalid_field_count"; return r; }
        php::PhpVersion pv; std::string err;
        if (!LineParser::parse_uint64(f[0], pv.id, err)) { r.error = "invalid_integer" + err; return r; }
        if (ids.count(pv.id)) { r.error = "duplicate_id"; return r; }
        ids.insert(pv.id);
        pv.version = f[1]; pv.image = f[2];
        if (!LineParser::parse_bool(f[3], pv.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        if (!LineParser::parse_bool(f[4], pv.default_version, err)) { r.error = "invalid_boolean" + err; return r; }
        pv.name = pv.version;
        r.records.push_back(std::move(pv));
    }
    r.success = true;
    return r;
}

// ---- read_profiles_only (profiles.db, 9-field) ----
DatasetResult<profile::Profile> LegacyDatasetReader::read_profiles_only() {
    DatasetResult<profile::Profile> r;
    fs::path pp(legacy_dir_ + "profiles.db");
    if (!fs::exists(pp)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids; std::string err;
    LineParser lp(pp, "profiles.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() != 9) { r.error = "invalid_field_count"; return r; }
        profile::Profile rec;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer:" + err; return r; } rec.id = tmp; }
        if (ids.count(rec.id)) { r.error = "duplicate_id"; return r; }
        ids.insert(rec.id);
        rec.profile_name = f[i++];
        rec.type = profile::profile_type_from_string(f[i++]);
        rec.web_server = f[i++]; rec.runtime = f[i++]; rec.template_path = f[i++]; rec.description = f[i++];
        if (!LineParser::parse_bool(f[i++], rec.enabled, err)) { r.error = "invalid_boolean:" + err; return r; }
        if (!LineParser::parse_bool(f[i++], rec.default_profile, err)) { r.error = "invalid_boolean:" + err; return r; }
        rec.name = rec.profile_name;
        r.records.push_back(std::move(rec));
    }
    r.success = true;
    return r;
}

// ---- read_templates_only (template_profiles.db, 8-field) ----
DatasetResult<profile::Profile> LegacyDatasetReader::read_templates_only() {
    DatasetResult<profile::Profile> r;
    fs::path tp(legacy_dir_ + "template_profiles.db");
    if (!fs::exists(tp)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids; std::string err;
    LineParser lp(tp, "template_profiles.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() != 8) { r.error = "invalid_field_count"; return r; }
        profile::Profile rec;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer:" + err; return r; } rec.id = tmp; }
        if (ids.count(rec.id)) { r.error = "duplicate_id"; return r; }
        ids.insert(rec.id);
        rec.profile_name = f[i++];
        rec.type = profile::ProfileType::WEB_SERVER;
        rec.web_server = f[i++]; rec.runtime = f[i++]; rec.template_path = f[i++]; rec.description = f[i++];
        if (!LineParser::parse_bool(f[i++], rec.enabled, err)) { r.error = "invalid_boolean:" + err; return r; }
        if (!LineParser::parse_bool(f[i++], rec.default_profile, err)) { r.error = "invalid_boolean:" + err; return r; }
        rec.name = rec.profile_name;
        r.records.push_back(std::move(rec));
    }
    r.success = true;
    return r;
}

// ---- read_combined_profiles (profiles.db + template_profiles.db) ----
DatasetResult<profile::Profile> LegacyDatasetReader::read_combined_profiles() {
    DatasetResult<profile::Profile> r;
    std::set<uint64_t> ids;
    std::set<std::string> names;
    std::string err;

    // profiles.db (9-field, required)
    fs::path pp(legacy_dir_ + "profiles.db");
    if (!fs::exists(pp)) { r.error = "file_missing:profiles.db"; return r; }
    {
        LineParser lp(pp, "profiles.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split(); int i = 0;
            if (f.size() != 9) { r.error = "invalid_field_count"; return r; }
            profile::Profile rec;
            { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } rec.id = tmp; }
            if (ids.count(rec.id)) { r.error = "duplicate_id:profiles.db"; return r; }
            ids.insert(rec.id);
            rec.profile_name = f[i++];
            if (names.count(rec.profile_name)) { r.error = "duplicate_profile_name:profiles.db:" + rec.profile_name; return r; }
            names.insert(rec.profile_name);
            rec.type = profile::profile_type_from_string(f[i++]);
            rec.web_server = f[i++]; rec.runtime = f[i++]; rec.template_path = f[i++]; rec.description = f[i++];
            if (!LineParser::parse_bool(f[i++], rec.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
            if (!LineParser::parse_bool(f[i++], rec.default_profile, err)) { r.error = "invalid_boolean" + err; return r; }
            rec.name = rec.profile_name;
            r.records.push_back(std::move(rec));
        }
    }

    // template_profiles.db (8-field, optional)
    fs::path tp(legacy_dir_ + "template_profiles.db");
    if (fs::exists(tp) && fs::file_size(tp) > 0) {
        LineParser lp(tp, "template_profiles.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split(); int i = 0;
            if (f.size() != 8) { r.error = "invalid_field_count"; return r; }
            profile::Profile rec;
            { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } rec.id = tmp; }
            if (ids.count(rec.id)) { r.error = "duplicate_id:template_profiles.db"; return r; }
            ids.insert(rec.id);
            rec.profile_name = f[i++];
            if (names.count(rec.profile_name)) { r.error = "duplicate_profile_name:template_profiles.db:" + rec.profile_name; return r; }
            names.insert(rec.profile_name);
            rec.type = profile::ProfileType::WEB_SERVER;
            rec.web_server = f[i++]; rec.runtime = f[i++]; rec.template_path = f[i++]; rec.description = f[i++];
            if (!LineParser::parse_bool(f[i++], rec.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
            if (!LineParser::parse_bool(f[i++], rec.default_profile, err)) { r.error = "invalid_boolean" + err; return r; }
            rec.name = rec.profile_name;
            r.records.push_back(std::move(rec));
        }
    }

    r.success = true;
    return r;
}

// ---- read_users ----
DatasetResult<user::User> LegacyDatasetReader::read_users() {
    DatasetResult<user::User> r;
    fs::path p(legacy_dir_ + "users.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids; std::set<std::string> usernames;
    LineParser lp(p, "users.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() != 6) { r.error = "invalid_field_count"; return r; }
        user::User u; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } u.id = tmp; }
        if (ids.count(u.id)) { r.error = "duplicate_id"; return r; }
        ids.insert(u.id);
        u.username = f[i++];
        if (usernames.count(u.username)) { r.error = "duplicate_username"; return r; }
        usernames.insert(u.username);
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } u.uid = tmp; }
        u.home_directory = f[i++]; u.shell = f[i++];
        if (!LineParser::parse_bool(f[i++], u.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        u.name = u.username;
        r.records.push_back(std::move(u));
    }
    r.success = true;
    return r;
}

// ---- read_sites (5-field or 6-field) ----
DatasetResult<site::Site> LegacyDatasetReader::read_sites() {
    DatasetResult<site::Site> r;
    fs::path p(legacy_dir_ + "sites.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids; std::set<std::string> domains;
    LineParser lp(p, "sites.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        int pipes = lp.count_pipes(); auto f = lp.split();
        if (pipes >= 5) {
            if (f.size() != 6 && f.size() != 7) { r.error = "invalid_field_count"; return r; }
            site::Site s; int i = 0; std::string err;
            { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } s.id = tmp; }
            if (ids.count(s.id)) { r.error = "duplicate_id"; return r; } ids.insert(s.id);
            s.domain = f[i++];
            if (domains.count(s.domain)) { r.error = "duplicate_domain"; return r; } domains.insert(s.domain);
            s.owner = f[i++];
            { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } s.node_id = tmp; }
            s.web_server = f[i++].empty() ? "apache" : f[i-1];
            if (!LineParser::parse_bool(f[i++], s.php_mail_enabled, err)) { r.error = "invalid_boolean" + err; return r; }
            if (i < static_cast<int>(f.size())) s.web_template_profile = f[i++];
            s.php_mail_enabled_present = true; s.name = s.domain;
            r.records.push_back(std::move(s));
        } else {
            if (f.size() != 5) { r.error = "invalid_field_count"; return r; }
            site::Site s; int i = 0; std::string err;
            { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } s.id = tmp; }
            if (ids.count(s.id)) { r.error = "duplicate_id"; return r; } ids.insert(s.id);
            s.domain = f[i++];
            if (domains.count(s.domain)) { r.error = "duplicate_domain"; return r; } domains.insert(s.domain);
            s.owner = f[i++];
            { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } s.node_id = tmp; }
            s.web_server = f[i++].empty() ? "apache" : f[i-1];
            s.php_mail_enabled = false; s.php_mail_enabled_present = false; s.name = s.domain;
            r.records.push_back(std::move(s));
        }
    }
    r.success = true;
    return r;
}

// ---- read_domains ----
DatasetResult<domain::Domain> LegacyDatasetReader::read_domains() {
    DatasetResult<domain::Domain> r;
    fs::path p(legacy_dir_ + "domains.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "domains.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); size_t i = 0;
        if (f.size() < 7) { r.error = "invalid_field_count"; return r; }
        domain::Domain d; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } d.id = tmp; }
        if (ids.count(d.id)) { r.error = "duplicate_id"; return r; } ids.insert(d.id);
        d.fqdn = f[i++];
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } d.owner_id = tmp; }
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } d.site_id = tmp; }
        d.php_version = f[i++];
        if (!LineParser::parse_bool(f[i++], d.ssl_enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        if (!LineParser::parse_bool(f[i++], d.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        d.type = (f.size() > i) ? f[i++] : std::string("primary");
        d.target = (f.size() > i) ? f[i++] : std::string();
        d.name = d.fqdn;
        r.records.push_back(std::move(d));
    }
    r.success = true;
    return r;
}

// ---- read_databases ----
DatasetResult<database::Database> LegacyDatasetReader::read_databases() {
    DatasetResult<database::Database> r;
    fs::path p(legacy_dir_ + "databases.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "databases.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() != 9) { r.error = "invalid_field_count"; return r; }
        database::Database d; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } d.id = tmp; }
        if (ids.count(d.id)) { r.error = "duplicate_id"; return r; } ids.insert(d.id);
        d.db_name = f[i++]; d.db_user = f[i++]; d.db_password = f[i++];
        d.engine = f[i++]; d.version = f[i++];
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } d.owner_id = tmp; }
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } d.site_id = tmp; }
        if (!LineParser::parse_bool(f[i++], d.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        d.name = d.db_name;
        r.records.push_back(std::move(d));
    }
    r.success = true;
    return r;
}

// ---- read_backups ----
DatasetResult<backup::Backup> LegacyDatasetReader::read_backups() {
    DatasetResult<backup::Backup> r;
    fs::path p(legacy_dir_ + "backups.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "backups.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() != 10) { r.error = "invalid_field_count"; return r; }
        backup::Backup b; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } b.id = tmp; }
        if (ids.count(b.id)) { r.error = "duplicate_id"; return r; } ids.insert(b.id);
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } b.site_id = tmp; }
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } b.owner_id = tmp; }
        b.filename = f[i++]; b.type = f[i++];
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } b.size = tmp; }
        b.created_at = f[i++]; b.status = f[i++]; b.file_path = f[i++]; b.compression = f[i++];
        b.name = b.filename;
        r.records.push_back(std::move(b));
    }
    r.success = true;
    return r;
}

// ---- read_reverse_proxies ----
DatasetResult<proxy::ReverseProxy> LegacyDatasetReader::read_reverse_proxies() {
    DatasetResult<proxy::ReverseProxy> r;
    fs::path p(legacy_dir_ + "reverse_proxies.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "reverse_proxies.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() != 8) { r.error = "invalid_field_count"; return r; }
        proxy::ReverseProxy pr; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } pr.id = tmp; }
        if (ids.count(pr.id)) { r.error = "duplicate_id"; return r; } ids.insert(pr.id);
        pr.domain = f[i++];
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } pr.site_id = tmp; }
        pr.provider = f[i++]; pr.config_path = f[i++]; pr.upstream = f[i++];
        if (!LineParser::parse_bool(f[i++], pr.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        pr.status = f[i++]; pr.name = pr.domain;
        r.records.push_back(std::move(pr));
    }
    r.success = true;
    return r;
}

// ---- read_access_users ----
DatasetResult<access::AccessUser> LegacyDatasetReader::read_access_users() {
    DatasetResult<access::AccessUser> r;
    fs::path p(legacy_dir_ + "access_users.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "access_users.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() != 5) { r.error = "invalid_field_count"; return r; }
        access::AccessUser u; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } u.id = tmp; }
        if (ids.count(u.id)) { r.error = "duplicate_id"; return r; } ids.insert(u.id);
        u.username = f[i++]; u.auth_type = f[i++]; u.password_hash = f[i++];
        if (!LineParser::parse_bool(f[i++], u.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        u.name = u.username;
        r.records.push_back(std::move(u));
    }
    r.success = true;
    return r;
}

// ---- read_access_grants ----
DatasetResult<access::AccessGrant> LegacyDatasetReader::read_access_grants() {
    DatasetResult<access::AccessGrant> r;
    fs::path p(legacy_dir_ + "access_grants.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "access_grants.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() != 4) { r.error = "invalid_field_count"; return r; }
        access::AccessGrant g; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } g.id = tmp; }
        if (ids.count(g.id)) { r.error = "duplicate_id"; return r; } ids.insert(g.id);
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } g.access_user_id = tmp; }
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } g.site_id = tmp; }
        g.permission = access::permission_from_string(f[i++]);
        g.name = std::to_string(g.access_user_id) + "-" + std::to_string(g.site_id);
        r.records.push_back(std::move(g));
    }
    r.success = true;
    return r;
}

// ---- read_auth_users ----
DatasetResult<auth::AuthUser> LegacyDatasetReader::read_auth_users() {
    DatasetResult<auth::AuthUser> r;
    fs::path p(legacy_dir_ + "auth_users.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "auth_users.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() < 6) { r.error = "invalid_field_count"; return r; }
        auth::AuthUser u; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } u.id = tmp; }
        if (ids.count(u.id)) { r.error = "duplicate_id"; return r; } ids.insert(u.id);
        u.username = f[i++]; u.password_hash = f[i++];
        if (!LineParser::parse_bool(f[i++], u.must_change_password, err)) { r.error = "invalid_boolean" + err; return r; }
        if (!LineParser::parse_bool(f[i++], u.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        u.role = f[i++]; u.name = u.username;
        r.records.push_back(std::move(u));
    }
    r.success = true;
    return r;
}

// ---- read_ssl_certificates (20-field current or legacy) ----
DatasetResult<ssl::SslCertificate> LegacyDatasetReader::read_ssl_certificates() {
    DatasetResult<ssl::SslCertificate> r;
    fs::path p(legacy_dir_ + "ssl_certificates.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "ssl_certificates.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); size_t i = 0;
        if (f.size() < 6) { r.error = "invalid_field_count"; return r; }
        ssl::SslCertificate c; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } c.id = tmp; }
        if (ids.count(c.id)) { r.error = "duplicate_id"; return r; } ids.insert(c.id);
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } c.domain_id = tmp; }
        c.domain = f[i++]; c.provider = f[i++]; c.certificate_path = f[i++]; c.key_path = f[i++];
        if (f.size() >= 20) {
            if (f.size() > i) c.chain_path = f[i++];
            if (f.size() > i) c.issued_at = f[i++];
            if (f.size() > i) c.expires_at = f[i++];
            if (f.size() > i) c.renew_after = f[i++];
            if (f.size() > i) c.status = f[i++];
            if (f.size() > i) { if (!LineParser::parse_bool(f[i++], c.auto_renew, err)) { r.error = "invalid_boolean" + err; return r; } }
            if (f.size() > i) { if (!LineParser::parse_bool(f[i++], c.https_enabled, err)) { r.error = "invalid_boolean" + err; return r; } }
            if (f.size() > i) { if (!LineParser::parse_bool(f[i++], c.redirect_enabled, err)) { r.error = "invalid_boolean" + err; return r; } }
            if (f.size() > i) c.domains = f[i++];
            if (f.size() > i) c.challenge_type = f[i++];
            if (f.size() > i) c.last_error = f[i++];
            if (f.size() > i) c.last_validation = f[i++];
            if (f.size() > i) { if (!LineParser::parse_int(f[i++], c.renew_attempts, err)) { r.error = "invalid_integer" + err; return r; } }
            if (f.size() > i) { if (!LineParser::parse_int(f[i++], c.version, err)) { r.error = "invalid_integer" + err; return r; } }
        } else {
            if (f.size() > i) c.expires_at = f[i++];
            if (f.size() > i) c.status = f[i++];
            if (f.size() > i) { if (!LineParser::parse_bool(f[i++], c.https_enabled, err)) { r.error = "invalid_boolean" + err; return r; } }
            if (f.size() > i) { if (!LineParser::parse_bool(f[i++], c.auto_renew, err)) { r.error = "invalid_boolean" + err; return r; } }
            c.version = 0;
        }
        c.name = c.domain;
        r.records.push_back(std::move(c));
    }
    r.success = true;
    return r;
}

// ---- read_mail_domains (10-field legacy or 12-field current) ----
DatasetResult<mail::MailDomain> LegacyDatasetReader::read_mail_domains() {
    DatasetResult<mail::MailDomain> r;
    fs::path p(legacy_dir_ + "mail_domains.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "mail_domains.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); size_t i = 0;
        int pipes = lp.count_pipes();
        if (f.size() < 10) { r.error = "invalid_field_count"; return r; }
        mail::MailDomain m; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } m.id = tmp; }
        if (ids.count(m.id)) { r.error = "duplicate_id"; return r; } ids.insert(m.id);
        m.mode = mail::mail_domain_mode_from_string(f[i++]); m.domain_name = f[i++];

        if (pipes <= 9) {
            // Legacy 10-field: id|mode|domain_name|owner_id|enabled|catch_all|dkim_selector|relay_host|max_mailboxes|max_aliases
            { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } m.domain_id = tmp; /* owner_id → domain_id */ }
            if (f.size() > i && !LineParser::parse_bool(f[i++], m.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
            if (f.size() > i) m.catch_all = f[i++];
            if (f.size() > i) m.dkim_selector = f[i++];
            if (f.size() > i) m.relay_host = f[i++];
            if (f.size() > i) { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } m.max_mailboxes = tmp; }
            if (f.size() > i) { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } m.max_aliases = tmp; }
        } else {
            // Current 12-field: id|mode|domain_name|domain_id|site_id|enabled|catch_all|dkim_selector|dkim_public_key_dns|relay_host|max_mailboxes|max_aliases
            { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } m.domain_id = tmp; }
            if (f.size() > i) { uint64_t tmp; if (LineParser::parse_uint64(f[i++], tmp, err)) { m.site_id = tmp; } }
            if (f.size() > i && !LineParser::parse_bool(f[i++], m.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
            if (f.size() > i) m.catch_all = f[i++];
            if (f.size() > i) m.dkim_selector = f[i++];
            if (f.size() > i) m.dkim_public_key_dns = f[i++];
            if (f.size() > i) m.relay_host = f[i++];
            if (f.size() > i) { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } m.max_mailboxes = tmp; }
            if (f.size() > i) { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } m.max_aliases = tmp; }
        }
        m.name = m.domain_name;
        r.records.push_back(std::move(m));
    }
    r.success = true;
    return r;
}

// ---- read_mailboxes ----
DatasetResult<mail::Mailbox> LegacyDatasetReader::read_mailboxes() {
    DatasetResult<mail::Mailbox> r;
    fs::path p(legacy_dir_ + "mail_mailboxes.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "mail_mailboxes.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() != 13) { r.error = "invalid_field_count"; return r; }
        mail::Mailbox mb; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } mb.id = tmp; }
        if (ids.count(mb.id)) { r.error = "duplicate_id"; return r; } ids.insert(mb.id);
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } mb.domain_id = tmp; }
        mb.local_part = f[i++]; mb.password_hash = f[i++];
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } mb.quota_bytes = tmp; }
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } mb.quota_messages = tmp; }
        if (!LineParser::parse_bool(f[i++], mb.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        mb.display_name = f[i++]; mb.forward_to = f[i++];
        if (!LineParser::parse_bool(f[i++], mb.spam_enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        mb.last_login = f[i++]; mb.created_at = f[i++]; mb.updated_at = f[i++];
        mb.name = mb.local_part;
        r.records.push_back(std::move(mb));
    }
    r.success = true;
    return r;
}

// ---- read_mail_aliases ----
DatasetResult<mail::MailAlias> LegacyDatasetReader::read_mail_aliases() {
    DatasetResult<mail::MailAlias> r;
    fs::path p(legacy_dir_ + "mail_aliases.db");
    if (!fs::exists(p)) { r.error = "file_missing"; return r; }
    std::set<uint64_t> ids;
    LineParser lp(p, "mail_aliases.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split(); int i = 0;
        if (f.size() != 7) { r.error = "invalid_field_count"; return r; }
        mail::MailAlias a; std::string err;
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } a.id = tmp; }
        if (ids.count(a.id)) { r.error = "duplicate_id"; return r; } ids.insert(a.id);
        { uint64_t tmp; if (!LineParser::parse_uint64(f[i++], tmp, err)) { r.error = "invalid_integer" + err; return r; } a.domain_id = tmp; }
        a.source_local_part = f[i++]; a.destination = f[i++];
        if (!LineParser::parse_bool(f[i++], a.enabled, err)) { r.error = "invalid_boolean" + err; return r; }
        a.created_at = f[i++]; a.updated_at = f[i++];
        a.name = a.source_local_part;
        r.records.push_back(std::move(a));
    }
    r.success = true;
    return r;
}

// ---- read_mail_config ----
LegacyDatasetReader::MailConfigResult LegacyDatasetReader::read_mail_config() {
    MailConfigResult r;
    fs::path state_path(legacy_dir_ + "mail_state.db");
    if (fs::exists(state_path)) {
        r.module_state_present = true;
        if (fs::file_size(state_path) > 0) {
            std::ifstream f(state_path); std::getline(f, r.module_state);
            if (!r.module_state.empty() && r.module_state != "active" && r.module_state != "inactive") {
                r.error = "invalid_module_state"; return r;
            }
        }
    }
    fs::path smtp_path(legacy_dir_ + "mail_smarthost.db");
    if (fs::exists(smtp_path)) {
        r.smarthost_present = true;
        if (fs::file_size(smtp_path) > 0) {
            std::ifstream f(smtp_path); std::getline(f, r.smarthost);
        }
    }
    r.success = true;
    return r;
}
} // namespace containercp::storage
