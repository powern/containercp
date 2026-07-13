#include "VestaSiteImporter.h"

#include <algorithm>
#include <cerrno>
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
                                     logger::Logger& logger,
                                     site::SiteManager* sites,
                                     domain::DomainManager* domains)
    : executor_(executor)
    , fs_(fs)
    , cfg_(cfg)
    , logger_(logger)
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

// ── MigrationMarker: centralized JSON parser for .containercp-migration.json ──
struct MigrationMarker {
    uint64_t version = 0;
    std::string domain;
    std::string owner;
    uint64_t site_id = 0;
    uint64_t stage = 0;
    bool files_pending = false;
    bool files_imported = false;
    bool sql_pending = false;

    static MigrationMarker parse(const std::string& content) {
        MigrationMarker m;
        auto gs = [&](const std::string& key) -> std::string {
            std::string pats[] = {"\"" + key + "\":\"", "\"" + key + "\": \"", "\"" + key + "\" :\"", "\"" + key + "\" : \""};
            for (auto& p : pats) {
                auto pos = content.find(p); if (pos == std::string::npos) continue;
                auto s = pos + p.size(), e = content.find('"', s); if (e == std::string::npos) continue;
                return content.substr(s, e - s);
            }
            return "";
        };
        auto gi = [&](const std::string& key) -> uint64_t {
            std::string pats[] = {"\"" + key + "\":", "\"" + key + "\": ", "\"" + key + "\" :", "\"" + key + "\" : "};
            for (auto& p : pats) {
                auto pos = content.find(p); if (pos == std::string::npos) continue;
                auto s = pos + p.size(); while (s < content.size() && content[s] == ' ') ++s;
                if (s >= content.size() || !std::isdigit((unsigned char)content[s])) continue;
                size_t e = s; while (e < content.size() && std::isdigit((unsigned char)content[e])) ++e;
                try { return std::stoull(content.substr(s, e - s)); } catch (...) { return 0; }
            }
            return 0;
        };
        auto gb = [&](const std::string& key) -> bool {
            std::string pats[] = {"\"" + key + "\":true", "\"" + key + "\": true", "\"" + key + "\" :true", "\"" + key + "\" : true", "\"" + key + "\":\"true\"", "\"" + key + "\": \"true\""};
            for (auto& p : pats) { if (content.find(p) != std::string::npos) return true; }
            return false;
        };
        m.domain = gs("domain"); m.owner = gs("owner"); m.site_id = gi("site_id");
        m.stage = gi("stage"); m.version = gi("version");
        m.files_pending = gb("files_pending"); m.files_imported = gb("files_imported"); m.sql_pending = gb("sql_pending");
        if (m.stage == 1) {
            if (!gb("files_pending") && content.find("\"files_pending\"") == std::string::npos) m.files_pending = true;
            if (content.find("\"sql_pending\"") == std::string::npos) m.sql_pending = true;
        }
        return m;
    }
    std::string to_json() const {
        return "{\"version\":" + std::to_string(version > 0 ? version : 1)
            + ",\"domain\":\"" + domain + "\",\"owner\":\"" + owner
            + "\",\"site_id\":" + std::to_string(site_id)
            + ",\"stage\":" + std::to_string(stage)
            + ",\"files_pending\":" + (files_pending ? "true" : "false")
            + ",\"files_imported\":" + (files_imported ? "true" : "false")
            + ",\"sql_pending\":" + (sql_pending ? "true" : "false") + "}";
    }
};

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
    site::Site* found_site = nullptr;
    if (sites_) found_site = sites_->find(opts.domain);
    if (found_site != nullptr) {
        m.site_exists = true;
    }
    if (!m.site_exists && domains_ && domains_->find(opts.domain) != nullptr) {
        m.site_exists = true;
    }
    if (!m.site_exists) {
        std::string site_dir = cfg_.sites_dir() + opts.domain + "/";
        m.site_exists = fs_.exists(site_dir);
    }

    // Migration marker validation
    if (m.site_exists) {
        std::string marker_path = cfg_.sites_dir() + opts.domain + "/.containercp-migration.json";
        if (fs_.exists(marker_path)) {
            std::string content = fs_.read_file(marker_path);
            m.migration_marker_found = true;

            // Use centralized MigrationMarker parser
            MigrationMarker marker = MigrationMarker::parse(content);
            std::string m_domain = marker.domain;
            std::string m_owner = marker.owner;
            uint64_t m_site_id = marker.site_id;
            m.migration_stage = marker.stage;
            m.migration_site_id = m_site_id;
            m.migration_owner = m_owner;
            m.files_pending = marker.files_pending;
            m.files_imported = marker.files_imported;
            m.sql_pending = marker.sql_pending;
            m.files_status = m.files_imported ? "imported" : (m.files_pending ? "pending" : "unknown");
            m.sql_status = m.sql_pending ? "pending" : "imported";

            // Validate against SiteManager records — require real SiteRecord
            if (!found_site) {
                m.marker_error = "SiteRecord not found for domain";
            } else if (!sites_) {
                m.marker_error = "SiteManager not available";
            } else if (m_site_id == 0) {
                m.marker_error = "Marker has no site_id (re-run Stage 1)";
            } else if (m_site_id != found_site->id) {
                m.marker_error = "Marker site_id " + std::to_string(m_site_id)
                    + " != SiteRecord id " + std::to_string(found_site->id);
            } else if (m_domain != opts.domain) {
                m.marker_error = "Marker domain mismatch";
            } else if (m_owner != opts.owner) {
                m.marker_error = "Marker owner mismatch";
            } else if (m.migration_stage != 1) {
                m.marker_error = "Marker stage is " + std::to_string(m.migration_stage) + ", expected 1";
            } else if (m.files_imported) {
                m.marker_error = "Files already imported (stage 2)";
            } else if (!m.files_pending) {
                m.marker_error = "Marker files_pending is false";
            } else {
                // Also verify DomainRecord belongs to same site_id
                bool domain_ok = true;
                if (domains_) {
                    auto* dom_rec = domains_->find(opts.domain);
                    if (dom_rec && dom_rec->site_id != found_site->id) {
                        domain_ok = false;
                        m.marker_error = "DomainRecord site_id mismatch";
                    }
                }
                if (domain_ok) {
                    m.migration_ready_for_files = true;
                }
            }
        }
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

// ── Full tar entry validation with type metadata ──
// Uses tar -tvzf to get type: regular file, dir, symlink →
// validate targets and reject devices/FIFO/socket
static bool validate_inner_tar_verbose(const std::string& tarball,
                                        std::vector<std::string>& errors) {
    runtime::CommandExecutor exec;
    // tar -tvzf outputs: permissions user group size date name -> target (for symlinks)
    auto list = exec.run({"tar", "-tvzf", tarball});
    if (list.exit_code != 0) {
        errors.push_back("Cannot list archive");
        return false;
    }

    std::istringstream stream(list.out);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        // Parse first char of permissions: l=symlink, d=dir, b=block, c=char, p=FIFO, s=socket
        char type = ' '; // space = regular file
        if (line.size() > 1 && line[0] != ' ') {
            // Find the permissions field (first column)
            // Format: "drwxr-xr-x" or "lrwxrwxrwx" or "brw-r--r--" etc
            std::string perm = line.substr(0, 10);
            type = perm[0];
        }

        if (type == 'b' || type == 'c') {
            errors.push_back("Archive contains device entry: " + line);
            return false;
        }
        if (type == 'p') {
            errors.push_back("Archive contains FIFO entry: " + line);
            return false;
        }
        if (type == 's') {
            errors.push_back("Archive contains socket entry: " + line);
            return false;
        }

        // Find the path in the line (starts after permissions, size, date)
        // Format: perms size date time path -> target (for symlinks)
        // Simple heuristic: last space-separated token before "->" is the path
        size_t arrow_pos = line.find(" -> ");
        std::string entry_path, symlink_target;

        std::string raw_path;
        if (arrow_pos != std::string::npos) {
            // Has symlink target
            raw_path = line.substr(0, arrow_pos);
            symlink_target = line.substr(arrow_pos + 4);
            // Trim trailing spaces
            while (!raw_path.empty() && raw_path.back() == ' ') raw_path.pop_back();
        } else {
            raw_path = line;
        }

        // Extract the path (last field after spaces)
        auto last_space = raw_path.rfind(' ');
        if (last_space != std::string::npos) {
            entry_path = raw_path.substr(last_space + 1);
        } else {
            entry_path = raw_path;
        }

        // Remove trailing / for directories
        if (!entry_path.empty() && entry_path.back() == '/') entry_path.pop_back();

        if (entry_path.empty()) continue;

        // Reject absolute paths
        if (entry_path[0] == '/') {
            errors.push_back("Archive contains absolute path: " + entry_path);
            return false;
        }

        // Reject parent directory references
        std::istringstream cs(entry_path);
        std::string part;
        while (std::getline(cs, part, '/')) {
            if (part == "..") {
                errors.push_back("Archive contains parent directory reference: " + entry_path);
                return false;
            }
        }

        // For symlinks, validate target
        if (type == 'l') {
            if (symlink_target.empty()) {
                errors.push_back("Archive contains symlink with empty target: " + entry_path);
                return false;
            }
            if (symlink_target[0] == '/') {
                errors.push_back("Archive contains symlink with absolute target: " + entry_path + " -> " + symlink_target);
                return false;
            }
            // Check for .. in symlink target components
            std::istringstream tgt_cs(symlink_target);
            std::string tgt_part;
            while (std::getline(tgt_cs, tgt_part, '/')) {
                if (tgt_part == "..") {
                    errors.push_back("Archive contains symlink with parent reference: " + entry_path + " -> " + symlink_target);
                    return false;
                }
            }
        }
    }
    return true;
}

// Post-extraction symlink check: verify all symlink targets resolve inside webroot
static bool check_symlinks_after_extraction(const std::string& web_root,
                                            std::vector<std::string>& errors) {
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(web_root,
            std::filesystem::directory_options::skip_permission_denied)) {

            auto st = std::filesystem::symlink_status(entry);
            if (st.type() != std::filesystem::file_type::symlink) continue;

            if (!std::filesystem::exists(entry)) {
                errors.push_back("Broken symlink: " + entry.path().string());
                return false;
            }

            char target_real[PATH_MAX];
            if (!::realpath(entry.path().c_str(), target_real)) {
                errors.push_back("Cannot resolve symlink: " + entry.path().string());
                return false;
            }

            char web_root_real[PATH_MAX];
            if (!::realpath(web_root.c_str(), web_root_real)) {
                errors.push_back("Cannot resolve web root: " + web_root);
                return false;
            }

            std::string tgt(target_real);
            std::string wr(web_root_real);
            // Check target is inside web root
            if (tgt != wr && tgt.substr(0, wr.size() + 1) != wr + "/") {
                errors.push_back("Symlink escapes web root: " + entry.path().string() + " -> " + tgt);
                return false;
            }
        }
    } catch (const std::exception& e) {
        errors.push_back("Symlink check error: " + std::string(e.what()));
        return false;
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
    ImportFilesResult& result,
    const std::string& uid_str, const std::string& gid_str) {

    std::string data_tarball = staging_dir + "/domain_data.tar.gz";
    std::string public_dir = site_dir + "public/";

    logger_.info("MIGRATION", "Validating inner tar members");
    if (!validate_inner_tar_verbose(data_tarball, result.errors)) {
        logger_.error("MIGRATION", "Inner tar validation failed");
        for (const auto& e : result.errors) logger_.error("MIGRATION", "  " + e);
        return false;
    }
    logger_.info("MIGRATION", "Inner tar validation passed");

    // 2. Safety-copy current public — mandatory, hard error if fails
    std::string safety_dir = staging_dir + "/safety_public";
    logger_.info("MIGRATION", "Creating safety backup");
    logger_.info("MIGRATION", "  source=" + public_dir + " -> destination=" + safety_dir);
    ::mkdir(safety_dir.c_str(), 0755);
    auto safety = executor_.run({"rsync", "-a", "--safe-links", public_dir, safety_dir + "/"});
    if (safety.exit_code != 0) {
        std::string err = "rsync exit code " + std::to_string(safety.exit_code) + " stderr=" + safety.err;
        logger_.error("MIGRATION", "Safety copy failed: " + err);
        result.errors.push_back("Safety copy failed: " + err);
        return false;
    }
    logger_.info("MIGRATION", "Safety backup created");

    // Rollback helper with logging
    auto rollback_stage2 = [&]() -> bool {
        logger_.warning("MIGRATION", "Rollback: restoring safety copy");
        bool ok = true;
        executor_.run({"rm", "-rf", public_dir});
        ::mkdir(public_dir.c_str(), 0755);
        auto rst = executor_.run({"rsync", "-a", "--safe-links", safety_dir + "/", public_dir});
        if (rst.exit_code != 0) {
            logger_.error("MIGRATION", "Rollback rsync failed: exit=" + std::to_string(rst.exit_code));
            ok = false;
        }
        auto up = executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "up", "-d", "web", "php"});
        if (up.exit_code != 0) logger_.error("MIGRATION", "Rollback docker compose up failed: " + up.err);
        logger_.info("MIGRATION", "Rollback completed");
        return ok;
    };

    // 3. Stop web+php only
    logger_.info("MIGRATION", "Stopping web+php containers");
    auto stop_res = executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "stop", "web", "php"});
    if (stop_res.exit_code != 0) {
        std::string err = "exit=" + std::to_string(stop_res.exit_code) + " " + stop_res.err;
        logger_.error("MIGRATION", "Failed to stop web/php: " + err);
        result.errors.push_back("Failed to stop web/php: " + err);
        return false;
    }
    logger_.info("MIGRATION", "Web+php stopped");

    // 4. Extract inner archive
    std::string extract_dir = staging_dir + "/extracted";
    logger_.info("MIGRATION", "Extracting archive to " + extract_dir);
    ::mkdir(extract_dir.c_str(), 0755);
    auto ext_res = executor_.run({"tar", "-xzf", data_tarball, "-C", extract_dir});
    if (ext_res.exit_code != 0) {
        std::string err = "tar exit=" + std::to_string(ext_res.exit_code) + " " + ext_res.err;
        logger_.error("MIGRATION", "Extraction failed: " + err);
        result.errors.push_back("Extraction failed: " + err);
        rollback_stage2();
        return false;
    }
    logger_.info("MIGRATION", "Archive extracted");

    // 5. Post-extraction symlink check
    logger_.info("MIGRATION", "Checking symlinks after extraction");
    if (!check_symlinks_after_extraction(extract_dir, result.errors)) {
        for (const auto& e : result.errors) logger_.error("MIGRATION", "Symlink error: " + e);
        rollback_stage2();
        return false;
    }
    logger_.info("MIGRATION", "Symlink check passed");

    // 6. Determine source path
    std::string source_path = extract_dir;
    if (!web_root_type.empty() && web_root_type != ".") source_path = extract_dir + "/" + web_root_type;
    logger_.info("MIGRATION", "Source path: " + source_path + " (web_root_type=" + web_root_type + ")");

    struct stat st;
    if (::stat(source_path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        std::string err = "Web root not found: " + source_path + " errno=" + std::to_string(errno) + " " + std::strerror(errno);
        logger_.error("MIGRATION", err);
        result.errors.push_back("Web root not found: " + source_path);
        rollback_stage2();
        return false;
    }

    // 7. Canonical prefix check (sibling-safe)
    char src_r[PATH_MAX], ext_r[PATH_MAX];
    if (!::realpath(source_path.c_str(), src_r) || !::realpath(extract_dir.c_str(), ext_r)) {
        std::string err = "realpath failed: " + std::string(std::strerror(errno));
        logger_.error("MIGRATION", err);
        result.errors.push_back("Path resolution failed");
        rollback_stage2();
        return false;
    }
    std::string src_str(src_r), ext_str(ext_r);
    if (src_str != ext_str && (src_str.size() <= ext_str.size() ||
        src_str.substr(0, ext_str.size() + 1) != ext_str + "/")) {
        logger_.error("MIGRATION", "Sibling path rejected: " + src_str + " outside " + ext_str);
        result.errors.push_back("Web root outside extract dir (sibling rejected)");
        rollback_stage2();
        return false;
    }
    logger_.info("MIGRATION", "Canonical path OK: " + src_str);

    // 8. Clear public contents
    logger_.info("MIGRATION", "Cleaning public directory: " + public_dir);
    auto clean_res = executor_.run({"find", public_dir, "-mindepth", "1", "-maxdepth", "1", "-exec", "rm", "-rf", "{}", "+"});
    if (clean_res.exit_code != 0) {
        std::string err = "find/rm exit=" + std::to_string(clean_res.exit_code) + " " + clean_res.err;
        logger_.error("MIGRATION", "Failed to clean public: " + err);
        result.errors.push_back("Failed to clean public: " + err);
        rollback_stage2();
        return false;
    }
    logger_.info("MIGRATION", "Public directory cleaned");

    // 9. rsync files
    logger_.info("MIGRATION", "Copying files via rsync");
    logger_.info("MIGRATION", "  source=" + source_path + "/");
    logger_.info("MIGRATION", "  dest=" + public_dir);
    auto rsync_res = executor_.run({"rsync", "-a", "--safe-links", source_path + "/", public_dir});
    if (rsync_res.exit_code != 0) {
        std::string err = "rsync exit=" + std::to_string(rsync_res.exit_code) + " " + rsync_res.err;
        logger_.error("MIGRATION", "rsync failed: " + err);
        result.errors.push_back("rsync failed: " + err);
        rollback_stage2();
        return false;
    }
    logger_.info("MIGRATION", "Files copied successfully");

    // 10. Fix ownership
    logger_.info("MIGRATION", "Fixing ownership: " + uid_str + ":" + gid_str);
    auto chown_res = executor_.run({"docker", "run", "--rm", "-v", public_dir + ":/t:rw", "alpine",
                                     "chown", "-R", uid_str + ":" + gid_str, "/t"});
    if (chown_res.exit_code != 0) {
        std::string err = "chown exit=" + std::to_string(chown_res.exit_code) + " " + chown_res.err;
        logger_.error("MIGRATION", "Ownership fix failed: " + err);
        result.errors.push_back("Ownership fix failed: " + err);
        rollback_stage2();
        return false;
    }

    // 11. Start web+php
    logger_.info("MIGRATION", "Starting web+php containers");
    auto up_res = executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "up", "-d", "web", "php"});
    if (up_res.exit_code != 0) {
        std::string err = "docker compose exit=" + std::to_string(up_res.exit_code) + " " + up_res.err;
        logger_.error("MIGRATION", "Failed to start web/php: " + err);
        result.errors.push_back("Failed to start web/php: " + err);
        rollback_stage2();
        return false;
    }
    logger_.info("MIGRATION", "Web+php started, waiting for health...");

    // 12. Health check with timeout
    bool healthy = false;
    for (int i = 0; i < 15; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto ps = executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml",
                                "ps", "--format={{.Status}}", "web"});
        auto ps_php = executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml",
                                    "ps", "--format={{.Status}}", "php"});
        bool web_ok = ps.exit_code == 0 && ps.out.find("healthy") != std::string::npos;
        bool php_ok = ps_php.exit_code == 0 && ps_php.out.find("healthy") != std::string::npos;
        if (web_ok && php_ok) {
            healthy = true;
            logger_.info("MIGRATION", "Health check passed on attempt " + std::to_string(i + 1));
            break;
        }
        if (i == 7 || i == 14) {
            logger_.info("MIGRATION", "Health check attempt " + std::to_string(i + 1) + ": web=" + (web_ok ? "ok" : "waiting") + " php=" + (php_ok ? "ok" : "waiting"));
        }
    }
    if (!healthy) {
        logger_.error("MIGRATION", "Health check timeout after 15s");
        result.errors.push_back("Health check timeout — rolling back");
        rollback_stage2();
        return false;
    }

    // 13. Count files
    logger_.info("MIGRATION", "Counting files");
    try {
        for (const auto& e : std::filesystem::recursive_directory_iterator(public_dir)) {
            if (e.is_regular_file()) { result.files_count++; result.bytes_copied += e.file_size(); }
        }
    } catch (const std::filesystem::filesystem_error& ex) {
        logger_.warning("MIGRATION", "File count error: " + std::string(ex.what()));
    } catch (...) {
        logger_.warning("MIGRATION", "File count unknown error");
    }
    logger_.info("MIGRATION", "Files: " + std::to_string(result.files_count) + " (" + std::to_string(result.bytes_copied / 1024) + " KB)");

    result.web_root_type = web_root_type;
    result.success = true;
    logger_.info("MIGRATION", "copy_files_to_public completed successfully");
    return true;
}

VestaSiteImporter::ImportFilesResult VestaSiteImporter::import_files(const Options& opts) {
    ImportFilesResult result;

    logger_.info("MIGRATION", "Import files requested");
    logger_.info("MIGRATION", "  domain=" + opts.domain + " owner=" + opts.owner + " backup=" + opts.backup_path);

    Manifest m = inspect(opts);
    if (!m.errors.empty()) {
        logger_.error("MIGRATION", "Inspect failed: " + m.errors[0]);
        result.errors = m.errors; return result;
    }

    std::string site_dir = cfg_.sites_dir() + opts.domain + "/";
    std::string marker_path = site_dir + ".containercp-migration.json";

    logger_.info("MIGRATION", "Reading marker");
    if (!m.migration_ready_for_files) {
        std::string reason = !m.marker_error.empty() ? m.marker_error : "Migration not ready for file import. Run Stage 1 first.";
        logger_.error("MIGRATION", "Marker rejected: " + reason);
        result.errors.push_back(reason);
        return result;
    }
    logger_.info("MIGRATION", "Marker OK — site_id=" + std::to_string(m.migration_site_id) + " stage=" + std::to_string(m.migration_stage));
    logger_.info("MIGRATION", "  files_status=" + m.files_status + " sql_status=" + m.sql_status);

    // Determine UID/GID BEFORE stopping containers (while they're still running)
    logger_.info("MIGRATION", "Determining container UID/GID");
    std::string uid_str, gid_str;
    {
        auto uid_result = executor_.run({
            "docker", "compose", "-f", site_dir + "docker-compose.yml",
            "exec", "-T", "php", "stat", "-c", "%u:%g", "/var/www/html"
        });
        if (uid_result.exit_code != 0) {
            std::string err = "Cannot determine container UID/GID: exit=" + std::to_string(uid_result.exit_code) + " err=" + uid_result.err;
            logger_.error("MIGRATION", err);
            result.errors.push_back("Cannot determine container UID/GID. Is site running?");
            return result;
        }
        std::string uid_line = uid_result.out;
        while (!uid_line.empty() && (uid_line.back() == '\n' || uid_line.back() == '\r')) uid_line.pop_back();
        auto colon = uid_line.find(':');
        if (colon == std::string::npos) {
            logger_.error("MIGRATION", "Invalid UID:GID format: " + uid_line);
            result.errors.push_back("Invalid UID:GID format: " + uid_line);
            return result;
        }
        uid_str = uid_line.substr(0, colon);
        gid_str = uid_line.substr(colon + 1);
        if (uid_str.empty() || gid_str.empty()) {
            logger_.error("MIGRATION", "Empty UID or GID after parsing");
            result.errors.push_back("Empty UID or GID");
            return result;
        }
        logger_.info("MIGRATION", "UID=" + uid_str + " GID=" + gid_str);
    }

    std::string staging = make_staging_dir();
    if (staging.empty()) {
        logger_.error("MIGRATION", "Cannot create staging directory");
        result.errors.push_back("Cannot create staging"); return result;
    }
    logger_.info("MIGRATION", "Staging: " + staging);

    auto cleanup = [&]() { if (!opts.keep_staging) cleanup_staging(staging); };

    logger_.info("MIGRATION", "Extracting web archive from backup");
    std::string data_tarball;
    if (!extract_web_archive(opts.backup_path, opts.domain, staging, data_tarball)) {
        logger_.error("MIGRATION", "Failed to extract web archive from " + opts.backup_path);
        result.errors.push_back("Failed to extract web archive");
        cleanup(); return result;
    }
    logger_.info("MIGRATION", "Extracted to staging: " + data_tarball);

    logger_.info("MIGRATION", "Calling copy_files_to_public");
    bool ok = copy_files_to_public(staging, m.web_root_type, site_dir, result, uid_str, gid_str);
    cleanup();
    if (!ok) {
        logger_.error("MIGRATION", "copy_files_to_public failed");
        for (const auto& e : result.errors) logger_.error("MIGRATION", "  error: " + e);
        return result;
    }
    logger_.info("MIGRATION", "copy_files_to_public completed");

    // Update migration marker: stage 2 completed — preserve all identity fields
    logger_.info("MIGRATION", "Updating marker to stage 2");
    {
        MigrationMarker updated;
        updated.version = 1;
        updated.domain = opts.domain;
        updated.owner = opts.owner;
        updated.site_id = m.migration_site_id;
        updated.stage = 2;
        updated.files_pending = false;
        updated.files_imported = true;
        updated.sql_pending = true;

        std::string marker_content = updated.to_json();
        std::string tmp_path = marker_path + ".tmp";
        if (fs_.create_file(tmp_path, marker_content)) {
            if (std::rename(tmp_path.c_str(), marker_path.c_str()) != 0) {
                logger_.error("MIGRATION", "Failed to rename marker: " + std::string(std::strerror(errno)));
                result.errors.push_back("CRITICAL: Files imported but failed to rename migration marker");
                return result;
            }
        } else {
            logger_.error("MIGRATION", "Failed to write marker file");
            result.errors.push_back("CRITICAL: Files imported but failed to update migration marker");
            return result;
        }
    }
    logger_.info("MIGRATION", "Marker updated successfully");

    result.success = true;
    return result;
}

} // namespace containercp::migration
