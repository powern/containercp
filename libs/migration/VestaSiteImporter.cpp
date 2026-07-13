#include "VestaSiteImporter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <climits>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <sys/stat.h>
#include <thread>

namespace containercp::migration {

VestaSiteImporter::VestaSiteImporter(runtime::CommandExecutor& executor,
                                     filesystem::Filesystem& fs,
                                     config::Config& cfg,
                                     site::SiteManager* sites,
                                     domain::DomainManager* domains)
    : executor_(executor)
    , fs_(fs)
    , cfg_(cfg)
    , sites_(sites)
    , domains_(domains)
{
}

bool VestaSiteImporter::tar_safe_list(const std::string& archive,
                                       std::vector<std::string>& entries,
                                       std::string& error) {
    auto result = executor_.run({
        "tar", "-tf", archive
    });
    if (result.exit_code != 0) {
        error = result.err.empty() ? "Failed to read archive" : result.err;
        return false;
    }

    std::istringstream stream(result.out);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        // Reject absolute paths
        if (line[0] == '/') {
            error = "Archive contains absolute path: " + line;
            return false;
        }

        // Split path into components to check for ".." as a component
        std::istringstream path_stream(line);
        std::string component;
        while (std::getline(path_stream, component, '/')) {
            if (component == "..") {
                error = "Archive contains parent directory reference: " + line;
                return false;
            }
        }

        // Reject paths that normalize to outside
        std::string normalized;
        std::istringstream norm_stream(line);
        std::vector<std::string> parts;
        while (std::getline(norm_stream, component, '/')) {
            if (component == "..") { // already rejected, but keep as safety
                error = "Archive contains parent directory reference: " + line;
                return false;
            }
            if (!component.empty() && component != ".") {
                parts.push_back(component);
            }
        }
        // Rebuild and check for leading /
        if (!parts.empty() && parts[0][0] == '/') {
            error = "Archive contains absolute path: " + line;
            return false;
        }

        entries.push_back(line);
    }
    return true;
}

bool VestaSiteImporter::find_domain_in_archive(
    const std::vector<std::string>& entries,
    const std::string& domain,
    std::string& web_archive_path,
    size_t& web_size,
    bool& size_known) {

    std::string target_dot = "./web/" + domain + "/domain_data.tar.gz";
    std::string target_no_dot = "web/" + domain + "/domain_data.tar.gz";

    for (const auto& e : entries) {
        if (e == target_dot || e == target_no_dot) {
            web_archive_path = "web/" + domain + "/domain_data.tar.gz";
            web_size = 0;
            size_known = false;
            return true;
        }
    }
    return false;
}

std::string VestaSiteImporter::detect_web_root(
    const std::string& staging_dir,
    const std::string& archive,
    const std::string& domain) {

    std::string data_tarball = staging_dir + "/domain_data.tar.gz";

    auto try_extract = [&](const std::string& prefix) -> bool {
        auto r = executor_.run({
            "tar", "-xf", archive, "-C", staging_dir, prefix
        });
        return r.exit_code == 0;
    };

    if (!try_extract("web/" + domain + "/domain_data.tar.gz") &&
        !try_extract("./web/" + domain + "/domain_data.tar.gz")) {
        return "";
    }

    executor_.run({
        "mv", staging_dir + "/web/" + domain + "/domain_data.tar.gz", data_tarball
    });
    executor_.run({"rm", "-rf", staging_dir + "/web"});

    auto list_result = executor_.run({"tar", "-tzf", data_tarball});
    if (list_result.exit_code != 0) {
        std::remove(data_tarball.c_str());
        return "";
    }

    static const char* candidates[] = {
        "./public_html", "./public", "./htdocs", "./www", "./root",
        "public_html", "public", "htdocs", "www", "root"
    };

    std::istringstream stream(list_result.out);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        for (const auto* cand : candidates) {
            std::string s(cand);
            if (line == s || line.substr(0, s.size() + 1) == s + "/") {
                std::remove(data_tarball.c_str());
                // Normalize: remove leading ./
                if (s.size() >= 2 && s[0] == '.' && s[1] == '/')
                    s = s.substr(2);
                return s;
            }
        }
    }

    std::remove(data_tarball.c_str());
    return ".";
}

bool VestaSiteImporter::extract_wp_config(
    const std::string& archive,
    const std::string& domain,
    const std::string& web_root_type,
    std::string& staging_dir,
    std::string& out_db_name,
    std::string& out_db_user,
    std::string& out_db_password,
    std::string& out_db_host,
    bool& out_parsed,
    bool& out_ambiguous) {

    out_parsed = false;
    out_ambiguous = false;

    // Extract domain_data.tar.gz to staging
    auto try_extract = [&](const std::string& prefix) -> bool {
        auto r = executor_.run({
            "tar", "-xf", archive, "-C", staging_dir, prefix
        });
        return r.exit_code == 0;
    };

    if (!try_extract("web/" + domain + "/domain_data.tar.gz") &&
        !try_extract("./web/" + domain + "/domain_data.tar.gz")) {
        return false;
    }

    std::string data_tarball = staging_dir + "/domain_data.tar.gz";
    executor_.run({
        "mv", staging_dir + "/web/" + domain + "/domain_data.tar.gz", data_tarball
    });
    executor_.run({"rm", "-rf", staging_dir + "/web"});

    // List inner tar to find wp-config.php candidates
    auto list_inner = executor_.run({"tar", "-tzf", data_tarball});
    if (list_inner.exit_code != 0) {
        std::remove(data_tarball.c_str());
        return false;
    }

    // Build candidate search paths
    std::vector<std::string> search_paths;
    if (!web_root_type.empty() && web_root_type != ".") {
        search_paths.push_back(web_root_type + "/wp-config.php");
        search_paths.push_back("./" + web_root_type + "/wp-config.php");
    }
    search_paths.push_back("wp-config.php");
    search_paths.push_back("./wp-config.php");

    // Collect all wp-config.php candidates from inner tar
    // Only accept entries whose basename is exactly "wp-config.php"
    std::vector<std::string> candidates;
    std::istringstream inner_stream(list_inner.out);
    std::string inner_line;
    while (std::getline(inner_stream, inner_line)) {
        if (inner_line.empty()) continue;
        // Remove trailing slash (directory marker)
        std::string check = inner_line;
        if (!check.empty() && check.back() == '/') check.pop_back();
        auto slash = check.rfind('/');
        std::string basename = (slash != std::string::npos) ? check.substr(slash + 1) : check;
        if (basename == "wp-config.php") {
            candidates.push_back(inner_line);
        }
    }

    if (candidates.empty()) {
        std::remove(data_tarball.c_str());
        return false;
    }

    // Prefer known web root paths; fall back to shortest path
    std::string chosen;
    for (const auto& sp : search_paths) {
        for (const auto& c : candidates) {
            if (c == sp) {
                chosen = c;
                break;
            }
        }
        if (!chosen.empty()) break;
    }
    if (chosen.empty()) {
        // Fallback: pick the shortest (most likely root)
        chosen = candidates.front();
        for (const auto& c : candidates) {
            if (c.size() < chosen.size()) chosen = c;
        }
    }

    // Extract the chosen wp-config.php to staging (may include subdirectory)
    std::string wp_extract_dir = staging_dir + "/wp_extract";
    ::mkdir(wp_extract_dir.c_str(), 0755);
    auto wp_result = executor_.run({
        "tar", "-xzf", data_tarball, "-C", wp_extract_dir, chosen
    });
    std::remove(data_tarball.c_str());

    if (wp_result.exit_code != 0) {
        return false;
    }

    // Find the extracted wp-config.php (it may be in a subdirectory)
    auto find_result = executor_.run({
        "find", wp_extract_dir, "-name", "wp-config.php", "-type", "f"
    });
    if (find_result.exit_code != 0 || find_result.out.empty()) {
        executor_.run({"rm", "-rf", wp_extract_dir});
        return false;
    }

    // Read the first found wp-config.php
    std::string wp_file_path;
    std::istringstream find_stream(find_result.out);
    std::getline(find_stream, wp_file_path);

    std::ifstream wp_file(wp_file_path);
    if (!wp_file.is_open()) {
        executor_.run({"rm", "-rf", wp_extract_dir});
        return false;
    }
    std::stringstream wp_buf;
    wp_buf << wp_file.rdbuf();
    std::string content = wp_buf.str();
    executor_.run({"rm", "-rf", wp_extract_dir});

    bool found_any = false;

    // Step 1: find the full define('DB_NAME', ...) expression
    // Capture the WHOLE expression content between ( and )
    // so we can check its second argument for ambiguity
    std::regex define_name_re(R"(define\s*\(\s*['\"]DB_NAME['\"]\s*,\s*(.*?)\))");
    std::smatch match;

    if (std::regex_search(content, match, define_name_re)) {
        found_any = true;
        std::string full_expr = match[0].str();
        std::string second_arg = match[1].str();

        // Extract the DB_NAME value using a quoted-string regex
        std::regex quote_re(R"(['\"]?([^'\",]+)['\"]?\s*\))");
        std::smatch qm;

        // The second_arg may be: 'simple_literal' or getenv(...) or $var etc.
        // Check for ambiguity: contains $, getenv, or _SERVER (not in a comment)
        bool has_dollar = second_arg.find('$') != std::string::npos;
        bool has_getenv = second_arg.find("getenv") != std::string::npos;
        bool has_server = second_arg.find("_SERVER") != std::string::npos;

        if (!has_dollar && !has_getenv && !has_server) {
            // Simple literal — extract the value
            std::regex simple_val_re(R"(['\"]([^'\"]+)['\"])");
            std::smatch sv;
            if (std::regex_search(second_arg, sv, simple_val_re)) {
                out_db_name = sv[1];
                out_parsed = true;
            }
        } else {
            // Ambiguous — but still try to find the last literal value
            std::regex last_val_re(R"(['\"]([^'\"]+?)['\"]\s*\))");
            std::smatch lv;
            if (std::regex_search(second_arg, lv, last_val_re)) {
                out_db_name = lv[1];
                // Even though we found a value, it might be a fallback/default
                // Mark as ambiguous so user can override with --database
            }
            out_ambiguous = true;
        }
    }

    // Parse DB_USER, DB_PASSWORD, DB_HOST similarly (simple pattern)
    auto extract_simple = [&](const std::string& name, std::string& out) {
        std::regex re(R"(define\s*\(\s*['\"])" + name + R"(['\"]\s*,\s*['\"]([^'\"]+)['\"])");
        std::smatch m;
        if (std::regex_search(content, m, re)) {
            out = m[1];
        }
    };

    extract_simple("DB_USER", out_db_user);
    extract_simple("DB_PASSWORD", out_db_password);
    extract_simple("DB_HOST", out_db_host);

    return found_any; // wp_config_found
}

bool VestaSiteImporter::find_db_in_archive(
    const std::vector<std::string>& entries,
    const std::string& db_name,
    std::string& out_dump_path,
    size_t& out_size,
    bool& size_known,
    std::string& out_type) {

    std::string target_prefix_dot = "./db/" + db_name + "/";
    std::string target_prefix = "db/" + db_name + "/";

    for (const auto& e : entries) {
        bool match = false;
        if (e.substr(0, target_prefix_dot.size()) == target_prefix_dot) match = true;
        if (!match && e.substr(0, target_prefix.size()) == target_prefix) match = true;
        if (match && e.size() > target_prefix.size()) {
            if (e.find(".sql.gz") != std::string::npos) {
                out_dump_path = e;
                out_size = 0;
                size_known = false;
                auto dot1 = e.rfind(".sql.gz");
                if (dot1 != std::string::npos) {
                    auto prev = e.rfind('.', dot1 - 1);
                    if (prev != std::string::npos) {
                        out_type = e.substr(prev + 1, dot1 - prev - 1);
                    }
                }
                return true;
            }
        }
    }
    return false;
}

std::string VestaSiteImporter::normalize_db_name(const std::string& raw) const {
    std::string result = raw;
    auto underscore = result.find('_');
    if (underscore != std::string::npos && underscore < 20) {
        std::string prefix = result.substr(0, underscore);
        bool looks_like_prefix = true;
        for (char c : prefix) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                looks_like_prefix = false;
                break;
            }
        }
        if (looks_like_prefix && prefix.size() <= 15) {
            result = result.substr(underscore + 1);
        }
    }
    return result;
}

std::string VestaSiteImporter::make_staging_dir() {
    char tmpl[] = "/tmp/containercp-migrate-XXXXXX";
    char* dir = ::mkdtemp(tmpl);
    if (!dir) return "";
    return std::string(dir);
}

void VestaSiteImporter::cleanup_staging(const std::string& dir) {
    if (dir.empty()) return;
    executor_.run({"rm", "-rf", dir});
}

Manifest VestaSiteImporter::inspect(const Options& opts) {
    Manifest m;
    m.domain = opts.domain;
    m.backup_path = opts.backup_path;

    if (!fs_.exists(opts.backup_path)) {
        m.errors.push_back("Backup file not found: " + opts.backup_path);
        return m;
    }

    struct stat st;
    if (::stat(opts.backup_path.c_str(), &st) == 0) {
        m.archive_size = st.st_size;
    }

    std::vector<std::string> entries;
    std::string error;
    if (!tar_safe_list(opts.backup_path, entries, error)) {
        m.errors.push_back("Failed to read archive: " + error);
        return m;
    }

    auto df_result = executor_.run({"df", "--output=avail", cfg_.data_root()});
    if (df_result.exit_code == 0) {
        std::istringstream ss(df_result.out);
        std::string line;
        std::getline(ss, line);
        std::getline(ss, line);
        if (!line.empty()) {
            try { m.available_disk_mb = std::stoull(line) / 1024; }
            catch (...) {}
        }
    }

    bool size_known = false;
    std::string web_archive_path;
    if (!find_domain_in_archive(entries, opts.domain, web_archive_path,
                                 m.web_size, size_known)) {
        m.errors.push_back("Domain '" + opts.domain + "' not found in backup archive");
        return m;
    }
    m.domain_found = true;
    m.web_archive_path = web_archive_path;
    m.web_size_known = size_known;

    std::string staging = make_staging_dir();
    if (staging.empty()) {
        m.errors.push_back("Failed to create staging directory");
        return m;
    }
    m.web_root_type = detect_web_root(staging, opts.backup_path, opts.domain);
    cleanup_staging(staging);

    if (m.web_root_type.empty()) {
        m.errors.push_back("Failed to extract web archive");
        return m;
    }

    staging = make_staging_dir();
    if (staging.empty()) {
        m.errors.push_back("Failed to create staging directory");
        return m;
    }

    std::string wp_db_pass;
    bool parsed = false, ambiguous = false;
    if (extract_wp_config(opts.backup_path, opts.domain, m.web_root_type,
                          staging, m.wp_db_name, m.wp_db_user,
                          wp_db_pass, m.wp_db_host, parsed, ambiguous)) {
        m.wp_config_found = true;
    }
    m.wp_config_parsed = parsed;
    m.wp_db_ambiguous = ambiguous;
    cleanup_staging(staging);

    // Check site existence via SiteManager, DomainManager, and filesystem
    m.site_exists = false;
    if (sites_ && sites_->find(opts.domain) != nullptr) {
        m.site_exists = true;
    }
    if (!m.site_exists && domains_ && domains_->find(opts.domain) != nullptr) {
        m.site_exists = true;
    }
    if (!m.site_exists) {
        std::string site_dir = cfg_.sites_dir() + opts.domain + "/";
        m.site_exists = fs_.exists(site_dir);
    }

    // Find database
    bool should_search_db = (m.wp_config_found || m.wp_config_parsed || !opts.database.empty()) && !opts.skip_db;
    if (should_search_db) {
        std::string db_to_find = opts.database;
        if (db_to_find.empty() && m.wp_config_parsed && !m.wp_db_ambiguous) {
            db_to_find = normalize_db_name(m.wp_db_name);
        }
        if (db_to_find.empty() && !opts.database.empty()) {
            db_to_find = opts.database;
        }

        for (const auto& e : entries) {
            std::string db_check;
            if (e.substr(0, 5) == "./db/") db_check = e.substr(5);
            else if (e.substr(0, 3) == "db/") db_check = e.substr(3);
            if (!db_check.empty()) {
                auto slash = db_check.find('/');
                if (slash != std::string::npos) {
                    std::string db = db_check.substr(0, slash);
                    if (std::find(m.all_databases.begin(),
                                  m.all_databases.end(), db) == m.all_databases.end()) {
                        m.all_databases.push_back(db);
                    }
                }
            }
        }

        if (!db_to_find.empty()) {
            bool dump_size_known = false;
            find_db_in_archive(entries, db_to_find,
                               m.db_dump_path, m.db_dump_size,
                               dump_size_known, m.db_type);
            m.db_dump_size_known = dump_size_known;
        }

        if (!m.db_dump_path.empty()) {
            m.db_dump_found = true;
        } else if (!db_to_find.empty()) {
            // Try original name
            if (m.wp_config_parsed && !m.wp_db_name.empty()) {
                bool dump_size_known = false;
                find_db_in_archive(entries, m.wp_db_name,
                                   m.db_dump_path, m.db_dump_size,
                                   dump_size_known, m.db_type);
                m.db_dump_size_known = dump_size_known;
            }
        }

        if (m.db_dump_path.empty()) {
            m.warnings.push_back("SQL dump not found for database '" + db_to_find + "'");
        } else {
            m.db_dump_found = true;
        }
    }

    if (m.wp_config_found && ambiguous) {
        m.warnings.push_back("DB_NAME in wp-config.php uses a variable. "
                            "Use --database to specify manually.");
    }

    if (m.wp_config_found && !m.wp_config_parsed && m.warnings.empty()) {
        m.warnings.push_back("wp-config.php found but DB_NAME could not be parsed");
    }

    return m;
}

std::string VestaSiteImporter::format_dry_run(const Manifest& m, const Options& opts) {
    std::ostringstream out;

    out << "MyVestaCP → ContainerCP Site Import (DRY RUN)\n"
        << "=============================================\n"
        << "Backup file:      " << m.backup_path << "\n";

    if (m.archive_size > 0)
        out << "Archive size:     " << (m.archive_size / (1024*1024)) << " MB\n";
    else
        out << "Archive size:     unknown\n";

    out << "Domain:           " << m.domain << "\n"
        << "Owner:            " << opts.owner << "\n";

    if (!m.errors.empty()) {
        out << "\nERRORS:\n";
        for (const auto& e : m.errors)
            out << "  " << e << "\n";
        return out.str();
    }

    out << "\nDomain found:           " << (m.domain_found ? "yes" : "no");
    if (m.domain_found) {
        out << "\n  Web archive:          " << m.web_archive_path
            << "\n  Web root type:        " << m.web_root_type;
        if (m.web_size_known)
            out << "\n  Web files:            " << (m.web_size / 1024) << " KB";
        else
            out << "\n  Web files:            " << "unknown (available after extraction)";
    }

    out << "\nSite already exists:    " << (m.site_exists ? "YES (will abort)" : "no");

    out << "\nWordPress config:       "
        << (m.wp_config_found ? "found" : "not found");

    if (m.wp_config_found) {
        if (m.wp_config_parsed && !m.wp_db_ambiguous) {
            out << "\n  DB_NAME:             " << m.wp_db_name
                << "\n  DB_USER:             " << m.wp_db_user
                << "\n  DB_HOST:             " << m.wp_db_host
                << "\n  Normalized DB name:  " << normalize_db_name(m.wp_db_name);
        } else if (m.wp_db_ambiguous) {
            out << "\n  DB_NAME:             " << "(uses variable, ambiguous)";
        } else {
            out << "\n  DB_NAME:             " << "(could not parse)";
        }

        if (m.db_dump_found) {
            out << "\n  SQL dump:            " << m.db_dump_path;
            if (!m.db_type.empty())
                out << "\n  DB type:             " << m.db_type;
        } else if (m.wp_config_parsed && !m.wp_db_ambiguous) {
            out << "\n  SQL dump:            not found in archive";
        }
    }

    if (!m.all_databases.empty()) {
        out << "\n\nDatabases in archive:";
        for (const auto& db : m.all_databases)
            out << "\n  - " << db;
    }

    out << "\n\nDisk space:";
    if (m.available_disk_mb > 0)
        out << "\n  Available:          " << m.available_disk_mb << " MB";
    else
        out << "\n  Available:          unknown";

    if (m.site_exists) {
        out << "\n\nSite already exists. Will abort — overwrite not supported in v1.";
    }

    if (!m.warnings.empty()) {
        out << "\n\nWarnings:\n";
        for (const auto& w : m.warnings)
            out << "  " << w << "\n";
    }

    if (m.errors.empty() && !m.site_exists) {
        out << "\n\nWill do:";
        out << "\n  1. Create site record (SiteCreateOperation)";
        out << "\n  2. Create Docker stack (web, php, mariadb, redis)";
        out << "\n  3. Import web files";
        if (m.wp_config_found && m.wp_config_parsed && m.db_dump_found) {
            out << "\n  4. Import SQL dump into existing database";
            out << "\n  5. Update wp-config.php with new credentials";
            out << "\n  6. Restart web+php";
        }
        out << "\n  7. Health check";
    }

    out << "\n";
    return out.str();
}

// ── Safe tar member validation ──
static bool validate_inner_tar(const std::string& tarball,
                                std::vector<std::string>& errors) {
    runtime::CommandExecutor exec;
    auto list = exec.run({"tar", "-tzf", tarball});
    if (list.exit_code != 0) {
        errors.push_back("Cannot list inner archive");
        return false;
    }

    std::istringstream stream(list.out);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        if (line[0] == '/') { errors.push_back("Absolute path: " + line); return false; }
        std::istringstream cs(line);
        std::string part;
        while (std::getline(cs, part, '/')) {
            if (part == "..") { errors.push_back("Parent ref: " + line); return false; }
        }
    }
    return true;
}

bool VestaSiteImporter::extract_web_archive(
    const std::string& archive, const std::string& domain,
    const std::string& staging_dir,
    std::string& out_data_tarball) {

    auto try_extract = [&](const std::string& prefix) -> bool {
        auto r = executor_.run({"tar", "-xf", archive, "-C", staging_dir, prefix});
        return r.exit_code == 0;
    };

    if (!try_extract("web/" + domain + "/domain_data.tar.gz") &&
        !try_extract("./web/" + domain + "/domain_data.tar.gz")) {
        return false;
    }

    out_data_tarball = staging_dir + "/domain_data.tar.gz";
    executor_.run({"mv", staging_dir + "/web/" + domain + "/domain_data.tar.gz", out_data_tarball});
    executor_.run({"rm", "-rf", staging_dir + "/web"});
    return true;
}

bool VestaSiteImporter::copy_files_to_public(
    const std::string& staging_dir,
    const std::string& web_root_type,
    const std::string& site_dir,
    ImportFilesResult& result) {

    std::string data_tarball = staging_dir + "/domain_data.tar.gz";
    std::string public_dir = site_dir + "public/";

    // 1. Validate inner tar members BEFORE extraction
    if (!validate_inner_tar(data_tarball, result.errors)) return false;

    // 2. Safety-copy current public — mandatory, hard error if fails
    std::string safety_dir = staging_dir + "/safety_public";
    ::mkdir(safety_dir.c_str(), 0755);
    auto safety = executor_.run({"rsync", "-a", "--safe-links", public_dir, safety_dir + "/"});
    if (safety.exit_code != 0) {
        result.errors.push_back("Safety copy failed — aborting");
        return false;
    }

    // Rollback helper (used from multiple points)
    auto rollback_stage2 = [&]() -> bool {
        bool ok = true;
        executor_.run({"rm", "-rf", public_dir});
        ::mkdir(public_dir.c_str(), 0755);
        if (executor_.run({"rsync", "-a", "--safe-links", safety_dir + "/", public_dir}).exit_code != 0) ok = false;

        // Always try to restart web+php
        executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "up", "-d", "web", "php"});
        return ok;
    };

    // 3. Stop web+php only (mariadb+redis stay running)
    if (executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "stop", "web", "php"}).exit_code != 0) {
        result.errors.push_back("Failed to stop web/php");
        return false;
    }

    // 4. Extract inner archive
    std::string extract_dir = staging_dir + "/extracted";
    ::mkdir(extract_dir.c_str(), 0755);
    if (executor_.run({"tar", "-xzf", data_tarball, "-C", extract_dir}).exit_code != 0) {
        result.errors.push_back("Extraction failed");
        rollback_stage2();
        return false;
    }

    // 5. Determine source path
    std::string source_path = extract_dir;
    if (!web_root_type.empty() && web_root_type != ".") source_path = extract_dir + "/" + web_root_type;

    struct stat st;
    if (::stat(source_path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        result.errors.push_back("Web root not found: " + source_path);
        rollback_stage2();
        return false;
    }
    char src_r[PATH_MAX], ext_r[PATH_MAX];
    if (!::realpath(source_path.c_str(), src_r) || !::realpath(extract_dir.c_str(), ext_r)) {
        result.errors.push_back("Path resolution failed");
        rollback_stage2();
        return false;
    }
    if (std::string(src_r).substr(0, strlen(ext_r)) != ext_r) {
        result.errors.push_back("Web root outside extract dir");
        rollback_stage2();
        return false;
    }

    // 6. Clear public contents (not the dir itself)
    if (executor_.run({"find", public_dir, "-mindepth", "1", "-maxdepth", "1", "-exec", "rm", "-rf", "{}", "+"}).exit_code != 0) {
        result.errors.push_back("Failed to clean public");
        rollback_stage2();
        return false;
    }

    // 7. rsync files
    if (executor_.run({"rsync", "-a", "--safe-links", source_path + "/", public_dir}).exit_code != 0) {
        result.errors.push_back("rsync failed");
        rollback_stage2();
        return false;
    }

    // 8. Fix ownership — use UID from web container
    auto uid_r = executor_.run({"sh", "-c", "docker compose -f " + site_dir + "docker-compose.yml exec -T web stat -c %u /var/www/html 2>/dev/null || echo 1000"});
    std::string uid_str = "1000";
    if (uid_r.exit_code == 0) {
        std::string u = uid_r.out;
        while (!u.empty() && (u.back() == '\n' || u.back() == '\r')) u.pop_back();
        if (!u.empty() && u.find_first_not_of("0123456789") == std::string::npos) uid_str = u;
    }
    if (executor_.run({"docker", "run", "--rm", "-v", public_dir + ":/t:rw", "alpine", "chown", "-R", uid_str + ":" + uid_str, "/t"}).exit_code != 0) {
        result.errors.push_back("Ownership fix failed");
        rollback_stage2();
        return false;
    }

    // 9. Start web+php
    if (executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "up", "-d", "web", "php"}).exit_code != 0) {
        result.errors.push_back("Failed to start web/php");
        rollback_stage2();
        return false;
    }

    // 10. Health check with timeout
    bool healthy = false;
    for (int i = 0; i < 15; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto ps = executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "ps", "--format={{.Status}}", "web"});
        auto ps_php = executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "ps", "--format={{.Status}}", "php"});
        if (ps.exit_code == 0 && ps_php.exit_code == 0 &&
            ps.out.find("healthy") != std::string::npos &&
            ps_php.out.find("healthy") != std::string::npos) {
            healthy = true;
            break;
        }
    }
    if (!healthy) {
        result.errors.push_back("Health check timeout — rolling back");
        rollback_stage2();
        return false;
    }

    // 11. Count files
    try {
        for (const auto& e : std::filesystem::recursive_directory_iterator(public_dir)) {
            if (e.is_regular_file()) { result.files_count++; result.bytes_copied += e.file_size(); }
        }
    } catch (...) {}

    result.web_root_type = web_root_type;
    result.success = true;
    return true;
}

VestaSiteImporter::ImportFilesResult VestaSiteImporter::import_files(const Options& opts) {
    ImportFilesResult result;

    Manifest m = inspect(opts);
    if (!m.errors.empty()) { result.errors = m.errors; return result; }

    std::string site_dir = cfg_.sites_dir() + opts.domain + "/";
    if (!fs_.exists(site_dir + "docker-compose.yml")) {
        result.errors.push_back("Site not found. Run Stage 1 first.");
        return result;
    }

    std::string staging = make_staging_dir();
    if (staging.empty()) { result.errors.push_back("Cannot create staging"); return result; }

    auto cleanup = [&]() { if (!opts.keep_staging) cleanup_staging(staging); };

    std::string data_tarball;
    if (!extract_web_archive(opts.backup_path, opts.domain, staging, data_tarball)) {
        result.errors.push_back("Failed to extract web archive");
        cleanup(); return result;
    }

    bool ok = copy_files_to_public(staging, m.web_root_type, site_dir, result);
    cleanup();
    if (!ok) return result;

    result.success = true;
    return result;
}

} // namespace containercp::migration
