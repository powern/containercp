#include "LegacyImporter.h"
#include "profile/ProfileType.h"
#include "mail/MailModuleState.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <set>
#include <cstring>
#include <cerrno>

namespace containercp::storage {
namespace fs = std::filesystem;

LegacyImporter::LegacyImporter(const std::string& legacy_directory, ConnectionPool& pool)
    : legacy_dir_(legacy_directory)
    , pool_(pool)
    , sqlite_(pool)
{
    if (!legacy_dir_.empty() && legacy_dir_.back() != '/') {
        legacy_dir_ += '/';
    }
}

// -----------------------------------------------------------
// File state classification
// -----------------------------------------------------------

LegacyImporter::FileState LegacyImporter::inspect_file(
    const std::string& filename) const
{
    fs::path p(legacy_dir_ + filename);

    std::error_code ec;
    fs::file_status st = fs::status(p, ec);
    if (ec) {
        // Path does not exist
        return FileState::Missing;
    }

    if (!fs::is_regular_file(st)) {
        return FileState::InvalidType;
    }

    auto size = fs::file_size(p, ec);
    if (ec) {
        return FileState::Unreadable;
    }
    if (size == 0) {
        return FileState::Empty;
    }

    // Verify we can actually read it
    std::ifstream test(p);
    if (!test.is_open()) {
        return FileState::Unreadable;
    }
    std::string first;
    if (!std::getline(test, first) && test.fail() && !test.eof()) {
        return FileState::ReadError;
    }

    return FileState::RegularReadable;
}

// -----------------------------------------------------------
// inspect_and_begin — file inspection + early return for
// missing/unreadable/empty
// -----------------------------------------------------------

ImportResult LegacyImporter::inspect_and_begin(
    const std::string& type,
    const std::string& filename,
    bool required)
{
    ImportResult r;
    r.resource_type = type;
    r.source_file = filename;

    FileState st = inspect_file(filename);
    switch (st) {
    case FileState::Missing:
        if (!required) {
            r.success = true;
            r.disposition = ImportDisposition::SkippedMissingOptional;
            return r;
        }
        r.success = false;
        r.disposition = ImportDisposition::Failed;
        r.error = "file_missing";
        r.diagnostics = "Required file not found: " + filename;
        return r;

    case FileState::Unreadable:
        r.success = false;
        r.disposition = ImportDisposition::Failed;
        r.error = "file_unreadable";
        r.diagnostics = "File cannot be read: " + filename;
        return r;

    case FileState::InvalidType:
        r.success = false;
        r.disposition = ImportDisposition::Failed;
        r.error = "invalid_file_type";
        r.diagnostics = "Path is not a regular file: " + filename;
        return r;

    case FileState::ReadError:
        r.success = false;
        r.disposition = ImportDisposition::Failed;
        r.error = "file_read_error";
        r.diagnostics = "I/O error reading file: " + filename;
        return r;

    case FileState::Empty:
        r.success = true;
        r.disposition = ImportDisposition::SkippedEmpty;
        r.record_count = 0;
        return r;

    case FileState::RegularReadable:
        // success with no error — caller fills the rest
        // Use Imported as a provisional disposition (finish_import will confirm it).
        r.success = false;
        r.disposition = ImportDisposition::Imported;
        return r;
    }
    return r;
}

// -----------------------------------------------------------
// finish_import — centralised checked-saver result builder
// -----------------------------------------------------------

ImportResult LegacyImporter::finish_import(
    ImportResult r,
    bool write_ok,
    uint64_t count)
{
    if (!write_ok) {
        r.success = false;
        r.disposition = ImportDisposition::Failed;
        r.error = "sqlite_write_failed";
        r.diagnostics = "SQLite persistence failed for " + r.resource_type;
        r.record_count = 0;
        return r;
    }
    r.success = true;
    r.disposition = ImportDisposition::Imported;
    r.record_count = count;
    return r;
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

    LineParser(const fs::path& path, const std::string& fname)
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

    // Split on '|', preserving trailing empty fields.
    // std::getline discards trailing empties, so we use manual iteration.
    std::vector<std::string> split() const {
        std::vector<std::string> fields;
        size_t start = 0;
        while (start <= current_line.size()) {
            size_t pos = current_line.find('|', start);
            if (pos == std::string::npos) {
                fields.push_back(current_line.substr(start));
                break;
            }
            fields.push_back(current_line.substr(start, pos - start));
            start = pos + 1;
        }
        return fields;
    }

    void fail(const std::string& msg) {
        has_error = true;
        error_msg = msg;
    }

    // Safe unsigned integer parsing — checks errno for ERANGE.
    static bool parse_uint64(const std::string& s, uint64_t& out, std::string& err) {
        if (s.empty()) { err = "empty field"; return false; }
        if (s[0] == '-') { err = "negative value"; return false; }
        errno = 0;
        char* end = nullptr;
        unsigned long long val = std::strtoull(s.c_str(), &end, 10);
        if (end == s.c_str()) { err = "no digits"; return false; }
        if (*end != '\0') { err = "trailing characters"; return false; }
        if (errno == ERANGE) { err = "overflow"; return false; }
        out = static_cast<uint64_t>(val);
        return true;
    }

    static bool parse_uint64_strict(const std::string& s, uint64_t& out, std::string& err) {
        return parse_uint64(s, out, err);
    }

    static bool parse_int(const std::string& s, int& out, std::string& err) {
        if (s.empty()) { err = "empty field"; return false; }
        errno = 0;
        char* end = nullptr;
        long val = std::strtol(s.c_str(), &end, 10);
        if (end == s.c_str()) { err = "no digits"; return false; }
        if (*end != '\0') { err = "trailing characters"; return false; }
        if (errno == ERANGE || val < INT_MIN || val > INT_MAX) {
            err = "overflow"; return false;
        }
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

} // anonymous namespace

// ============================================================
// Per-resource import implementations
// ============================================================

ImportResult LegacyImporter::import_nodes() {
    auto r = inspect_and_begin("nodes", "nodes.db", true);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<node::Node> nodes;
        std::set<uint64_t> ids;
        LineParser lp(fs::path(legacy_dir_ + "nodes.db"), "nodes.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 3) {
                r.error = "invalid_field_count";
                r.diagnostics = "nodes.db:" + std::to_string(lp.line_number)
                    + ": expected 3 fields, got " + std::to_string(f.size());
                return r;
            }
            node::Node n;
            std::string err;
            if (!LineParser::parse_uint64(f[0], n.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "nodes.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(n.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "nodes.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(n.id);
            n.name = f[1];
            n.type = f[2];
            nodes.push_back(std::move(n));
        }
        bool ok = sqlite_.try_save_nodes(nodes);
        r = finish_import(std::move(r), ok, nodes.size());
    }
    return r;
}

ImportResult LegacyImporter::import_php_versions() {
    auto r = inspect_and_begin("php_versions", "php_versions.db", true);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<php::PhpVersion> versions;
        std::set<uint64_t> ids;
        LineParser lp(fs::path(legacy_dir_ + "php_versions.db"), "php_versions.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 5) {
                r.error = "invalid_field_count";
                r.diagnostics = "php_versions.db:" + std::to_string(lp.line_number)
                    + ": expected 5 fields, got " + std::to_string(f.size());
                return r;
            }
            php::PhpVersion pv;
            std::string err;
            if (!LineParser::parse_uint64(f[0], pv.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "php_versions.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(pv.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "php_versions.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(pv.id);
            pv.version = f[1];
            pv.image = f[2];
            if (!LineParser::parse_bool(f[3], pv.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "php_versions.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            if (!LineParser::parse_bool(f[4], pv.default_version, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "php_versions.db:" + std::to_string(lp.line_number) + ": default: " + err;
                return r;
            }
            pv.name = pv.version;
            versions.push_back(std::move(pv));
        }
        bool ok = sqlite_.try_save_php_versions(versions);
        r = finish_import(std::move(r), ok, versions.size());
    }
    return r;
}

ImportResult LegacyImporter::import_profiles() {
    auto r = inspect_and_begin("profiles", "profiles.db", true);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<profile::Profile> profiles;
        std::set<uint64_t> ids;
        LineParser lp(fs::path(legacy_dir_ + "profiles.db"), "profiles.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 9) {
                r.error = "invalid_field_count";
                r.diagnostics = "profiles.db:" + std::to_string(lp.line_number)
                    + ": expected 9 fields, got " + std::to_string(f.size());
                return r;
            }
            profile::Profile p;
            std::string err;
            if (!LineParser::parse_uint64(f[0], p.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "profiles.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(p.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "profiles.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(p.id);
            p.profile_name = f[1];
            p.type = profile::profile_type_from_string(f[2]);
            p.web_server = f[3];
            p.runtime = f[4];
            p.template_path = f[5];
            p.description = f[6];
            if (!LineParser::parse_bool(f[7], p.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "profiles.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            if (!LineParser::parse_bool(f[8], p.default_profile, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "profiles.db:" + std::to_string(lp.line_number) + ": default: " + err;
                return r;
            }
            p.name = p.profile_name;
            profiles.push_back(std::move(p));
        }
        bool ok = sqlite_.try_save_profiles(profiles);
        r = finish_import(std::move(r), ok, profiles.size());
    }
    return r;
}

ImportResult LegacyImporter::import_template_profiles() {
    auto r = inspect_and_begin("template_profiles", "template_profiles.db", false);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<profile::Profile> profiles;
        std::set<uint64_t> ids;
        LineParser lp(fs::path(legacy_dir_ + "template_profiles.db"), "template_profiles.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 8) {
                r.error = "invalid_field_count";
                r.diagnostics = "template_profiles.db:" + std::to_string(lp.line_number)
                    + ": expected 8 fields, got " + std::to_string(f.size());
                return r;
            }
            profile::Profile p;
            std::string err;
            if (!LineParser::parse_uint64(f[0], p.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "template_profiles.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(p.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "template_profiles.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(p.id);
            p.profile_name = f[1];
            p.type = profile::ProfileType::WEB_SERVER;
            p.web_server = f[2];
            p.runtime = f[3];
            p.template_path = f[4];
            p.description = f[5];
            if (!LineParser::parse_bool(f[6], p.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "template_profiles.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            if (!LineParser::parse_bool(f[7], p.default_profile, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "template_profiles.db:" + std::to_string(lp.line_number) + ": default: " + err;
                return r;
            }
            p.name = p.profile_name;
            profiles.push_back(std::move(p));
        }
        bool ok = sqlite_.try_save_profiles(profiles);
        r = finish_import(std::move(r), ok, profiles.size());
    }
    return r;
}

ImportResult LegacyImporter::import_users() {
    auto r = inspect_and_begin("users", "users.db", true);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<user::User> users;
        std::set<uint64_t> ids;
        std::set<std::string> usernames;
        LineParser lp(fs::path(legacy_dir_ + "users.db"), "users.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 6) {
                r.error = "invalid_field_count";
                r.diagnostics = "users.db:" + std::to_string(lp.line_number)
                    + ": expected 6 fields, got " + std::to_string(f.size());
                return r;
            }
            user::User u;
            std::string err;
            if (!LineParser::parse_uint64(f[0], u.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "users.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(u.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "users.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(u.id);
            u.username = f[1];
            if (usernames.count(u.username)) {
                r.error = "duplicate_username";
                r.diagnostics = "users.db:" + std::to_string(lp.line_number) + ": duplicate username";
                return r;
            }
            usernames.insert(u.username);
            if (!LineParser::parse_uint64(f[2], u.uid, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "users.db:" + std::to_string(lp.line_number) + ": uid: " + err;
                return r;
            }
            u.home_directory = f[3];
            u.shell = f[4];
            if (!LineParser::parse_bool(f[5], u.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "users.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            u.name = u.username;
            users.push_back(std::move(u));
        }
        bool ok = sqlite_.try_save_users(users);
        r = finish_import(std::move(r), ok, users.size());
    }
    return r;
}

ImportResult LegacyImporter::import_sites() {
    auto r = inspect_and_begin("sites", "sites.db", true);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<site::Site> sites;
        std::set<uint64_t> ids;
        std::set<std::string> domains;
        LineParser lp(fs::path(legacy_dir_ + "sites.db"), "sites.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            int pipes = lp.count_pipes();
            auto f = lp.split();

            if (pipes >= 5) {
                // Current 6-field format
                if (f.size() != 6) {
                    r.error = "invalid_field_count";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number)
                        + ": expected 6 fields, got " + std::to_string(f.size());
                    return r;
                }
                site::Site s;
                std::string err;
                if (!LineParser::parse_uint64(f[0], s.id, err)) {
                    r.error = "invalid_integer";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": id: " + err;
                    return r;
                }
                if (ids.count(s.id)) {
                    r.error = "duplicate_id";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": duplicate id";
                    return r;
                }
                ids.insert(s.id);
                s.domain = f[1];
                if (domains.count(s.domain)) {
                    r.error = "duplicate_domain";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": duplicate domain";
                    return r;
                }
                domains.insert(s.domain);
                s.owner = f[2];
                if (!LineParser::parse_uint64(f[3], s.node_id, err)) {
                    r.error = "invalid_integer";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": node_id: " + err;
                    return r;
                }
                s.web_server = f[4].empty() ? "apache" : f[4];
                if (!LineParser::parse_bool(f[5], s.php_mail_enabled, err)) {
                    r.error = "invalid_boolean";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": php_mail: " + err;
                    return r;
                }
                s.php_mail_enabled_present = true;
                s.name = s.domain;
                sites.push_back(std::move(s));
            } else {
                // Legacy 5-field format (no php_mail_enabled)
                if (f.size() != 5) {
                    r.error = "invalid_field_count";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number)
                        + ": expected 5 fields (legacy), got " + std::to_string(f.size());
                    return r;
                }
                site::Site s;
                std::string err;
                if (!LineParser::parse_uint64(f[0], s.id, err)) {
                    r.error = "invalid_integer";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": id: " + err;
                    return r;
                }
                if (ids.count(s.id)) {
                    r.error = "duplicate_id";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": duplicate id";
                    return r;
                }
                ids.insert(s.id);
                s.domain = f[1];
                if (domains.count(s.domain)) {
                    r.error = "duplicate_domain";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": duplicate domain";
                    return r;
                }
                domains.insert(s.domain);
                s.owner = f[2];
                if (!LineParser::parse_uint64(f[3], s.node_id, err)) {
                    r.error = "invalid_integer";
                    r.diagnostics = "sites.db:" + std::to_string(lp.line_number) + ": node_id: " + err;
                    return r;
                }
                s.web_server = f[4].empty() ? "apache" : f[4];
                s.php_mail_enabled = false;
                s.php_mail_enabled_present = false;
                s.name = s.domain;
                sites.push_back(std::move(s));
            }
        }
        bool ok = sqlite_.try_save_sites(sites);
        r = finish_import(std::move(r), ok, sites.size());
    }
    return r;
}

ImportResult LegacyImporter::import_domains() {
    auto r = inspect_and_begin("domains", "domains.db", true);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<domain::Domain> domains;
        std::set<uint64_t> ids;
        std::set<std::string> fqdns;
        LineParser lp(fs::path(legacy_dir_ + "domains.db"), "domains.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() < 7) {
                r.error = "invalid_field_count";
                r.diagnostics = "domains.db:" + std::to_string(lp.line_number)
                    + ": expected 7+ fields, got " + std::to_string(f.size());
                return r;
            }
            domain::Domain d;
            std::string err;
            if (!LineParser::parse_uint64(f[0], d.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(d.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(d.id);
            d.fqdn = f[1];
            if (fqdns.count(d.fqdn)) {
                r.error = "duplicate_fqdn";
                r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": duplicate fqdn";
                return r;
            }
            fqdns.insert(d.fqdn);
            if (!LineParser::parse_uint64(f[2], d.owner_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": owner_id: " + err;
                return r;
            }
            if (!LineParser::parse_uint64(f[3], d.site_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": site_id: " + err;
                return r;
            }
            d.php_version = f[4];
            if (!LineParser::parse_bool(f[5], d.ssl_enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": ssl: " + err;
                return r;
            }
            if (!LineParser::parse_bool(f[6], d.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "domains.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            if (f.size() > 7) d.type = f[7]; else d.type = "primary";
            if (f.size() > 8) d.target = f[8];
            d.name = d.fqdn;
            domains.push_back(std::move(d));
        }
        bool ok = sqlite_.try_save_domains(domains);
        r = finish_import(std::move(r), ok, domains.size());
    }
    return r;
}

ImportResult LegacyImporter::import_databases() {
    auto r = inspect_and_begin("databases", "databases.db", true);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<database::Database> databases;
        std::set<uint64_t> ids;
        std::set<std::string> db_names;
        LineParser lp(fs::path(legacy_dir_ + "databases.db"), "databases.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 9) {
                r.error = "invalid_field_count";
                r.diagnostics = "databases.db:" + std::to_string(lp.line_number)
                    + ": expected 9 fields, got " + std::to_string(f.size());
                return r;
            }
            database::Database d;
            std::string err;
            if (!LineParser::parse_uint64(f[0], d.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(d.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(d.id);
            d.db_name = f[1];
            if (db_names.count(d.db_name)) {
                r.error = "duplicate_db_name";
                r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": duplicate db_name";
                return r;
            }
            db_names.insert(d.db_name);
            d.db_user = f[2];
            d.db_password = f[3];
            d.engine = f[4];
            d.version = f[5];
            if (!LineParser::parse_uint64(f[6], d.owner_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": owner_id: " + err;
                return r;
            }
            if (!LineParser::parse_uint64(f[7], d.site_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": site_id: " + err;
                return r;
            }
            if (!LineParser::parse_bool(f[8], d.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "databases.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            d.name = d.db_name;
            databases.push_back(std::move(d));
        }
        bool ok = sqlite_.try_save_databases(databases);
        r = finish_import(std::move(r), ok, databases.size());
    }
    return r;
}

ImportResult LegacyImporter::import_backups() {
    auto r = inspect_and_begin("backups", "backups.db", true);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<backup::Backup> backups;
        std::set<uint64_t> ids;
        LineParser lp(fs::path(legacy_dir_ + "backups.db"), "backups.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 10) {
                r.error = "invalid_field_count";
                r.diagnostics = "backups.db:" + std::to_string(lp.line_number)
                    + ": expected 10 fields, got " + std::to_string(f.size());
                return r;
            }
            backup::Backup b;
            std::string err;
            if (!LineParser::parse_uint64(f[0], b.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "backups.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(b.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "backups.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(b.id);
            if (!LineParser::parse_uint64(f[1], b.site_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "backups.db:" + std::to_string(lp.line_number) + ": site_id: " + err;
                return r;
            }
            if (!LineParser::parse_uint64(f[2], b.owner_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "backups.db:" + std::to_string(lp.line_number) + ": owner_id: " + err;
                return r;
            }
            b.filename = f[3];
            b.type = f[4];
            if (!LineParser::parse_uint64(f[5], b.size, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "backups.db:" + std::to_string(lp.line_number) + ": size: " + err;
                return r;
            }
            b.created_at = f[6];
            b.status = f[7];
            b.file_path = f[8];
            b.compression = f[9];
            b.name = b.filename;
            backups.push_back(std::move(b));
        }
        // Backups use direct TransactionGuard (not via SQLiteStorage)
        TransactionGuard txn(pool_);
        if (!txn.is_active()) {
            r = finish_import(std::move(r), false, 0);
            r.error = "transaction_failed";
            return r;
        }
        if (!txn.db().exec("DELETE FROM backups")) {
            r = finish_import(std::move(r), false, 0);
            r.error = "sqlite_write_failed";
            r.diagnostics = "backups: DELETE failed";
            return r;
        }
        for (const auto& b : backups) {
            if (!txn.db().prepare("INSERT INTO backups "
                "(id, site_id, owner_id, filename, type, size, created_at, status, file_path, compression, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'))"))
            { r = finish_import(std::move(r), false, 0); r.error = "prepare_failed"; return r; }
            if (!txn.db().bind_int(1, static_cast<int64_t>(b.id))) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_int(2, static_cast<int64_t>(b.site_id))) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_int(3, static_cast<int64_t>(b.owner_id))) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(4, b.filename)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(5, b.type)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_int(6, static_cast<int64_t>(b.size))) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(7, b.created_at)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(8, b.status)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(9, b.file_path)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(10, b.compression)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (txn.db().step() == false && txn.db().error_code() != 0) { r = finish_import(std::move(r), false, 0); r.error = "step_failed"; return r; }
        }
        bool ok = txn.commit();
        r = finish_import(std::move(r), ok, backups.size());
    }
    return r;
}

ImportResult LegacyImporter::import_reverse_proxies() {
    auto r = inspect_and_begin("reverse_proxies", "reverse_proxies.db", true);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<proxy::ReverseProxy> proxies;
        std::set<uint64_t> ids;
        std::set<std::string> domains;
        LineParser lp(fs::path(legacy_dir_ + "reverse_proxies.db"), "reverse_proxies.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 8) {
                r.error = "invalid_field_count";
                r.diagnostics = "reverse_proxies.db:" + std::to_string(lp.line_number)
                    + ": expected 8 fields, got " + std::to_string(f.size());
                return r;
            }
            proxy::ReverseProxy p;
            std::string err;
            if (!LineParser::parse_uint64(f[0], p.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "reverse_proxies.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(p.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "reverse_proxies.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(p.id);
            p.domain = f[1];
            if (domains.count(p.domain)) {
                r.error = "duplicate_domain";
                r.diagnostics = "reverse_proxies.db:" + std::to_string(lp.line_number) + ": duplicate domain";
                return r;
            }
            domains.insert(p.domain);
            if (!LineParser::parse_uint64(f[2], p.site_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "reverse_proxies.db:" + std::to_string(lp.line_number) + ": site_id: " + err;
                return r;
            }
            p.provider = f[3];
            p.config_path = f[4];
            p.upstream = f[5];
            if (!LineParser::parse_bool(f[6], p.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "reverse_proxies.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            p.status = f[7];
            p.name = p.domain;
            proxies.push_back(std::move(p));
        }
        bool ok = sqlite_.try_save_reverse_proxies(proxies);
        r = finish_import(std::move(r), ok, proxies.size());
    }
    return r;
}

ImportResult LegacyImporter::import_access_users() {
    auto r = inspect_and_begin("access_users", "access_users.db", false);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<access::AccessUser> users;
        std::set<uint64_t> ids;
        std::set<std::string> usernames;
        LineParser lp(fs::path(legacy_dir_ + "access_users.db"), "access_users.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 5) {
                r.error = "invalid_field_count";
                r.diagnostics = "access_users.db:" + std::to_string(lp.line_number)
                    + ": expected 5 fields, got " + std::to_string(f.size());
                return r;
            }
            access::AccessUser u;
            std::string err;
            if (!LineParser::parse_uint64(f[0], u.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "access_users.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(u.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "access_users.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(u.id);
            u.username = f[1];
            if (usernames.count(u.username)) {
                r.error = "duplicate_username";
                r.diagnostics = "access_users.db:" + std::to_string(lp.line_number) + ": duplicate username";
                return r;
            }
            usernames.insert(u.username);
            u.auth_type = f[2];
            u.password_hash = f[3];
            if (!LineParser::parse_bool(f[4], u.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "access_users.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            u.name = u.username;
            users.push_back(std::move(u));
        }
        bool ok = sqlite_.try_save_access_users(users);
        r = finish_import(std::move(r), ok, users.size());
    }
    return r;
}

ImportResult LegacyImporter::import_access_grants() {
    auto r = inspect_and_begin("access_grants", "access_grants.db", false);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<access::AccessGrant> grants;
        std::set<uint64_t> ids;
        LineParser lp(fs::path(legacy_dir_ + "access_grants.db"), "access_grants.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 4) {
                r.error = "invalid_field_count";
                r.diagnostics = "access_grants.db:" + std::to_string(lp.line_number)
                    + ": expected 4 fields, got " + std::to_string(f.size());
                return r;
            }
            access::AccessGrant g;
            std::string err;
            if (!LineParser::parse_uint64(f[0], g.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "access_grants.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(g.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "access_grants.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(g.id);
            if (!LineParser::parse_uint64(f[1], g.access_user_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "access_grants.db:" + std::to_string(lp.line_number) + ": access_user_id: " + err;
                return r;
            }
            if (!LineParser::parse_uint64(f[2], g.site_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "access_grants.db:" + std::to_string(lp.line_number) + ": site_id: " + err;
                return r;
            }
            g.permission = access::permission_from_string(f[3]);
            g.name = std::to_string(g.access_user_id) + "-" + std::to_string(g.site_id);
            grants.push_back(std::move(g));
        }
        bool ok = sqlite_.try_save_access_grants(grants);
        r = finish_import(std::move(r), ok, grants.size());
    }
    return r;
}

ImportResult LegacyImporter::import_auth_users() {
    auto r = inspect_and_begin("auth_users", "auth_users.db", false);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<auth::AuthUser> users;
        std::set<uint64_t> ids;
        std::set<std::string> usernames;
        LineParser lp(fs::path(legacy_dir_ + "auth_users.db"), "auth_users.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() < 6) {
                r.error = "invalid_field_count";
                r.diagnostics = "auth_users.db:" + std::to_string(lp.line_number)
                    + ": expected 6 fields, got " + std::to_string(f.size());
                return r;
            }
            auth::AuthUser u;
            std::string err;
            if (!LineParser::parse_uint64(f[0], u.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "auth_users.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(u.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "auth_users.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(u.id);
            u.username = f[1];
            if (usernames.count(u.username)) {
                r.error = "duplicate_username";
                r.diagnostics = "auth_users.db:" + std::to_string(lp.line_number) + ": duplicate username";
                return r;
            }
            usernames.insert(u.username);
            u.password_hash = f[2];
            if (!LineParser::parse_bool(f[3], u.must_change_password, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "auth_users.db:" + std::to_string(lp.line_number) + ": must_change: " + err;
                return r;
            }
            if (!LineParser::parse_bool(f[4], u.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "auth_users.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            u.role = f[5];
            u.name = u.username;
            users.push_back(std::move(u));
        }
        // Write directly via TransactionGuard
        TransactionGuard txn(pool_);
        if (!txn.is_active()) {
            r = finish_import(std::move(r), false, 0);
            r.error = "transaction_failed";
            return r;
        }
        if (!txn.db().exec("DELETE FROM auth_users")) {
            r = finish_import(std::move(r), false, 0);
            r.error = "sqlite_write_failed";
            r.diagnostics = "auth_users: DELETE failed";
            return r;
        }
        for (const auto& u : users) {
            if (!txn.db().prepare("INSERT INTO auth_users "
                "(id, username, password_hash, must_change_password, enabled, role, created_at, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, strftime('%Y-%m-%dT%H:%M:%SZ','now'), "
                "strftime('%Y-%m-%dT%H:%M:%SZ','now'))"))
            { r = finish_import(std::move(r), false, 0); r.error = "prepare_failed"; return r; }
            if (!txn.db().bind_int(1, static_cast<int64_t>(u.id))) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(2, u.username)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(3, u.password_hash)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_int(4, u.must_change_password ? 1 : 0)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_int(5, u.enabled ? 1 : 0)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (!txn.db().bind_text(6, u.role)) { r = finish_import(std::move(r), false, 0); r.error = "bind_failed"; return r; }
            if (txn.db().step() == false && txn.db().error_code() != 0) { r = finish_import(std::move(r), false, 0); r.error = "step_failed"; return r; }
        }
        bool ok = txn.commit();
        r = finish_import(std::move(r), ok, users.size());
    }
    return r;
}

ImportResult LegacyImporter::import_ssl_certificates() {
    auto r = inspect_and_begin("ssl_certificates", "ssl_certificates.db", false);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<ssl::SslCertificate> certs;
        std::set<uint64_t> ids;
        LineParser lp(fs::path(legacy_dir_ + "ssl_certificates.db"), "ssl_certificates.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() < 6) {
                r.error = "invalid_field_count";
                r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number)
                    + ": too few fields: " + std::to_string(f.size());
                return r;
            }
            ssl::SslCertificate c;
            std::string err;
            if (!LineParser::parse_uint64(f[0], c.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(c.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(c.id);
            if (!LineParser::parse_uint64(f[1], c.domain_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": domain_id: " + err;
                return r;
            }
            c.domain = f[2];
            c.provider = f[3];
            c.certificate_path = f[4];
            c.key_path = f[5];

            if (f.size() >= 20) {
                // Current extended format (20 fields)
                if (f.size() >= 7) c.chain_path = f[6];
                if (f.size() >= 8) c.issued_at = f[7];
                if (f.size() >= 9) c.expires_at = f[8];
                if (f.size() >= 10) c.renew_after = f[9];
                if (f.size() >= 11) c.status = f[10];
                if (f.size() >= 12) {
                    if (!LineParser::parse_bool(f[11], c.auto_renew, err)) {
                        r.error = "invalid_boolean"; r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": auto_renew: " + err; return r;
                    }
                }
                if (f.size() >= 13) {
                    if (!LineParser::parse_bool(f[12], c.https_enabled, err)) {
                        r.error = "invalid_boolean"; r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": https_enabled: " + err; return r;
                    }
                }
                if (f.size() >= 14) {
                    if (!LineParser::parse_bool(f[13], c.redirect_enabled, err)) {
                        r.error = "invalid_boolean"; r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": redirect: " + err; return r;
                    }
                }
                if (f.size() >= 15) c.domains = f[14];
                if (f.size() >= 16) c.challenge_type = f[15];
                if (f.size() >= 17) c.last_error = f[16];
                if (f.size() >= 18) c.last_validation = f[17];
                if (f.size() >= 19) {
                    if (!LineParser::parse_int(f[18], c.renew_attempts, err)) {
                        r.error = "invalid_integer"; r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": renew_attempts: " + err; return r;
                    }
                }
                if (f.size() >= 20) {
                    if (!LineParser::parse_int(f[19], c.version, err)) {
                        r.error = "invalid_integer"; r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": version: " + err; return r;
                    }
                }
            } else {
                // Legacy format (4 common fields after the 6 mandatory ones)
                if (f.size() >= 7) c.expires_at = f[6];
                if (f.size() >= 8) c.status = f[7];
                if (f.size() >= 9) {
                    if (!LineParser::parse_bool(f[8], c.https_enabled, err)) {
                        r.error = "invalid_boolean"; r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r;
                    }
                }
                if (f.size() >= 10) {
                    if (!LineParser::parse_bool(f[9], c.auto_renew, err)) {
                        r.error = "invalid_boolean"; r.diagnostics = "ssl_certificates.db:" + std::to_string(lp.line_number) + ": auto_renew: " + err; return r;
                    }
                }
                c.version = 0;
            }
            c.name = c.domain;
            certs.push_back(std::move(c));
        }
        bool ok = sqlite_.try_save_ssl_certificates(certs);
        r = finish_import(std::move(r), ok, certs.size());
    }
    return r;
}

ImportResult LegacyImporter::import_mail_domains() {
    auto r = inspect_and_begin("mail_domains", "mail_domains.db", false);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<mail::MailDomain> domains;
        std::set<uint64_t> ids;
        std::set<std::string> domain_names;
        LineParser lp(fs::path(legacy_dir_ + "mail_domains.db"), "mail_domains.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() < 10) {
                r.error = "invalid_field_count";
                r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number)
                    + ": too few fields: " + std::to_string(f.size());
                return r;
            }
            mail::MailDomain m;
            std::string err;
            if (!LineParser::parse_uint64(f[0], m.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(m.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(m.id);
            m.mode = mail::mail_domain_mode_from_string(f[1]);
            m.domain_name = f[2];
            if (domain_names.count(m.domain_name)) {
                r.error = "duplicate_domain_name";
                r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": duplicate domain_name";
                return r;
            }
            domain_names.insert(m.domain_name);

            int pipes = lp.count_pipes();
            if (pipes <= 9) {
                // Legacy 10-field
                if (!LineParser::parse_uint64(f[3], m.domain_id, err)) {
                    // owner_id → domain_id, sentinel 0 allowed
                }
                if (f.size() >= 5) {
                    if (!LineParser::parse_bool(f[4], m.enabled, err)) {
                        r.error = "invalid_boolean"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r;
                    }
                }
                if (f.size() >= 6) m.catch_all = f[5];
                if (f.size() >= 7) m.dkim_selector = f[6];
                if (f.size() >= 8) m.relay_host = f[7];
                if (f.size() >= 9) {
                    uint64_t tmp;
                    if (!LineParser::parse_uint64(f[8], tmp, err)) {
                        r.error = "invalid_integer"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": max_mailboxes: " + err; return r;
                    }
                    m.max_mailboxes = tmp;
                }
                if (f.size() >= 10) {
                    uint64_t tmp;
                    if (!LineParser::parse_uint64(f[9], tmp, err)) {
                        r.error = "invalid_integer"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": max_aliases: " + err; return r;
                    }
                    m.max_aliases = tmp;
                }
            } else {
                // Current 12-field format
                if (!LineParser::parse_uint64(f[3], m.domain_id, err)) {
                    r.error = "invalid_integer"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": domain_id: " + err; return r;
                }
                if (f.size() >= 5) {
                    if (!LineParser::parse_uint64(f[4], m.site_id, err)) {
                        // sentinel 0 ok
                    }
                }
                if (f.size() >= 6) {
                    if (!LineParser::parse_bool(f[5], m.enabled, err)) {
                        r.error = "invalid_boolean"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": enabled: " + err; return r;
                    }
                }
                if (f.size() >= 7) m.catch_all = f[6];
                if (f.size() >= 8) m.dkim_selector = f[7];
                if (f.size() >= 9) m.dkim_public_key_dns = f[8];
                if (f.size() >= 10) m.relay_host = f[9];
                if (f.size() >= 11) {
                    uint64_t tmp;
                    if (!LineParser::parse_uint64(f[10], tmp, err)) {
                        r.error = "invalid_integer"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": max_mailboxes: " + err; return r;
                    }
                    m.max_mailboxes = tmp;
                }
                if (f.size() >= 12) {
                    uint64_t tmp;
                    if (!LineParser::parse_uint64(f[11], tmp, err)) {
                        r.error = "invalid_integer"; r.diagnostics = "mail_domains.db:" + std::to_string(lp.line_number) + ": max_aliases: " + err; return r;
                    }
                    m.max_aliases = tmp;
                }
            }
            m.name = m.domain_name;
            domains.push_back(std::move(m));
        }
        bool ok = sqlite_.try_save_mail_domains(domains);
        r = finish_import(std::move(r), ok, domains.size());
    }
    return r;
}

ImportResult LegacyImporter::import_mail_mailboxes() {
    auto r = inspect_and_begin("mail_mailboxes", "mail_mailboxes.db", false);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<mail::Mailbox> mailboxes;
        std::set<uint64_t> ids;
        LineParser lp(fs::path(legacy_dir_ + "mail_mailboxes.db"), "mail_mailboxes.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 13) {
                r.error = "invalid_field_count";
                r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number)
                    + ": expected 13, got " + std::to_string(f.size());
                return r;
            }
            mail::Mailbox mb;
            std::string err;
            if (!LineParser::parse_uint64(f[0], mb.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(mb.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(mb.id);
            if (!LineParser::parse_uint64(f[1], mb.domain_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": domain_id: " + err;
                return r;
            }
            mb.local_part = f[2];
            mb.password_hash = f[3];
            {
                uint64_t tmp;
                if (!LineParser::parse_uint64(f[4], tmp, err)) {
                    r.error = "invalid_integer";
                    r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": quota_bytes: " + err;
                    return r;
                }
                mb.quota_bytes = tmp;
            }
            {
                uint64_t tmp;
                if (!LineParser::parse_uint64(f[5], tmp, err)) {
                    r.error = "invalid_integer";
                    r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": quota_messages: " + err;
                    return r;
                }
                mb.quota_messages = tmp;
            }
            if (!LineParser::parse_bool(f[6], mb.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            mb.display_name = f[7];
            mb.forward_to = f[8];
            if (!LineParser::parse_bool(f[9], mb.spam_enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "mail_mailboxes.db:" + std::to_string(lp.line_number) + ": spam: " + err;
                return r;
            }
            mb.last_login = f[10];
            mb.created_at = f[11];
            mb.updated_at = f[12];
            mb.name = mb.local_part;
            mailboxes.push_back(std::move(mb));
        }
        bool ok = sqlite_.try_save_mailboxes(mailboxes);
        r = finish_import(std::move(r), ok, mailboxes.size());
    }
    return r;
}

ImportResult LegacyImporter::import_mail_aliases() {
    auto r = inspect_and_begin("mail_aliases", "mail_aliases.db", false);
    if (!r.success && r.disposition == ImportDisposition::Imported) {
        std::vector<mail::MailAlias> aliases;
        std::set<uint64_t> ids;
        LineParser lp(fs::path(legacy_dir_ + "mail_aliases.db"), "mail_aliases.db");
        while (lp.next()) {
            if (lp.empty_line()) continue;
            auto f = lp.split();
            if (f.size() != 7) {
                r.error = "invalid_field_count";
                r.diagnostics = "mail_aliases.db:" + std::to_string(lp.line_number)
                    + ": expected 7, got " + std::to_string(f.size());
                return r;
            }
            mail::MailAlias a;
            std::string err;
            if (!LineParser::parse_uint64(f[0], a.id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "mail_aliases.db:" + std::to_string(lp.line_number) + ": id: " + err;
                return r;
            }
            if (ids.count(a.id)) {
                r.error = "duplicate_id";
                r.diagnostics = "mail_aliases.db:" + std::to_string(lp.line_number) + ": duplicate id";
                return r;
            }
            ids.insert(a.id);
            if (!LineParser::parse_uint64(f[1], a.domain_id, err)) {
                r.error = "invalid_integer";
                r.diagnostics = "mail_aliases.db:" + std::to_string(lp.line_number) + ": domain_id: " + err;
                return r;
            }
            a.source_local_part = f[2];
            a.destination = f[3];
            if (!LineParser::parse_bool(f[4], a.enabled, err)) {
                r.error = "invalid_boolean";
                r.diagnostics = "mail_aliases.db:" + std::to_string(lp.line_number) + ": enabled: " + err;
                return r;
            }
            a.created_at = f[5];
            a.updated_at = f[6];
            a.name = a.source_local_part;
            aliases.push_back(std::move(a));
        }
        bool ok = sqlite_.try_save_mail_aliases(aliases);
        r = finish_import(std::move(r), ok, aliases.size());
    }
    return r;
}

ImportResult LegacyImporter::import_mail_config() {
    ImportResult r;
    r.resource_type = "mail_config";
    r.source_file = "mail_state.db, mail_smarthost.db";

    bool has_state_file = (inspect_file("mail_state.db") == FileState::RegularReadable);
    bool has_smarthost_file = (inspect_file("mail_smarthost.db") == FileState::RegularReadable);

    if (!has_state_file && !has_smarthost_file) {
        // Both missing — check for unreadable/invalid
        auto st = inspect_file("mail_state.db");
        auto ss = inspect_file("mail_smarthost.db");
        if (st == FileState::Unreadable || st == FileState::InvalidType || st == FileState::ReadError) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "file_unreadable"; r.diagnostics = std::string("mail_state.db: ") + (st == FileState::Unreadable ? "unreadable" : st == FileState::InvalidType ? "not a file" : "read error");
            return r;
        }
        if (ss == FileState::Unreadable || ss == FileState::InvalidType || ss == FileState::ReadError) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "file_unreadable"; r.diagnostics = std::string("mail_smarthost.db: ") + (ss == FileState::Unreadable ? "unreadable" : ss == FileState::InvalidType ? "not a file" : "read error");
            return r;
        }
        // Both absent or empty
        if (st == FileState::Empty && ss == FileState::Empty) {
            r.success = true; r.disposition = ImportDisposition::SkippedEmpty;
            r.record_count = 0; return r;
        }
        r.success = true; r.disposition = ImportDisposition::SkippedMissingOptional;
        r.record_count = 0; return r;
    }

    std::string state_val;
    std::string smarthost_val;
    uint64_t imported_keys = 0;

    // Read module state
    if (has_state_file) {
        std::ifstream f(legacy_dir_ + "mail_state.db");
        std::getline(f, state_val);
        if (f.fail() && !f.eof()) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "file_read_error"; r.diagnostics = "mail_state.db: read error";
            return r;
        }
        // Validate against supported legacy contract: "active" or "inactive"
        if (!state_val.empty() && state_val != "active" && state_val != "inactive") {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "invalid_module_state";
            r.diagnostics = "mail_state.db: unsupported state (must be active or inactive)";
            return r;
        }
    }

    // Read smarthost
    if (has_smarthost_file) {
        std::ifstream f(legacy_dir_ + "mail_smarthost.db");
        std::getline(f, smarthost_val);
        if (f.fail() && !f.eof()) {
            r.success = false; r.disposition = ImportDisposition::Failed;
            r.error = "file_read_error"; r.diagnostics = "mail_smarthost.db: read error";
            return r;
        }
    }

    // Save module state if present
    if (!state_val.empty()) {
        bool ok = sqlite_.try_save_mail_module_state(state_val);
        if (!ok) {
            r = finish_import(std::move(r), false, imported_keys);
            r.error = "sqlite_write_failed";
            r.diagnostics = "mail_config: module_state persistence failed";
            return r;
        }
        ++imported_keys;
    }

    // Save smarthost if present (if state succeeded but smarthost fails,
    // overall mail_config fails — state remains committed)
    if (!smarthost_val.empty()) {
        bool ok = sqlite_.try_save_mail_smarthost(smarthost_val);
        if (!ok) {
            r = finish_import(std::move(r), false, imported_keys);
            r.error = "sqlite_write_failed";
            r.diagnostics = "mail_config: smarthost persistence failed";
            return r;
        }
        ++imported_keys;
    }

    // Determine disposition
    if (imported_keys == 0) {
        // Both files existed but were empty
        r.success = true;
        r.disposition = ImportDisposition::SkippedEmpty;
        r.record_count = 0;
        return r;
    }

    r.success = true;
    r.disposition = ImportDisposition::Imported;
    r.record_count = imported_keys;
    return r;
}

// ============================================================
// import_all — dependency-safe ordering, stops on write failure
// ============================================================

ImportAllResult LegacyImporter::import_all() {
    ImportAllResult result;

    auto do_step = [&](const std::string& name, ImportResult res) -> bool {
        result.resources.push_back(std::move(res));
        if (!result.resources.back().success) {
            result.success = false;
            result.failed_resource = name;
            result.error = result.resources.back().error;
            return false;
        }
        return true;
    };

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
    if (!do_step("access_grants", import_access_grants())) return result;
    if (!do_step("auth_users", import_auth_users())) return result;
    if (!do_step("ssl_certificates", import_ssl_certificates())) return result;
    if (!do_step("mail_domains", import_mail_domains())) return result;
    if (!do_step("mail_mailboxes", import_mail_mailboxes())) return result;
    if (!do_step("mail_aliases", import_mail_aliases())) return result;
    if (!do_step("mail_config", import_mail_config())) return result;

    result.success = true;
    return result;
}

} // namespace containercp::storage
