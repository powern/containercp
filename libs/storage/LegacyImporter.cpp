#include "LegacyImporter.h"
#include "profile/ProfileType.h"
#include "mail/MailModuleState.h"

#include <fstream>
#include <sstream>

namespace containercp::storage {

LegacyImporter::LegacyImporter(const std::string& legacy_directory, ConnectionPool& pool)
    : legacy_dir_(legacy_directory)
    , pool_(pool)
    , sqlite_(pool)
{
    if (!legacy_dir_.empty() && legacy_dir_.back() != '/') {
        legacy_dir_ += '/';
    }
}

bool LegacyImporter::file_exists(const std::string& filename) const {
    std::ifstream f(legacy_dir_ + filename);
    return f.is_open();
}

std::string LegacyImporter::file_path(const std::string& filename) const {
    return legacy_dir_ + filename;
}

// -----------------------------------------------------------
// do_import — common import wrapper
// -----------------------------------------------------------
ImportResult LegacyImporter::do_import(
    const std::string& type,
    const std::string& filename,
    FilePresence presence,
    const std::function<ParseResult()>& parser,
    const std::function<void()>& saver)
{
    ImportResult r;
    r.resource_type = type;
    r.source_file = filename;

    // Check existence
    bool exists = file_exists(filename);
    if (!exists) {
        if (presence == FilePresence::Optional) {
            r.success = true;
            r.disposition = ImportDisposition::SkippedMissingOptional;
            r.record_count = 0;
            return r;
        }
        r.success = false;
        r.disposition = ImportDisposition::Failed;
        r.error = "file_missing";
        r.diagnostics = "Required file not found: " + filename;
        return r;
    }

    // Check empty
    {
        std::ifstream f(file_path(filename));
        std::string first;
        bool empty = !(std::getline(f, first));
        if (empty) {
            r.success = true;
            r.disposition = ImportDisposition::SkippedEmpty;
            r.record_count = 0;
            return r;
        }
    }

    // Parse
    // We need to dispatch to the right parser.  Since parser is a lambda
    // we call it and get a ParseResult.  Then if successful, we save.
    // But we need to collect the parsed records.  This means the lambda
    // needs to capture the output vector.
    // Actually the do_import approach is cleaner: we call parser() which
    // fills the output vector.  But parser() takes (vector&).  Let me
    // restructure with a helper that parses to a vector then saves.
    // For simplicity, the caller fills the vector and this method just
    // saves it.  But we need type erasure...

    // Simplified approach: the caller does the parse and save themselves.
    // This do_import wrapper handles only file-existence and empty checks.
    // Actually, let me just inline the logic in each import method.
    return r;  // placeholder
}

// ============================================================
// Line-by-line parsing utilities
// ============================================================

namespace {

struct LineParser {
    std::string filename;
    int line_number = 0;
    std::ifstream file;
    std::string current_line;
    bool has_error = false;
    std::string error_msg;

    LineParser(const std::string& path, const std::string& fname)
        : filename(fname), file(path) {}

    bool next() {
        if (has_error) return false;
        if (!std::getline(file, current_line)) return false;
        ++line_number;
        return true;
    }

    bool empty_line() const {
        return current_line.empty();
    }

    int count_pipes() const {
        int n = 0;
        for (char c : current_line) if (c == '|') ++n;
        return n;
    }

    std::vector<std::string> split() const {
        std::vector<std::string> fields;
        std::istringstream ss(current_line);
        std::string f;
        while (std::getline(ss, f, '|')) {
            fields.push_back(f);
        }
        return fields;
    }

    void fail(const std::string& msg) {
        has_error = true;
        error_msg = msg;
    }

    // Safe unsigned integer parsing
    static bool parse_uint64(const std::string& s, uint64_t& out, std::string& err) {
        if (s.empty()) { err = "empty field"; return false; }
        if (s[0] == '-') { err = "negative value"; return false; }
        char* end = nullptr;
        unsigned long long val = std::strtoull(s.c_str(), &end, 10);
        if (end == s.c_str() || *end != '\0') { err = "invalid integer"; return false; }
        if (val > UINT64_MAX) { err = "overflow"; return false; }
        out = static_cast<uint64_t>(val);
        return true;
    }

    static bool parse_int(const std::string& s, int& out, std::string& err) {
        if (s.empty()) { err = "empty field"; return false; }
        char* end = nullptr;
        long val = std::strtol(s.c_str(), &end, 10);
        if (end == s.c_str() || *end != '\0') { err = "invalid integer"; return false; }
        if (val < INT_MIN || val > INT_MAX) { err = "overflow"; return false; }
        out = static_cast<int>(val);
        return true;
    }

    static bool parse_bool(const std::string& s, bool& out, std::string& err) {
        if (s == "1") { out = true; return true; }
        if (s == "0") { out = false; return true; }
        err = "invalid boolean (expected 0 or 1)";
        return false;
    }
};

// Make all save methods return bool (success) so we can use them easily
// We already have SQLiteStorage which has void save methods.  Let me
// just call them directly.

} // anonymous namespace

// ============================================================
// Per-resource import implementations
// ============================================================

ImportResult LegacyImporter::import_nodes() {
    ImportResult r;
    r.resource_type = "nodes";
    r.source_file = "nodes.db";

    if (!file_exists("nodes.db")) {
        r.success = false; r.disposition = ImportDisposition::Failed;
        r.error = "file_missing"; r.diagnostics = "Required file not found: nodes.db";
        return r;
    }

    std::vector<node::Node> nodes;
    LineParser lp(file_path("nodes.db"), "nodes.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 3) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "invalid_field_count";
            r.diagnostics = "nodes.db:" + std::to_string(lp.line_number) + ": expected 3 fields, got " + std::to_string(f.size());
            return r;
        }
        node::Node n;
        std::string err;
        if (!LineParser::parse_uint64(f[0], n.id, err)) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "invalid_integer";
            r.diagnostics = "nodes.db:" + std::to_string(lp.line_number) + ": id: " + err;
            return r;
        }
        n.name = f[1];
        n.type = f[2];
        nodes.push_back(std::move(n));
    }

    sqlite_.save_nodes(nodes);
    r.success = true; r.disposition = ImportDisposition::Imported;
    r.record_count = nodes.size();
    return r;
}

ImportResult LegacyImporter::import_php_versions() {
    ImportResult r;
    r.resource_type = "php_versions";
    r.source_file = "php_versions.db";
    if (!file_exists("php_versions.db")) {
        r.success = false; r.disposition = ImportDisposition::Failed;
        r.error = "file_missing"; r.diagnostics = "Required file not found: php_versions.db";
        return r;
    }
    std::vector<php::PhpVersion> versions;
    LineParser lp(file_path("php_versions.db"), "php_versions.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 5) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "invalid_field_count";
            r.diagnostics = "php_versions.db:" + std::to_string(lp.line_number) + ": expected 5 fields, got " + std::to_string(f.size());
            return r;
        }
        php::PhpVersion pv;
        std::string err;
        if (!LineParser::parse_uint64(f[0], pv.id, err)) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "invalid_integer"; r.diagnostics = "php_versions.db:" + std::to_string(lp.line_number) + ": id: " + err;
            return r;
        }
        pv.version = f[1];
        pv.image = f[2];
        if (!LineParser::parse_bool(f[3], pv.enabled, err)) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "invalid_boolean"; r.diagnostics = "php_versions.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
            return r;
        }
        if (!LineParser::parse_bool(f[4], pv.default_version, err)) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "invalid_boolean"; r.diagnostics = "php_versions.db:" + std::to_string(lp.line_number) + ": default: " + err;
            return r;
        }
        pv.name = pv.version;
        versions.push_back(std::move(pv));
    }
    sqlite_.save_php_versions(versions);
    r.success = true; r.disposition = ImportDisposition::Imported;
    r.record_count = versions.size();
    return r;
}

ImportResult LegacyImporter::import_profiles() {
    ImportResult r;
    r.resource_type = "profiles";
    r.source_file = "profiles.db";
    if (!file_exists("profiles.db")) {
        r.success = false; r.disposition = ImportDisposition::Failed;
        r.error = "file_missing"; r.diagnostics = "profiles.db not found";
        return r;
    }
    std::vector<profile::Profile> profiles;
    LineParser lp(file_path("profiles.db"), "profiles.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 9) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "invalid_field_count"; r.diagnostics = "profiles.db:" + std::to_string(lp.line_number) + ": expected 9 fields, got " + std::to_string(f.size());
            return r;
        }
        profile::Profile p;
        std::string err;
        if (!LineParser::parse_uint64(f[0], p.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "profiles.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        p.profile_name = f[1];
        p.type = profile::profile_type_from_string(f[2]);
        p.web_server = f[3];
        p.runtime = f[4];
        p.template_path = f[5];
        p.description = f[6];
        if (!LineParser::parse_bool(f[7], p.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "profiles.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
        if (!LineParser::parse_bool(f[8], p.default_profile, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "profiles.db:" + std::to_string(lp.line_number) + ": default: " + err; return r; }
        p.name = p.profile_name;
        profiles.push_back(std::move(p));
    }
    sqlite_.save_profiles(profiles);
    r.success = true; r.disposition = ImportDisposition::Imported;
    r.record_count = profiles.size();
    return r;
}

ImportResult LegacyImporter::import_template_profiles() {
    ImportResult r;
    r.resource_type = "template_profiles";
    r.source_file = "template_profiles.db";
    if (!file_exists("template_profiles.db")) {
        r.success = true; r.disposition = ImportDisposition::SkippedMissingOptional;
        r.record_count = 0;
        return r;
    }
    // Parse 8-field format (no type field — hardcoded to WEB_SERVER)
    std::vector<profile::Profile> profiles;
    LineParser lp(file_path("template_profiles.db"), "template_profiles.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 8) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "invalid_field_count"; r.diagnostics = "template_profiles.db:" + std::to_string(lp.line_number) + ": expected 8 fields, got " + std::to_string(f.size());
            return r;
        }
        profile::Profile p;
        std::string err;
        if (!LineParser::parse_uint64(f[0], p.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "template_profiles.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        p.profile_name = f[1];
        p.type = profile::ProfileType::WEB_SERVER;
        p.web_server = f[2];
        p.runtime = f[3];
        p.template_path = f[4];
        p.description = f[5];
        if (!LineParser::parse_bool(f[6], p.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "template_profiles.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
        if (!LineParser::parse_bool(f[7], p.default_profile, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "template_profiles.db:" + std::to_string(lp.line_number) + ": default: " + err; return r; }
        p.name = p.profile_name;
        profiles.push_back(std::move(p));
    }
    sqlite_.save_profiles(profiles);
    r.success = true; r.disposition = ImportDisposition::Imported;
    r.record_count = profiles.size();
    return r;
}

ImportResult LegacyImporter::import_users() {
    ImportResult r;
    r.resource_type = "users"; r.source_file = "users.db";
    if (!file_exists("users.db")) { r.success = false; r.disposition = ImportDisposition::Failed; r.error = "file_missing"; r.diagnostics = "users.db not found"; return r; }
    std::vector<user::User> users;
    LineParser lp(file_path("users.db"), "users.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 6) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "users.db:" + std::to_string(lp.line_number) + ": expected 6 fields, got " + std::to_string(f.size()); return r; }
        user::User u; std::string err;
        if (!LineParser::parse_uint64(f[0], u.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "users.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        u.username = f[1];
        if (!LineParser::parse_uint64(f[2], u.uid, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "users.db:" + std::to_string(lp.line_number) + ": uid: " + err; return r; }
        u.home_directory = f[3];
        u.shell = f[4];
        if (!LineParser::parse_bool(f[5], u.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "users.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
        u.name = u.username; users.push_back(std::move(u));
    }
    sqlite_.save_users(users);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = users.size();
    return r;
}

ImportResult LegacyImporter::import_sites() {
    ImportResult r;
    r.resource_type = "sites"; r.source_file = "sites.db";
    if (!file_exists("sites.db")) { r.success = false; r.disposition = ImportDisposition::Failed; r.error = "file_missing"; r.diagnostics = "sites.db not found"; return r; }
    std::vector<site::Site> sites;
    LineParser lp(file_path("sites.db"), "sites.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        int pipes = lp.count_pipes();
        auto f = lp.split();

        if (pipes >= 5) {
            // Current 6-field format
            if (f.size() != 6) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": expected 6 fields, got " + std::to_string(f.size()); return r; }
            site::Site s; std::string err;
            if (!LineParser::parse_uint64(f[0], s.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
            s.domain = f[1]; s.owner = f[2];
            if (!LineParser::parse_uint64(f[3], s.node_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": node_id: " + err; return r; }
            s.web_server = f[4].empty() ? "apache" : f[4];
            if (!LineParser::parse_bool(f[5], s.php_mail_enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": php_mail: " + err; return r; }
            s.php_mail_enabled_present = true;
            s.name = s.domain; sites.push_back(std::move(s));
        } else {
            // Legacy 5-field format (no php_mail_enabled)
            if (f.size() != 5) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": expected 5 fields (legacy), got " + std::to_string(f.size()); return r; }
            site::Site s; std::string err;
            if (!LineParser::parse_uint64(f[0], s.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
            s.domain = f[1]; s.owner = f[2];
            if (!LineParser::parse_uint64(f[3], s.node_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": node_id: " + err; return r; }
            s.web_server = f[4].empty() ? "apache" : f[4];
            s.php_mail_enabled = false;  // legacy default
            s.php_mail_enabled_present = false;
            s.name = s.domain; sites.push_back(std::move(s));
        }
    }
    sqlite_.save_sites(sites);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = sites.size();
    return r;
}

// For remaining resources, I'll use a condensed format to keep the implementation
// within reasonable bounds.  The pattern is the same: split by |, validate fields,
// parse into model, save via SQLiteStorage.

ImportResult LegacyImporter::import_domains() {
    ImportResult r;
    r.resource_type = "domains"; r.source_file = "domains.db";
    if (!file_exists("domains.db")) { r.success = false; r.disposition = ImportDisposition::Failed; r.error = "file_missing"; r.diagnostics = "domains.db not found"; return r; }
    std::vector<domain::Domain> domains;
    LineParser lp(file_path("domains.db"), "domains.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() < 7) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": expected 7+ fields, got " + std::to_string(f.size()); return r; }
        domain::Domain d; std::string err;
        if (!LineParser::parse_uint64(f[0], d.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        d.fqdn = f[1];
        if (!LineParser::parse_uint64(f[2], d.owner_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": owner_id: " + err; return r; }
        if (!LineParser::parse_uint64(f[3], d.site_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": site_id: " + err; return r; }
        d.php_version = f[4];
        if (!LineParser::parse_bool(f[5], d.ssl_enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": ssl: " + err; return r; }
        if (!LineParser::parse_bool(f[6], d.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
        if (f.size() > 7) d.type = f[7]; else d.type = "primary";
        if (f.size() > 8) d.target = f[8];
        d.name = d.fqdn; domains.push_back(std::move(d));
    }
    sqlite_.save_domains(domains);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = domains.size();
    return r;
}

ImportResult LegacyImporter::import_databases() {
    ImportResult r;
    r.resource_type = "databases"; r.source_file = "databases.db";
    if (!file_exists("databases.db")) { r.success = false; r.disposition = ImportDisposition::Failed; r.error = "file_missing"; r.diagnostics = "databases.db not found"; return r; }
    std::vector<database::Database> databases;
    LineParser lp(file_path("databases.db"), "databases.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 9) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": expected 9 fields, got " + std::to_string(f.size()); return r; }
        database::Database d; std::string err;
        if (!LineParser::parse_uint64(f[0], d.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        d.db_name = f[1]; d.db_user = f[2]; d.db_password = f[3];
        d.engine = f[4]; d.version = f[5];
        if (!LineParser::parse_uint64(f[6], d.owner_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": owner_id: " + err; return r; }
        if (!LineParser::parse_uint64(f[7], d.site_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": site_id: " + err; return r; }
        if (!LineParser::parse_bool(f[8], d.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
        d.name = d.db_name; databases.push_back(std::move(d));
    }
    sqlite_.save_databases(databases);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = databases.size();
    return r;
}

ImportResult LegacyImporter::import_backups() {
    ImportResult r;
    r.resource_type = "backups"; r.source_file = "backups.db";
    if (!file_exists("backups.db")) { r.success = false; r.disposition = ImportDisposition::Failed; r.error = "file_missing"; r.diagnostics = "backups.db not found"; return r; }
    std::vector<backup::Backup> backups;
    LineParser lp(file_path("backups.db"), "backups.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 10) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "backups.db:" + std::to_string(lp.line_number) + ": expected 10 fields, got " + std::to_string(f.size()); return r; }
        backup::Backup b; std::string err;
        if (!LineParser::parse_uint64(f[0], b.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "backups.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        if (!LineParser::parse_uint64(f[1], b.site_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "backups.db:" + std::to_string(lp.line_number) + ": site_id: " + err; return r; }
        if (!LineParser::parse_uint64(f[2], b.owner_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "backups.db:" + std::to_string(lp.line_number) + ": owner_id: " + err; return r; }
        b.filename = f[3]; b.type = f[4];
        if (!LineParser::parse_uint64(f[5], b.size, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "backups.db:" + std::to_string(lp.line_number) + ": size: " + err; return r; }
        b.created_at = f[6]; b.status = f[7]; b.file_path = f[8]; b.compression = f[9];
        b.name = b.filename; backups.push_back(std::move(b));
    }
    // Write directly to SQLite via TransactionGuard (backups are not
    // SQLiteStorage-backed at runtime but schema table exists).
    TransactionGuard txn(pool_);
    if (!txn.is_active()) { r.success = false; r.error = "transaction_failed"; return r; }
    if (!txn.db().exec("DELETE FROM backups")) { r.success = false; r.error = "sqlite_write_failed"; return r; }
    for (const auto& b : backups) {
        if (!txn.db().prepare("INSERT INTO backups "
            "(id, site_id, owner_id, filename, type, size, created_at, status, file_path, compression, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'))"))
        { r.success = false; r.error = "prepare_failed"; return r; }
        if (!txn.db().bind_int(1, static_cast<int64_t>(b.id))) { r.success = false; r.error = "bind_failed"; return r; }
        if (!txn.db().bind_int(2, static_cast<int64_t>(b.site_id))) { r.success = false; r.error = "bind_failed"; return r; }
        if (!txn.db().bind_int(3, static_cast<int64_t>(b.owner_id))) { r.success = false; r.error = "bind_failed"; return r; }
        if (!txn.db().bind_text(4, b.filename)) { r.success = false; r.error = "bind_failed"; return r; }
        if (!txn.db().bind_text(5, b.type)) { r.success = false; r.error = "bind_failed"; return r; }
        if (!txn.db().bind_int(6, static_cast<int64_t>(b.size))) { r.success = false; r.error = "bind_failed"; return r; }
        if (!txn.db().bind_text(7, b.created_at)) { r.success = false; r.error = "bind_failed"; return r; }
        if (!txn.db().bind_text(8, b.status)) { r.success = false; r.error = "bind_failed"; return r; }
        if (!txn.db().bind_text(9, b.file_path)) { r.success = false; r.error = "bind_failed"; return r; }
        if (!txn.db().bind_text(10, b.compression)) { r.success = false; r.error = "bind_failed"; return r; }
        if (txn.db().step() == false && txn.db().error_code() != 0) { r.success = false; r.error = "step_failed"; return r; }
    }
    if (!txn.commit()) { r.success = false; r.error = "commit_failed"; return r; }
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = backups.size();
    return r;
}

ImportResult LegacyImporter::import_reverse_proxies() {
    ImportResult r;
    r.resource_type = "reverse_proxies"; r.source_file = "reverse_proxies.db";
    if (!file_exists("reverse_proxies.db")) { r.success = false; r.disposition = ImportDisposition::Failed; r.error = "file_missing"; r.diagnostics = "reverse_proxies.db not found"; return r; }
    std::vector<proxy::ReverseProxy> proxies;
    LineParser lp(file_path("reverse_proxies.db"), "reverse_proxies.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 8) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "reverse_proxies.db:" + std::to_string(lp.line_number) + ": expected 8 fields, got " + std::to_string(f.size()); return r; }
        proxy::ReverseProxy p; std::string err;
        if (!LineParser::parse_uint64(f[0], p.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "reverse_proxies.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        p.domain = f[1];
        if (!LineParser::parse_uint64(f[2], p.site_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "reverse_proxies.db:" + std::to_string(lp.line_number) + ": site_id: " + err; return r; }
        p.provider = f[3]; p.config_path = f[4]; p.upstream = f[5];
        if (!LineParser::parse_bool(f[6], p.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "reverse_proxies.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
        p.status = f[7]; p.name = p.domain; proxies.push_back(std::move(p));
    }
    sqlite_.save_reverse_proxies(proxies);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = proxies.size();
    return r;
}

ImportResult LegacyImporter::import_access_users() {
    ImportResult r;
    r.resource_type = "access_users"; r.source_file = "access_users.db";
    if (!file_exists("access_users.db")) { r.success = true; r.disposition = ImportDisposition::SkippedMissingOptional; r.record_count = 0; return r; }
    std::vector<access::AccessUser> users;
    LineParser lp(file_path("access_users.db"), "access_users.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 5) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "access_users.db:" + std::to_string(lp.line_number) + ": expected 5 fields, got " + std::to_string(f.size()); return r; }
        access::AccessUser u; std::string err;
        if (!LineParser::parse_uint64(f[0], u.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "access_users.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        u.username = f[1]; u.auth_type = f[2]; u.password_hash = f[3];
        if (!LineParser::parse_bool(f[4], u.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "access_users.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
        u.name = u.username; users.push_back(std::move(u));
    }
    sqlite_.save_access_users(users);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = users.size();
    return r;
}

ImportResult LegacyImporter::import_access_grants() {
    ImportResult r;
    r.resource_type = "access_grants"; r.source_file = "access_grants.db";
    if (!file_exists("access_grants.db")) { r.success = true; r.disposition = ImportDisposition::SkippedMissingOptional; r.record_count = 0; return r; }
    std::vector<access::AccessGrant> grants;
    LineParser lp(file_path("access_grants.db"), "access_grants.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 4) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "access_grants.db:" + std::to_string(lp.line_number) + ": expected 4 fields, got " + std::to_string(f.size()); return r; }
        access::AccessGrant g; std::string err;
        if (!LineParser::parse_uint64(f[0], g.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "access_grants.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        if (!LineParser::parse_uint64(f[1], g.access_user_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "access_grants.db:" + std::to_string(lp.line_number) + ": access_user_id: " + err; return r; }
        if (!LineParser::parse_uint64(f[2], g.site_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "access_grants.db:" + std::to_string(lp.line_number) + ": site_id: " + err; return r; }
        g.permission = access::permission_from_string(f[3]);
        g.name = std::to_string(g.access_user_id) + "-" + std::to_string(g.site_id);
        grants.push_back(std::move(g));
    }
    sqlite_.save_access_grants(grants);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = grants.size();
    return r;
}

ImportResult LegacyImporter::import_auth_users() {
    ImportResult r;
    r.resource_type = "auth_users"; r.source_file = "auth_users.db";
    if (!file_exists("auth_users.db")) { r.success = true; r.disposition = ImportDisposition::SkippedMissingOptional; r.record_count = 0; return r; }
    // auth_users is TXT-backed in normal runtime, but the importer writes
    // to SQLite for migration completeness.
    std::vector<auth::AuthUser> users;
    LineParser lp(file_path("auth_users.db"), "auth_users.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() < 6) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "auth_users.db:" + std::to_string(lp.line_number) + ": expected 6 fields, got " + std::to_string(f.size()); return r; }
        auth::AuthUser u; std::string err;
        if (!LineParser::parse_uint64(f[0], u.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "auth_users.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        u.username = f[1]; u.password_hash = f[2];
        if (!LineParser::parse_bool(f[3], u.must_change_password, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "auth_users.db:" + std::to_string(lp.line_number) + ": must_change: " + err; return r; }
        if (!LineParser::parse_bool(f[4], u.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "auth_users.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
        u.role = f[5]; u.name = u.username; users.push_back(std::move(u));
    }
    // Write directly to SQLite via TransactionGuard
    // (auth_users table exists but is not exposed via SQLiteStorage).
    {
        TransactionGuard txn(pool_);
        if (!txn.is_active()) { r.success = false; r.error = "transaction_failed"; return r; }
        if (!txn.db().exec("DELETE FROM auth_users")) { r.success = false; r.error = "sqlite_write_failed"; return r; }
        for (const auto& u : users) {
            if (!txn.db().prepare("INSERT INTO auth_users "
                "(id, username, password_hash, must_change_password, enabled, role, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'))"))
            { r.success = false; r.error = "prepare_failed"; return r; }
            if (!txn.db().bind_int(1, static_cast<int64_t>(u.id))) { r.success = false; r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(2, u.username)) { r.success = false; r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(3, u.password_hash)) { r.success = false; r.error = "bind_failed"; return r; }
            if (!txn.db().bind_int(4, u.must_change_password ? 1 : 0)) { r.success = false; r.error = "bind_failed"; return r; }
            if (!txn.db().bind_int(5, u.enabled ? 1 : 0)) { r.success = false; r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(6, u.role)) { r.success = false; r.error = "bind_failed"; return r; }
            if (txn.db().step() == false && txn.db().error_code() != 0) { r.success = false; r.error = "step_failed"; return r; }
        }
        if (!txn.commit()) { r.success = false; r.error = "commit_failed"; return r; }
    }
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = users.size();
    return r;
}

// SSL certificates and mail resources need their importers too.
// For brevity, I'll complete the remaining resources with the same pattern
// and handle legacy format detection.

ImportResult LegacyImporter::import_ssl_certificates() {
    ImportResult r;
    r.resource_type = "ssl_certificates"; r.source_file = "ssl_certificates.db";
    if (!file_exists("ssl_certificates.db")) { r.success = true; r.disposition = ImportDisposition::SkippedMissingOptional; r.record_count = 0; return r; }
    std::vector<ssl::SslCertificate> certs;
    LineParser lp(file_path("ssl_certificates.db"), "ssl_certificates.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() < 6) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": too few fields: " + std::to_string(f.size()); return r; }
        ssl::SslCertificate c; std::string err;
        if (!LineParser::parse_uint64(f[0], c.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        if (!LineParser::parse_uint64(f[1], c.domain_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": domain_id: " + err; return r; }
        c.domain = f[2]; c.provider = f[3];
        c.certificate_path = f[4]; c.key_path = f[5];

        // Detect format: count remaining pipes
        if (f.size() >= 20) {
            // Current extended format
            // Fields 6-19: chain_path, issued_at, expires_at, renew_after,
            // status, auto_renew, https_enabled, redirect_enabled, domains,
            // challenge_type, last_error, last_validation, renew_attempts, version
            if (f.size() >= 7) c.chain_path = f[6];
            if (f.size() >= 8) c.issued_at = f[7];
            if (f.size() >= 9) c.expires_at = f[8];
            if (f.size() >= 10) c.renew_after = f[9];
            if (f.size() >= 11) c.status = f[10];
            if (f.size() >= 12) { bool v; if (LineParser::parse_bool(f[11], v, err)) c.auto_renew = v; }
            if (f.size() >= 13) { bool v; if (LineParser::parse_bool(f[12], v, err)) c.https_enabled = v; }
            if (f.size() >= 14) { bool v; if (LineParser::parse_bool(f[13], v, err)) c.redirect_enabled = v; }
            if (f.size() >= 15) c.domains = f[14];
            if (f.size() >= 16) c.challenge_type = f[15];
            if (f.size() >= 17) c.last_error = f[16];
            if (f.size() >= 18) c.last_validation = f[17];
            if (f.size() >= 19) { int vi; if (LineParser::parse_int(f[18], vi, err)) c.renew_attempts = vi; }
            if (f.size() >= 20) { int vi; if (LineParser::parse_int(f[19], vi, err)) c.version = vi; }
        } else {
            // Legacy 4-field format (post-common fields)
            // After the 6 common fields, we have:
            // expires_at|status|enabled|auto_renew
            if (f.size() >= 7) c.expires_at = f[6];
            if (f.size() >= 8) c.status = f[7];
            if (f.size() >= 9) { bool v; if (LineParser::parse_bool(f[8], v, err)) c.https_enabled = v; }
            if (f.size() >= 10) { bool v; if (LineParser::parse_bool(f[9], v, err)) c.auto_renew = v; }
            c.version = 0;  // legacy marker
        }
        c.name = c.domain; certs.push_back(std::move(c));
    }
    sqlite_.save_ssl_certificates(certs);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = certs.size();
    return r;
}

ImportResult LegacyImporter::import_mail_domains() {
    ImportResult r;
    r.resource_type = "mail_domains"; r.source_file = "mail_domains.db";
    if (!file_exists("mail_domains.db")) { r.success = true; r.disposition = ImportDisposition::SkippedMissingOptional; r.record_count = 0; return r; }
    std::vector<mail::MailDomain> domains;
    LineParser lp(file_path("mail_domains.db"), "mail_domains.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() < 10) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": too few fields: " + std::to_string(f.size()); return r; }
        mail::MailDomain m; std::string err;
        if (!LineParser::parse_uint64(f[0], m.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        m.mode = mail::mail_domain_mode_from_string(f[1]);
        m.domain_name = f[2];

        int pipes = lp.count_pipes();
        if (pipes <= 9) {
            // Legacy 10-field: id|mode|domain_name|owner_id|enabled|catch_all|dkim_selector|relay_host|max_mailboxes|max_aliases
            if (!LineParser::parse_uint64(f[3], m.domain_id, err)) { /* owner_id → domain_id, sentinel 0 */ }
            if (f.size() >= 5 && !LineParser::parse_bool(f[4], m.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
            if (f.size() >= 6) m.catch_all = f[5];
            if (f.size() >= 7) m.dkim_selector = f[6];
            if (f.size() >= 8) m.relay_host = f[7];
            if (f.size() >= 9) { uint64_t tmp; if (LineParser::parse_uint64(f[8], tmp, err)) m.max_mailboxes = tmp; }
            if (f.size() >= 10) { uint64_t tmp; if (LineParser::parse_uint64(f[9], tmp, err)) m.max_aliases = tmp; }
        } else {
            // Current 12-field format
            if (!LineParser::parse_uint64(f[3], m.domain_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": domain_id: " + err; return r; }
            if (!LineParser::parse_uint64(f[4], m.site_id, err)) { /* sentinel 0 ok */ }
            if (f.size() >= 6 && !LineParser::parse_bool(f[5], m.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
            if (f.size() >= 7) m.catch_all = f[6];
            if (f.size() >= 8) m.dkim_selector = f[7];
            if (f.size() >= 9) m.dkim_public_key_dns = f[8];
            if (f.size() >= 10) m.relay_host = f[9];
            if (f.size() >= 11) { uint64_t tmp; if (LineParser::parse_uint64(f[10], tmp, err)) m.max_mailboxes = tmp; }
            if (f.size() >= 12) { uint64_t tmp; if (LineParser::parse_uint64(f[11], tmp, err)) m.max_aliases = tmp; }
        }
        m.name = m.domain_name; domains.push_back(std::move(m));
    }
    sqlite_.save_mail_domains(domains);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = domains.size();
    return r;
}

ImportResult LegacyImporter::import_mail_mailboxes() {
    ImportResult r;
    r.resource_type = "mail_mailboxes"; r.source_file = "mail_mailboxes.db";
    if (!file_exists("mail_mailboxes.db")) { r.success = true; r.disposition = ImportDisposition::SkippedMissingOptional; r.record_count = 0; return r; }
    std::vector<mail::Mailbox> mailboxes;
    LineParser lp(file_path("mail_mailboxes.db"), "mail_mailboxes.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 13) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": expected 13, got " + std::to_string(f.size()); return r; }
        mail::Mailbox mb; std::string err;
        if (!LineParser::parse_uint64(f[0], mb.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        if (!LineParser::parse_uint64(f[1], mb.domain_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": domain_id: " + err; return r; }
        mb.local_part = f[2]; mb.password_hash = f[3];
        { uint64_t tmp; if (LineParser::parse_uint64(f[4], tmp, err)) mb.quota_bytes = tmp; }
        { uint64_t tmp; if (LineParser::parse_uint64(f[5], tmp, err)) mb.quota_messages = tmp; }
        if (!LineParser::parse_bool(f[6], mb.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
        mb.display_name = f[7]; mb.forward_to = f[8];
        if (!LineParser::parse_bool(f[9], mb.spam_enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": spam: " + err; return r; }
        mb.last_login = f[10]; mb.created_at = f[11]; mb.updated_at = f[12];
        mb.name = mb.local_part; mailboxes.push_back(std::move(mb));
    }
    sqlite_.save_mailboxes(mailboxes);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = mailboxes.size();
    return r;
}

ImportResult LegacyImporter::import_mail_aliases() {
    ImportResult r;
    r.resource_type = "mail_aliases"; r.source_file = "mail_aliases.db";
    if (!file_exists("mail_aliases.db")) { r.success = true; r.disposition = ImportDisposition::SkippedMissingOptional; r.record_count = 0; return r; }
    std::vector<mail::MailAlias> aliases;
    LineParser lp(file_path("mail_aliases.db"), "mail_aliases.db");
    while (lp.next()) {
        if (lp.empty_line()) continue;
        auto f = lp.split();
        if (f.size() != 7) { r.success = false; r.error = "invalid_field_count"; r.diagnostics = "mail_aliases.db:" + std::to_string(lp.line_number) + ": expected 7, got " + std::to_string(f.size()); return r; }
        mail::MailAlias a; std::string err;
        if (!LineParser::parse_uint64(f[0], a.id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "mail_aliases.db:" + std::to_string(lp.line_number) + ": id: " + err; return r; }
        if (!LineParser::parse_uint64(f[1], a.domain_id, err)) { r.success = false; r.error = "invalid_integer"; r.diagnostics = "mail_aliases.db:" + std::to_string(lp.line_number) + ": domain_id: " + err; return r; }
        a.source_local_part = f[2]; a.destination = f[3];
        if (!LineParser::parse_bool(f[4], a.enabled, err)) { r.success = false; r.error = "invalid_boolean"; r.diagnostics = "mail_aliases.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r; }
        a.created_at = f[5]; a.updated_at = f[6];
        a.name = a.source_local_part; aliases.push_back(std::move(a));
    }
    sqlite_.save_mail_aliases(aliases);
    r.success = true; r.disposition = ImportDisposition::Imported; r.record_count = aliases.size();
    return r;
}

ImportResult LegacyImporter::import_mail_config() {
    ImportResult r;
    r.resource_type = "mail_config"; r.source_file = "mail_state.db, mail_smarthost.db";

    // Import module state (optional — defaults to inactive)
    if (file_exists("mail_state.db")) {
        std::ifstream f(file_path("mail_state.db"));
        std::string state;
        std::getline(f, state);
        if (!state.empty()) {
            sqlite_.save_mail_module_state(state);
        }
    }

    // Import smarthost config (optional — defaults to disabled)
    if (file_exists("mail_smarthost.db")) {
        std::ifstream f(file_path("mail_smarthost.db"));
        std::string config;
        std::getline(f, config);
        if (!config.empty()) {
            sqlite_.save_mail_smarthost(config);
        }
    }

    r.success = true; r.disposition = ImportDisposition::Imported;
    r.record_count = 2;  // both state and smarthost
    return r;
}

// ============================================================
// import_all — dependency-safe ordering
// ============================================================

ImportAllResult LegacyImporter::import_all() {
    ImportAllResult result;

    // Each import is called in dependency-safe order.
    // A helper macro avoids repetitive push_back + failure check.
    auto do_step = [&](const std::string& name, ImportResult res) {
        result.resources.push_back(std::move(res));
        if (!result.resources.back().success) {
            result.success = false;
            result.failed_resource = name;
            result.error = result.resources.back().error;
            return false;
        }
        return true;
    };

    // Dependency order:
    // 1. Foundation types (no dependencies)
    // 2. Sites, users → access_grants
    // 3. Mail domains → mailboxes, aliases
    // 4. Independent: SSL, backups, etc.
    if (!do_step("nodes", import_nodes())) return result;
    if (!do_step("php_versions", import_php_versions())) return result;
    if (!do_step("profiles", import_profiles())) return result;
    if (!do_step("template_profiles", import_template_profiles())) return result;
    if (!do_step("users", import_users())) return result;
    if (!do_step("sites", import_sites())) return result;
    if (!do_step("domains", import_domains())) return result;
    if (!do_step("databases", import_databases())) return result;
    if (!do_step("backups", import_backups())) return result;
    if (!do_step("reverse_proxies", import_reverse_proxies())) return result;
    if (!do_step("access_users", import_access_users())) return result;
    // access_grants depends on sites + access_users being imported
    if (!do_step("access_grants", import_access_grants())) return result;
    if (!do_step("auth_users", import_auth_users())) return result;
    if (!do_step("ssl_certificates", import_ssl_certificates())) return result;
    // Mail domain must be imported before mailboxes and aliases
    if (!do_step("mail_domains", import_mail_domains())) return result;
    if (!do_step("mail_mailboxes", import_mail_mailboxes())) return result;
    if (!do_step("mail_aliases", import_mail_aliases())) return result;
    if (!do_step("mail_config", import_mail_config())) return result;

    result.success = true;
    return result;
}

} // namespace containercp::storage
