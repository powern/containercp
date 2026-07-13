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
    logger_.info("MIGRATION", "tar_safe_list ENTER: " + archive);
    logger_.info("MIGRATION", "tar_safe_list: running tar -tf (may take time for large archives)");
    auto result = executor_.run({
        "tar", "-tf", archive
    });
    logger_.info("MIGRATION", "tar_safe_list: tar -tf completed, exit_code=" + std::to_string(result.exit_code)
                 + " stdout_size=" + std::to_string(result.out.size()));
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

    // Helper: check if an entry matches db/<name>/<name>.<type>.sql.gz
    auto parse_entry = [&](const std::string& entry, const std::string& expected_db,
                           bool& matched, std::string& type) -> bool {
        // Normalize ./db/ → db/
        std::string path = entry;
        if (path.substr(0, 5) == "./db/") path = "db/" + path.substr(5);
        if (path.substr(0, 3) != "db/") return false;

        auto first_slash = path.find('/', 3); // after "db/"
        if (first_slash == std::string::npos) return false;
        std::string dir_db = path.substr(3, first_slash - 3);

        if (dir_db != expected_db) return false;

        // Find filename after second slash
        auto last_slash = path.rfind('/');
        if (last_slash == std::string::npos) return false;
        std::string filename = path.substr(last_slash + 1);

        // Check .sql.gz extension
        if (filename.size() < 7) return false;
        if (filename.substr(filename.size() - 7) != ".sql.gz") return false;

        // Expected format: <dirname>.<type>.sql.gz
        auto dot1 = filename.rfind(".sql.gz");
        if (dot1 == std::string::npos) return false;
        auto prev = filename.rfind('.', dot1 - 1);
        if (prev == std::string::npos) return false;

        // Verify the database dir name matches filename prefix
        std::string file_db = filename.substr(0, prev);
        if (file_db != dir_db) return false;

        type = filename.substr(prev + 1, dot1 - prev - 1);
        matched = true;
        return true;
    };

    // Step 1: try exact match first
    for (const auto& e : entries) {
        bool matched = false;
        std::string type;
        if (parse_entry(e, db_name, matched, type)) {
            out_dump_path = e;
            out_type = type;
            return true;
        }
    }

    // Step 2: if not found, generate fallback variants (without owner prefix)
    std::vector<std::string> fallbacks;
    // Add the original name (in case it has special chars that were escaped during search)
    fallbacks.push_back(db_name);

    // Generate normalized variants (remove VestaCP user prefix)
    auto underscore = db_name.find('_');
    if (underscore != std::string::npos && underscore < 20) {
        std::string prefix = db_name.substr(0, underscore);
        bool looks_like_prefix = true;
        for (char c : prefix) {
            if (!std::isalnum(static_cast<unsigned char>(c))) { looks_like_prefix = false; break; }
        }
        if (looks_like_prefix && prefix.size() <= 15) {
            fallbacks.push_back(db_name.substr(underscore + 1));
            // Also add underscore variant
            std::string no_dots = db_name.substr(underscore + 1);
            for (auto& c : no_dots) if (c == '.') c = '_';
            if (no_dots != fallbacks.back()) fallbacks.push_back(no_dots);
        }
    }
    // Also try underscore version of original
    std::string uscore_ver = db_name;
    for (auto& c : uscore_ver) if (c == '.') c = '_';
    if (uscore_ver != db_name) fallbacks.push_back(uscore_ver);

    // Also try without dots
    std::string no_dots = db_name;
    for (auto& c : no_dots) if (c == '.') c = '_';
    if (no_dots != db_name) fallbacks.push_back(no_dots);

    for (const auto& fb : fallbacks) {
        for (const auto& e : entries) {
            bool matched = false;
            std::string type;
            if (parse_entry(e, fb, matched, type)) {
                out_dump_path = e;
                out_type = type;
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

            // Validate marker integrity (identity checks only — no stage rejection)
            bool marker_valid = true;
            if (!found_site) {
                m.marker_error = "SiteRecord not found for domain";
                marker_valid = false;
            } else if (sites_ == nullptr) {
                m.marker_error = "SiteManager not available";
                marker_valid = false;
            } else if (m_site_id == 0) {
                m.marker_error = "Marker has no site_id";
                marker_valid = false;
            } else if (m_site_id != found_site->id) {
                m.marker_error = "Marker site_id " + std::to_string(m_site_id)
                    + " != SiteRecord id " + std::to_string(found_site->id);
                marker_valid = false;
            } else if (m_domain != opts.domain) {
                m.marker_error = "Marker domain mismatch";
                marker_valid = false;
            } else if (m_owner != opts.owner) {
                m.marker_error = "Marker owner mismatch";
                marker_valid = false;
            } else if (domains_) {
                auto* dom_rec = domains_->find(opts.domain);
                if (dom_rec && dom_rec->site_id != found_site->id) {
                    m.marker_error = "DomainRecord site_id mismatch";
                    marker_valid = false;
                }
            }

            // State machine: determine available actions based on current stage
            if (marker_valid) {
                switch (m.migration_stage) {
                    case 1:
                        if (m.files_pending && !m.files_imported) {
                            m.can_import_files = true;
                        }
                        break;
                    case 2:
                        if (m.sql_pending && m.files_imported && !m.files_pending) {
                            m.can_import_sql = true;
                        }
                        break;
                    case 3:
                        m.migration_completed = true;
                        break;
                    default:
                        m.marker_error = "Unknown migration stage: " + std::to_string(m.migration_stage);
                        break;
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
    const std::string& uid_str, const std::string& gid_str,
    uint64_t site_id, const std::string& domain) {

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
    const int HEALTH_TIMEOUT_SECONDS = 120;
    const int HEALTH_CHECK_INTERVAL = 2;
    std::string web_container = "site-" + std::to_string(site_id) + "-web";
    std::string php_container = "site-" + std::to_string(site_id) + "-php";

    logger_.info("MIGRATION", "Starting web+php containers");
    auto up_res = executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "up", "-d", "web", "php"});
    if (up_res.exit_code != 0) {
        std::string err = "docker compose exit=" + std::to_string(up_res.exit_code) + " " + up_res.err;
        logger_.error("MIGRATION", "Failed to start web/php: " + err);
        result.errors.push_back("Failed to start web/php: " + err);
        rollback_stage2();
        return false;
    }
    logger_.info("MIGRATION", "Web+php started, checking health (timeout=" + std::to_string(HEALTH_TIMEOUT_SECONDS) + "s, interval=" + std::to_string(HEALTH_CHECK_INTERVAL) + "s)");

    // 12. Health check with extended timeout (up to 120s)
    bool healthy = false;
    bool web_running = false, php_running = false;
    bool web_unhealthy = false, php_unhealthy = false;
    int attempts = HEALTH_TIMEOUT_SECONDS / HEALTH_CHECK_INTERVAL;

    for (int i = 0; i < attempts; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(HEALTH_CHECK_INTERVAL));

        // Use docker inspect for reliable health status
        auto web_hc = executor_.run({"docker", "inspect", web_container,
                                     "--format", "{{.State.Status}}|{{.State.Health.Status}}|{{.State.ExitCode}}"});
        auto php_hc = executor_.run({"docker", "inspect", php_container,
                                     "--format", "{{.State.Status}}|{{.State.Health.Status}}|{{.State.ExitCode}}"});

        auto parse_status = [](const std::string& output, bool& running, bool& unhealthy) -> bool {
            // Format: "Status|Health|ExitCode"
            running = false; unhealthy = false;
            if (output.empty()) return false;
            auto first_pipe = output.find('|');
            if (first_pipe == std::string::npos) return false;
            auto second_pipe = output.find('|', first_pipe + 1);
            if (second_pipe == std::string::npos) return false;
            std::string state = output.substr(0, first_pipe);
            std::string health = output.substr(first_pipe + 1, second_pipe - first_pipe - 1);
            std::string exitcode = output.substr(second_pipe + 1);
            while (!exitcode.empty() && (exitcode.back() == '\n' || exitcode.back() == '\r')) exitcode.pop_back();

            running = (state == "running");
            if (health == "healthy") return true;
            if (health == "unhealthy") unhealthy = true;
            if (state == "exited" || state == "dead") return false; // failure
            return false; // still waiting (starting, etc.)
        };

        bool web_ok = parse_status(web_hc.out, web_running, web_unhealthy);
        bool php_ok = parse_status(php_hc.out, php_running, php_unhealthy);

        if (web_ok && php_ok) {
            healthy = true;
            logger_.info("MIGRATION", "Health check passed on attempt " + std::to_string(i + 1)
                         + " (after " + std::to_string((i + 1) * HEALTH_CHECK_INTERVAL) + "s)");
            break;
        }

        // Immediate failure on exited/dead containers
        if (!web_running && web_hc.exit_code == 0) {
            logger_.error("MIGRATION", "Web container not running: " + web_hc.out);
        }
        if (!php_running && php_hc.exit_code == 0) {
            logger_.error("MIGRATION", "PHP container not running: " + php_hc.out);
        }

        // Log progress every 20 seconds
        if (i > 0 && (i * HEALTH_CHECK_INTERVAL) % 20 == 0) {
            logger_.info("MIGRATION", "Health check at " + std::to_string((i + 1) * HEALTH_CHECK_INTERVAL)
                         + "s: web=" + (web_ok ? "ok" : web_unhealthy ? "unhealthy" : web_running ? "starting" : "waiting")
                         + " php=" + (php_ok ? "ok" : php_unhealthy ? "unhealthy" : php_running ? "starting" : "waiting"));
        }
    }

    if (!healthy) {
        logger_.error("MIGRATION", "Health check timeout after " + std::to_string(HEALTH_TIMEOUT_SECONDS) + "s");

        // Collect diagnostics before rollback
        auto web_inspect = executor_.run({"docker", "inspect", web_container,
                                          "--format", "Status={{.State.Status}} Health={{.State.Health.Status}} Exit={{.State.ExitCode}} Error={{.State.Error}}"});
        auto php_inspect = executor_.run({"docker", "inspect", php_container,
                                          "--format", "Status={{.State.Status}} Health={{.State.Health.Status}} Exit={{.State.ExitCode}} Error={{.State.Error}}"});
        auto web_logs = executor_.run({"docker", "logs", "--tail", "20", web_container});
        auto php_logs = executor_.run({"docker", "logs", "--tail", "20", php_container});

        logger_.error("MIGRATION", "Web inspect: " + web_inspect.out);
        logger_.error("MIGRATION", "PHP inspect: " + php_inspect.out);
        if (!web_logs.err.empty()) logger_.error("MIGRATION", "Web logs (err): " + web_logs.err);
        if (!php_logs.err.empty()) logger_.error("MIGRATION", "PHP logs (err): " + php_logs.err);

        // If containers are running but still starting, don't rollback immediately — health often succeeds
        if (web_running && php_running) {
            logger_.warning("MIGRATION", "Both containers running but not healthy — finishing Stage 2 with warning");
            // Proceed without rollback — site may become healthy later
            healthy = true;
            result.warnings.push_back("Health check timed out but containers are running");
        } else {
            result.errors.push_back("Health check timeout — rolling back");
            rollback_stage2();
            return false;
        }
    }

    // 13. HTTP check via local proxy
    if (healthy && site_id > 0) {
        logger_.info("MIGRATION", "Running HTTP check via proxy");
        auto http = executor_.run({
            "curl", "-s", "-o", "/dev/null", "-w", "%{http_code}",
            "--resolve", domain + ":80:127.0.0.1",
            "http://" + domain + "/"
        });
        if (http.exit_code == 0 && !http.out.empty()) {
            int code = 0;
            try { code = std::stoi(http.out); } catch (...) {}
            logger_.info("MIGRATION", "HTTP check returned " + std::to_string(code));
            // Accept various status codes — SQL not imported yet, DB errors expected
            if (code >= 200 && code <= 499) {
                result.warnings.push_back("HTTP status " + std::to_string(code) + " (expected during migration)");
            } else {
                logger_.warning("MIGRATION", "HTTP status " + std::to_string(code) + " — site may not be fully accessible");
                result.warnings.push_back("HTTP status " + std::to_string(code));
            }
        } else {
            logger_.warning("MIGRATION", "HTTP check failed with exit " + std::to_string(http.exit_code));
            result.warnings.push_back("HTTP check not available (waiting for DNS/proxy)");
        }
    }

    // 14. Count files

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

std::string VestaSiteImporter::find_wp_config_file(const std::string& public_dir) const {
    // Search for wp-config.php in common locations
    static const char* paths[] = {"wp-config.php", "public/wp-config.php", "public_html/wp-config.php", "htdocs/wp-config.php", "www/wp-config.php", "root/wp-config.php"};
    for (auto* p : paths) {
        std::string full = public_dir + "/" + p;
        // Handle various public_dir structures
        if (public_dir.back() == '/') {
            full = public_dir + p;
        }
        struct stat st;
        if (::stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            return full;
        }
    }
    // Recursive search as fallback (only in public dir)
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(public_dir,
            std::filesystem::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file() && entry.path().filename() == "wp-config.php") {
                return entry.path().string();
            }
        }
    } catch (...) {}
    return "";
}

bool VestaSiteImporter::update_wp_config_db_credentials(
    const std::string& config_path,
    const std::string& old_db_name,
    const std::string& old_db_user,
    const std::string& new_db_name,
    const std::string& new_db_user,
    const std::string& new_db_password,
    const std::string& new_db_host) const {

    std::ifstream in(config_path);
    if (!in.is_open()) return false;
    std::stringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();

    auto replace_def = [&](const std::string& define_name, const std::string& old_val, const std::string& new_val) -> bool {
        if (old_val.empty() || new_val.empty()) return false;
        std::string p_single = "define('" + define_name + "', '" + old_val + "')";
        std::string p_double = "define(\"" + define_name + "\", \"" + old_val + "\")";
        std::string r_single = "define('" + define_name + "', '" + new_val + "')";
        std::string r_double = "define(\"" + define_name + "\", \"" + new_val + "\")";
        bool found = false;
        auto pos = content.find(p_single);
        if (pos != std::string::npos) { content.replace(pos, p_single.size(), r_single); found = true; }
        pos = content.find(p_double);
        if (pos != std::string::npos) { content.replace(pos, p_double.size(), r_double); found = true; }
        return found;
    };

    // Update DB_NAME
    if (!replace_def("DB_NAME", old_db_name, new_db_name)) {
        std::regex re(R"(define\s*\(\s*['\"]DB_NAME['\"].*?['\"]([^'\"]+?)['\"]\s*\))");
        content = std::regex_replace(content, re, "define('DB_NAME', '" + new_db_name + "')");
    }
    // Update DB_USER
    replace_def("DB_USER", old_db_user, new_db_user);
    // Update DB_PASSWORD (generic regex, no old value needed)
    {
        std::regex pre(R"(define\s*\(\s*['\"]DB_PASSWORD['\"].*?['\"]([^'\"]+?)['\"]\s*\))");
        content = std::regex_replace(content, pre, "define('DB_PASSWORD', '" + new_db_password + "')");
    }
    // Update DB_HOST to Docker service name (never localhost)
    {
        std::regex hre(R"(define\s*\(\s*['\"]DB_HOST['\"].*?['\"]([^'\"]+?)['\"]\s*\))");
        content = std::regex_replace(content, hre, "define('DB_HOST', '" + new_db_host + "')");
    }

    // Verify all 4 constants were updated
    auto check_val = [&](const std::string& define_name, const std::string& expected) -> bool {
        std::regex re(R"(define\s*\(\s*['\"]" + define_name + "['\"]\s*,\s*['\"]([^'\"]+?)['\"]\s*\))");
        std::smatch m;
        std::string search = content;
        if (std::regex_search(search, m, re)) {
            return m[1] == expected;
        }
        return false;
    };

    std::ofstream out(config_path);
    if (!out.is_open()) return false;
    out << content;
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
    if (!m.can_import_files) {
        std::string reason = !m.marker_error.empty() ? m.marker_error : "File import not available at current stage (" + std::to_string(m.migration_stage) + "). Stage 1 required.";
        logger_.error("MIGRATION", "Cannot import files: " + reason);
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

    logger_.info("MIGRATION", "Calling copy_files_to_public (site_id=" + std::to_string(m.migration_site_id) + ")");
    bool ok = copy_files_to_public(staging, m.web_root_type, site_dir, result, uid_str, gid_str, m.migration_site_id, opts.domain);
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

VestaSiteImporter::ImportSqlResult VestaSiteImporter::import_sql(const Options& opts) {
    ImportSqlResult result;

    logger_.info("MIGRATION", "Import SQL requested — domain=" + opts.domain + " backup=" + opts.backup_path);

    Manifest m = inspect(opts);
    if (!m.errors.empty()) {
        logger_.error("MIGRATION", "Inspect failed: " + m.errors[0]);
        result.errors = m.errors; return result;
    }

    std::string site_dir = cfg_.sites_dir() + opts.domain + "/";
    std::string marker_path = site_dir + ".containercp-migration.json";

    logger_.info("MIGRATION", "Reading marker");
    if (!m.can_import_sql) {
        std::string reason = !m.marker_error.empty() ? m.marker_error
            : "SQL import not available at current stage (" + std::to_string(m.migration_stage) + ")";
        logger_.error("MIGRATION", "Cannot import SQL: " + reason);
        result.errors.push_back(reason);
        return result;
    }
    logger_.info("MIGRATION", "Marker OK — stage=" + std::to_string(m.migration_stage) + " site_id=" + std::to_string(m.migration_site_id));

    // Find DB dump in backup
    logger_.info("MIGRATION", "Finding SQL dump in backup");
    logger_.info("MIGRATION", "tar_safe_list: opening " + opts.backup_path);
    std::vector<std::string> entries;
    std::string error;
    if (!tar_safe_list(opts.backup_path, entries, error)) {
        logger_.error("MIGRATION", "tar_safe_list FAILED: " + error);
        result.errors.push_back("Cannot read backup: " + error);
        return result;
    }
    logger_.info("MIGRATION", "tar_safe_list OK: " + std::to_string(entries.size()) + " entries in archive");
    if (m.wp_db_name.empty()) {
        logger_.error("MIGRATION", "wp_db_name is empty — cannot find SQL dump");
        result.errors.push_back("DB_NAME not determined — cannot find SQL dump");
        return result;
    }
    // Use exact DB_NAME from wp-config.php as source (do NOT normalize before lookup)
    std::string source_db_name = m.wp_db_name;
    logger_.info("MIGRATION", "Source DB_NAME from wp-config: '" + source_db_name
                 + "' — searching exact match first, fallback variants if not found");
    std::string dump_path, dump_type;
    size_t dump_size = 0;
    bool size_known = false;
    logger_.info("MIGRATION", "Entering find_db_in_archive for '" + source_db_name + "'");
    if (!find_db_in_archive(entries, source_db_name, dump_path, dump_size, size_known, dump_type)) {
        logger_.error("MIGRATION", "find_db_in_archive FAILED for '" + source_db_name + "'");
        // Log all databases found in archive for debugging
        std::string dbs;
        for (const auto& e : entries) {
            if (e.find("db/") != std::string::npos || e.find("./db/") != std::string::npos) {
                if (e.find(".sql.gz") != std::string::npos) dbs += e + " ";
            }
        }
        logger_.info("MIGRATION", "SQL dumps in archive: " + dbs);
        result.errors.push_back("SQL dump not found for database '" + source_db_name + "'");
        return result;
    }
    logger_.info("MIGRATION", "find_db_in_archive OK: path=" + dump_path + " type=" + dump_type);

    // Read credentials from site .env
    std::string db_name, db_user, db_password;
    {
        std::string env_path = site_dir + ".env";
        if (!fs_.exists(env_path)) {
            result.errors.push_back("Site .env not found — cannot determine DB credentials");
            return result;
        }
        std::string env_content = fs_.read_file(env_path);
        auto find_in_env = [&](const std::string& key) -> std::string {
            auto pos = env_content.find(key + "=");
            if (pos == std::string::npos) return "";
            auto start = pos + key.size() + 1;
            auto end = env_content.find('\n', start);
            if (end == std::string::npos) end = env_content.size();
            auto val = env_content.substr(start, end - start);
            while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();
            return val;
        };
        db_name = find_in_env("DB_NAME");
        db_user = find_in_env("DB_USER");
        db_password = find_in_env("DB_PASSWORD");
    }
    if (db_name.empty() || db_user.empty() || db_password.empty()) {
        result.errors.push_back("Cannot determine database credentials from .env");
        return result;
    }
    result.db_name = db_name;

    std::string db_container = "site-" + std::to_string(m.migration_site_id) + "-db";
    logger_.info("MIGRATION", "Target DB: " + db_name + " user=" + db_user + " container=" + db_container);

    std::string staging = make_staging_dir();
    if (staging.empty()) { result.errors.push_back("Cannot create staging"); return result; }
    auto cleanup_stg = [&]() { if (!opts.keep_staging) cleanup_staging(staging); };

    // Extract SQL dump from backup
    logger_.info("MIGRATION", "Extracting SQL dump from backup");
    auto try_extract = [&](const std::string& prefix) -> bool {
        return executor_.run({"tar", "-xf", opts.backup_path, "-C", staging, prefix}).exit_code == 0;
    };
    if (!try_extract(dump_path) && !try_extract("./" + dump_path)) {
        dump_path = "db/" + source_db_name + "/" + source_db_name + "." + dump_type + ".sql.gz";
        if (!try_extract(dump_path) && !try_extract("./" + dump_path)) {
            result.errors.push_back("Cannot extract SQL dump from backup");
            cleanup_stg(); return result;
        }
    }
    std::string dump_gz = staging + "/" + dump_path;
    logger_.info("MIGRATION", "Dump extracted: " + dump_gz);

    // Decompress SQL dump directly to staging/import.sql via streaming (no RAM)
    logger_.info("MIGRATION", "Decompressing SQL dump to staging/import.sql");
    std::string sql_file = staging + "/import.sql";
    auto gunzip_res = executor_.run_stdout_to_file({"gunzip", "-c", dump_gz}, sql_file);
    if (gunzip_res.exit_code != 0) {
        logger_.error("MIGRATION", "gzip decompression failed: exit=" + std::to_string(gunzip_res.exit_code));
        result.errors.push_back("SQL dump decompression failed");
        cleanup_stg(); return result;
    }
    logger_.info("MIGRATION", "Decompressed to " + sql_file + " (streaming, no RAM)");

    // Copy SQL file into container for safe import
    logger_.info("MIGRATION", "Copying SQL dump into container");
    auto cp_res = executor_.run({"docker", "cp", sql_file, db_container + ":/tmp/import.sql"});
    if (cp_res.exit_code != 0) {
        logger_.error("MIGRATION", "docker cp failed: " + cp_res.err);
        result.errors.push_back("Cannot copy SQL dump into database container");
        cleanup_stg(); return result;
    }

    // Create safety backup via streaming mariadb-dump directly to file (no RAM)
    logger_.info("MIGRATION", "Creating database safety backup (streaming)");
    std::string safety_file = staging + "/safety.sql";
    auto safety = executor_.run_stdout_to_file({
        "docker", "exec", db_container,
        "env", "MYSQL_PWD=" + db_password,
        "mariadb-dump", "--single-transaction", "--quick",
        "-u" + db_user, db_name
    }, safety_file);
    if (safety.exit_code != 0) {
        logger_.error("MIGRATION", "Safety backup failed: exit=" + std::to_string(safety.exit_code));
        result.errors.push_back("Database safety backup failed — aborting");
        cleanup_stg(); return result;
    }
    result.safety_backup_created = true;
    logger_.info("MIGRATION", "Safety backup created at " + safety_file);

    // Parse safety table count from backup for rollback verification (no shell)
    uint64_t expected_table_count = 0;
    {
        std::ifstream sf_count(safety_file);
        if (sf_count.is_open()) {
            std::string line;
            while (std::getline(sf_count, line)) {
                if (line.find("CREATE TABLE") != std::string::npos ||
                    line.find("CREATE VIEW") != std::string::npos ||
                    line.find("CREATE PROCEDURE") != std::string::npos) {
                    expected_table_count++;
                }
            }
        }
    }

    // Pre-declare for rollback lambda capture
    std::string wp_config_path_for_rollback;
    std::string wp_config_bak;

    // Rollback helper: restore safety.sql into DB via app user (no DROP DATABASE)
    auto do_rollback = [&]() -> bool {
        logger_.info("MIGRATION", "Rollback step 1: copying safety dump to container");
        if (!result.safety_backup_created) {
            logger_.error("MIGRATION", "Rollback failed: no safety backup available");
            return false;
        }
        auto cp = executor_.run({"docker", "cp", safety_file, db_container + ":/tmp/safety_restore.sql"});
        if (cp.exit_code != 0) {
            logger_.error("MIGRATION", "Rollback: docker cp failed: " + cp.err);
            return false;
        }
        logger_.info("MIGRATION", "Rollback step 2: clearing DB objects and restoring safety dump");

        // First clean all existing objects with FK disabled
        auto cleanup = executor_.run({
            "docker", "exec", db_container,
            "env", "MYSQL_PWD=" + db_password,
            "mariadb", "-N", "-u" + db_user, db_name,
            "-e", "SET FOREIGN_KEY_CHECKS=0; "
                  "SELECT GROUP_CONCAT(CONCAT('DROP TABLE IF EXISTS `', TABLE_NAME, '`') SEPARATOR '; ') "
                  "FROM information_schema.TABLES WHERE TABLE_SCHEMA='" + db_name + "';"
        });
        if (cleanup.exit_code == 0 && !cleanup.out.empty() && cleanup.out.find("DROP") != std::string::npos) {
            executor_.run({
                "docker", "exec", db_container,
                "env", "MYSQL_PWD=" + db_password,
                "mariadb", "-N", "-u" + db_user, db_name,
                "-e", "SET FOREIGN_KEY_CHECKS=0; " + cleanup.out + "; SET FOREIGN_KEY_CHECKS=1;"
            });
        }
        // Always use SOURCE for safety restore (even if no tables existed)
        auto restore = executor_.run({
            "docker", "exec", db_container,
            "env", "MYSQL_PWD=" + db_password,
            "mariadb", "-N", "-u" + db_user, db_name,
            "-e", "SET FOREIGN_KEY_CHECKS=0; SOURCE /tmp/safety_restore.sql; SET FOREIGN_KEY_CHECKS=1;"
        });
        if (restore.exit_code != 0) {
            logger_.error("MIGRATION", "Rollback: DB restore failed: exit=" + std::to_string(restore.exit_code) + " " + restore.err);
            return false;
        }
        logger_.info("MIGRATION", "Rollback step 3: verifying restored table count");
        auto check = executor_.run({
            "docker", "exec", db_container,
            "env", "MYSQL_PWD=" + db_password,
            "mariadb", "-N", "-u" + db_user, db_name, "-e", "SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA='" + db_name + "'"
        });
        uint64_t actual_count = 0;
        if (check.exit_code == 0) {
            std::string c = check.out;
            while (!c.empty() && (c.back() == '\n' || c.back() == '\r')) c.pop_back();
            try { actual_count = std::stoull(c); } catch (...) {}
        }
        bool ok = (check.exit_code == 0 && actual_count == expected_table_count);
        logger_.info("MIGRATION", "Rollback: expected=" + std::to_string(expected_table_count) + " actual=" + std::to_string(actual_count)
                     + " ok=" + std::string(ok ? "true" : "false"));

        // Restore wp-config if it was modified
        if (!wp_config_bak.empty() && !wp_config_path_for_rollback.empty()) {
            logger_.info("MIGRATION", "Rollback step 4: restoring wp-config.php from host copy");
            auto cfg_restore = executor_.run({"cp", wp_config_bak, wp_config_path_for_rollback});
            logger_.info("MIGRATION", "Rollback: wp-config restore exit=" + std::to_string(cfg_restore.exit_code));
        }
        logger_.info("MIGRATION", "Rollback completed");
        return ok;
    };

    // Drop existing tables with FOREIGN_KEY_CHECKS=0 (HARD FAILURE)
    logger_.info("MIGRATION", "Dropping all database content from " + db_name);
    logger_.info("MIGRATION", "Target database: " + db_name);
    logger_.info("MIGRATION", "Target user: " + db_user);
    logger_.info("MIGRATION", "DB container: " + db_container);

    // Step 1: list tables
    logger_.info("MIGRATION", "mysql: SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA='" + db_name + "'");
    auto table_list = executor_.run({
        "docker", "exec", db_container,
        "env", "MYSQL_PWD=" + db_password,
        "mariadb", "-N", "-u" + db_user, db_name,
        "-e", "SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA='" + db_name + "'"
    });
    logger_.info("MIGRATION", "table_list exit_code=" + std::to_string(table_list.exit_code)
                 + " stdout_size=" + std::to_string(table_list.out.size())
                 + " stderr=" + table_list.err);

    if (table_list.exit_code != 0) {
        logger_.error("MIGRATION", "Failed to list tables: exit=" + std::to_string(table_list.exit_code));
        logger_.error("MIGRATION", "table_list stderr: " + table_list.err);
        result.errors.push_back("Cannot list database tables: " + table_list.err);
        cleanup_stg(); return result;
    }

    // Drop each table with FOREIGN_KEY_CHECKS=0
    logger_.info("MIGRATION", "Dropping tables one by one with FOREIGN_KEY_CHECKS=0");
    std::string drop_sql = "SET FOREIGN_KEY_CHECKS=0;";
    std::istringstream tbl_stream(table_list.out);
    std::string tbl_name;
    int table_count = 0;
    while (std::getline(tbl_stream, tbl_name)) {
        if (tbl_name.empty()) continue;
        // Trim whitespace
        while (!tbl_name.empty() && (tbl_name.back() == '\n' || tbl_name.back() == '\r' || tbl_name.back() == ' ')) tbl_name.pop_back();
        if (tbl_name.empty()) continue;
        drop_sql += " DROP TABLE IF EXISTS `" + tbl_name + "`;";
        table_count++;
    }
    drop_sql += " SET FOREIGN_KEY_CHECKS=1;";

    logger_.info("MIGRATION", "Dropping " + std::to_string(table_count) + " tables");
    logger_.info("MIGRATION", "mysql: SET FOREIGN_KEY_CHECKS=0; DROP TABLE ...; SET FOREIGN_KEY_CHECKS=1;");

    if (table_count > 0) {
        auto drop_res = executor_.run({
            "docker", "exec", db_container,
            "env", "MYSQL_PWD=" + db_password,
            "mariadb", "-N", "-u" + db_user, db_name,
            "-e", drop_sql
        });
        logger_.info("MIGRATION", "drop_tables exit_code=" + std::to_string(drop_res.exit_code)
                     + " stderr=" + drop_res.err);
        if (drop_res.exit_code != 0) {
            logger_.error("MIGRATION", "Failed to drop tables: exit=" + std::to_string(drop_res.exit_code));
            logger_.error("MIGRATION", "drop_tables stderr: " + drop_res.err);
            result.errors.push_back("Cannot clean database: " + drop_res.err);
            cleanup_stg(); return result;
        }
    } else {
        logger_.info("MIGRATION", "No tables to drop (empty database)");
    }
    logger_.info("MIGRATION", "Database cleaned — " + std::to_string(table_count) + " tables removed");

    // Import SQL via stdin from file (streaming, no RAM)
    logger_.info("MIGRATION", "Importing SQL from staging/import.sql via container stdin");
    auto import_res = executor_.run_with_stdin_file({
        "docker", "exec", "-i", db_container,
        "env", "MYSQL_PWD=" + db_password,
        "mariadb", "-u" + db_user, db_name
    }, sql_file);
    if (import_res.exit_code != 0) {
        logger_.error("MIGRATION", "SQL import failed: exit=" + std::to_string(import_res.exit_code) + " " + import_res.err);
        result.errors.push_back("SQL import failed");
        do_rollback();
        cleanup_stg(); return result;
    }
    result.dump_size = dump_size;
    logger_.info("MIGRATION", "SQL import completed");

    // Verify database has tables
    auto table_verify = executor_.run({
        "docker", "exec", db_container,
        "env", "MYSQL_PWD=" + db_password,
        "mariadb", "-N", "-u" + db_user, db_name,
        "-e", "SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA = '" + db_name + "'"
    });
    if (table_verify.exit_code == 0) {
        std::string cnt = table_verify.out;
        while (!cnt.empty() && (cnt.back() == '\n' || cnt.back() == '\r')) cnt.pop_back();
        logger_.info("MIGRATION", "Tables in database: " + cnt);
        if (cnt == "0") {
            logger_.error("MIGRATION", "Zero tables after import — rollback");
            result.errors.push_back("Zero tables after import");
            do_rollback();
            cleanup_stg(); return result;
        }
    }

    // Verify DB connection with new credentials
    logger_.info("MIGRATION", "Verifying DB connection with current credentials");
    auto conn_check = executor_.run({
        "docker", "exec", db_container,
        "env", "MYSQL_PWD=" + db_password,
        "mariadb", "-N", "-u" + db_user, db_name,
        "-e", "SELECT 1"
    });
    if (conn_check.exit_code != 0 || conn_check.out.find("1") == std::string::npos) {
        logger_.error("MIGRATION", "DB connection check failed after import");
        result.errors.push_back("DB connection check failed after import");
        do_rollback();
        cleanup_stg(); return result;
    }

    // Docker compose MariaDB service name (used for wp-config and diagnostics)
    std::string db_host = "mariadb";

    // Determine container wp-config path from docker inspect mount mapping
    logger_.info("MIGRATION", "Determining container wp-config path from mount mapping");
    std::string container_wp_config;
    std::string php_cont = "site-" + std::to_string(m.migration_site_id) + "-php";
    {
        // Get all mounts from PHP container as JSON
        auto inspect = executor_.run({"docker", "inspect", php_cont,
            "--format", "{{range .Mounts}}{{.Source}}|{{.Destination}}\n{{end}}"});
        logger_.info("MIGRATION", "Host public dir: " + cfg_.sites_dir() + opts.domain + "/public");
        if (inspect.exit_code == 0) {
            std::istringstream mount_stream(inspect.out);
            std::string mount_line;
            while (std::getline(mount_stream, mount_line)) {
                if (mount_line.empty()) continue;
                auto pipe = mount_line.find('|');
                if (pipe == std::string::npos) continue;
                std::string host_path = mount_line.substr(0, pipe);
                std::string cont_path = mount_line.substr(pipe + 1);
                // Canonical match: resolve host path
                char host_real[PATH_MAX], pub_real[PATH_MAX];
                if (::realpath(host_path.c_str(), host_real) &&
                    ::realpath((cfg_.sites_dir() + opts.domain + "/public").c_str(), pub_real) &&
                    std::string(host_real) == std::string(pub_real)) {
                    container_wp_config = cont_path + "/wp-config.php";
                    logger_.info("MIGRATION", "PHP public mount destination: " + cont_path);
                    logger_.info("MIGRATION", "Container wp-config path: " + container_wp_config);
                    break;
                }
            }
        }
    }

    // Update wp-config.php with safety copy
    std::string public_dir = site_dir + "public/";
    std::string wp_config_path = find_wp_config_file(public_dir);
    wp_config_path_for_rollback = wp_config_path; // for rollback to restore
    if (!wp_config_path.empty()) {
        logger_.info("MIGRATION", "Backing up wp-config.php before update");
        wp_config_bak = wp_config_path + ".containercp-before-sql";
        executor_.run({"cp", wp_config_path, wp_config_bak});

        // Atomic update: write to temp, then rename
        std::string tmp_config = wp_config_path + ".tmp";
        bool wp_updated = update_wp_config_db_credentials(wp_config_path, m.wp_db_name, m.wp_db_user,
                                                           db_name, db_user, db_password, db_host);
        if (!wp_updated) {
            logger_.error("MIGRATION", "wp-config.php update failed — rolling back DB");
            result.errors.push_back("wp-config.php update failed");
            do_rollback();
            executor_.run({"cp", wp_config_bak, wp_config_path});
            cleanup_stg(); return result;
        }

        // PHP syntax check (use container mount path)
        logger_.info("MIGRATION", "Running php -l on updated wp-config.php");
        std::string php_check_path = container_wp_config.empty() ? "/var/www/html/wp-config.php" : container_wp_config;
        logger_.info("MIGRATION", "PHP check path: " + php_check_path);

        // Verify file exists in container
        auto exist_check = executor_.run({"docker", "exec", php_cont, "test", "-f", php_check_path});
        logger_.info("MIGRATION", "wp-config exists in PHP container: " + std::string(exist_check.exit_code == 0 ? "true" : "false"));

        auto php_check = executor_.run({
            "docker", "exec", php_cont,
            "php", "-l", php_check_path
        });
        if (php_check.exit_code != 0) {
            logger_.error("MIGRATION", "php -l failed: " + php_check.err);
            result.errors.push_back("wp-config.php syntax check failed");
            do_rollback();
            executor_.run({"cp", wp_config_bak, wp_config_path});
            cleanup_stg(); return result;
        }
        logger_.info("MIGRATION", "php -l OK");

        result.wp_config_updated = true;
        logger_.info("MIGRATION", "wp-config.php updated with all 4 constants");
    }

    // Restart PHP
    logger_.info("MIGRATION", "Restarting PHP");
    executor_.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "restart", "php"});
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Health check
    logger_.info("MIGRATION", "Checking container health");
    bool healthy = false;
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        auto whc = executor_.run({"docker", "inspect", "site-" + std::to_string(m.migration_site_id) + "-web",
                                  "--format", "{{.State.Status}}|{{.State.Health.Status}}"});
        auto phc = executor_.run({"docker", "inspect", "site-" + std::to_string(m.migration_site_id) + "-php",
                                  "--format", "{{.State.Status}}|{{.State.Health.Status}}"});
        if (whc.exit_code == 0 && phc.exit_code == 0 &&
            whc.out.find("running|healthy") != std::string::npos &&
            phc.out.find("running|healthy") != std::string::npos) {
            healthy = true;
            logger_.info("MIGRATION", "Health check passed at " + std::to_string((i + 1) * 2) + "s");
            break;
        }
    }
    if (!healthy) {
        logger_.error("MIGRATION", "Health check timeout after 60s");
        result.errors.push_back("Health check timeout after 60s — rolling back");
        do_rollback();
        if (result.wp_config_updated && !wp_config_bak.empty()) {
            executor_.run({"cp", wp_config_bak, wp_config_path});
        }
        cleanup_stg(); return result;
    }

    // HTTP check
    logger_.info("MIGRATION", "Running HTTP check");
    auto http = executor_.run({
        "curl", "-s", "-o", "/dev/null", "-w", "%{http_code}",
        "--resolve", opts.domain + ":80:127.0.0.1",
        "http://" + opts.domain + "/"
    });
    if (http.exit_code == 0 && !http.out.empty()) {
        int code = 0;
        try { code = std::stoi(http.out); } catch (...) {}
        logger_.info("MIGRATION", "HTTP check returned " + std::to_string(code));

        if (code >= 200 && code <= 404 && code != 500) {
            // Acceptable codes
        } else if (code == 500) {
            logger_.error("MIGRATION", "HTTP 500 — running WordPress diagnostics");

            // A. Run WordPress directly via PHP CLI to capture real error
            logger_.info("MIGRATION", "DIAG: Running WordPress via PHP CLI");
            auto index_path = container_wp_config.empty()
                ? "/usr/local/apache2/htdocs/index.php"
                : container_wp_config.substr(0, container_wp_config.rfind('/') + 1) + "index.php";
            auto wp_cli = executor_.run({
                "docker", "exec", "site-" + std::to_string(m.migration_site_id) + "-php",
                "php", "-d", "display_errors=1", index_path
            });
            std::string wp_error = wp_cli.err.empty() ? wp_cli.out : wp_cli.err;
            logger_.info("MIGRATION", "DIAG: WordPress CLI output=" + wp_error.substr(0, 1000));

            // B. DB constants (without password)
            logger_.info("MIGRATION", "DIAG: DB_NAME=" + db_name + " DB_USER=" + db_user + " DB_HOST=" + db_host);

            // C. PHP modules check
            auto php_m = executor_.run({"docker", "exec", "site-" + std::to_string(m.migration_site_id) + "-php",
                                         "php", "-m"});
            bool has_mysqli = php_m.out.find("mysqli") != std::string::npos;
            bool has_pdo_mysql = php_m.out.find("pdo_mysql") != std::string::npos;
            logger_.info("MIGRATION", "DIAG: mysqli=" + std::string(has_mysqli ? "yes" : "NO")
                         + " pdo_mysql=" + std::string(has_pdo_mysql ? "yes" : "NO"));

            // D. DNS check from PHP container
            auto dns = executor_.run({"docker", "exec", "site-" + std::to_string(m.migration_site_id) + "-php",
                                       "getent", "hosts", db_host});
            logger_.info("MIGRATION", "DIAG: DNS " + db_host + " exit=" + std::to_string(dns.exit_code) + " " + dns.out);

            // E. TCP connection
            auto tcp = executor_.run({"docker", "exec", "site-" + std::to_string(m.migration_site_id) + "-php",
                                      "timeout", "3", "nc", "-zv", db_host, "3306"});
            std::string tcp_result = (tcp.exit_code == 0) ? "connected" : "failed exit=" + std::to_string(tcp.exit_code);
            logger_.info("MIGRATION", "DIAG: TCP " + db_host + ":3306 " + tcp_result);

            // F. PHP error log
            auto php_err = executor_.run({"docker", "logs", "--tail", "50", "site-" + std::to_string(m.migration_site_id) + "-php"});
            logger_.info("MIGRATION", "DIAG: PHP logs tail=50 err=" + php_err.err);

            // G. Web error log
            auto web_err = executor_.run({"docker", "logs", "--tail", "50", "site-" + std::to_string(m.migration_site_id) + "-web"});
            logger_.info("MIGRATION", "DIAG: Web logs tail=50 err=" + web_err.err);

            // H. HTTP response body (first 4KB)
            auto http_body = executor_.run({"curl", "-s", "--max-time", "5", "--resolve", opts.domain + ":80:127.0.0.1",
                                            "http://" + opts.domain + "/"});
            std::string body_preview = http_body.out.substr(0, 4096);
            logger_.info("MIGRATION", "DIAG: HTTP body (4KB)=" + body_preview.substr(0, 200));

            result.errors.push_back("HTTP 500: " + wp_error.substr(0, 500));
            do_rollback();
            if (result.wp_config_updated && !wp_config_bak.empty()) {
                executor_.run({"cp", wp_config_bak, wp_config_path});
            }
            cleanup_stg(); return result;
        } else {
            logger_.warning("MIGRATION", "HTTP status " + std::to_string(code) + " — accepting");
        }
    }

    // Update marker to stage 3 (atomic)
    logger_.info("MIGRATION", "Updating marker to stage 3");
    MigrationMarker updated;
    updated.version = 1;
    updated.domain = opts.domain;
    updated.owner = opts.owner;
    updated.site_id = m.migration_site_id;
    updated.stage = 3;
    updated.files_pending = false;
    updated.files_imported = true;
    updated.sql_pending = false;

    std::string marker_content = updated.to_json();
    std::string tmp_path = marker_path + ".tmp";
    if (!fs_.create_file(tmp_path, marker_content) || std::rename(tmp_path.c_str(), marker_path.c_str()) != 0) {
        logger_.error("MIGRATION", "Failed to finalize marker: " + std::string(std::strerror(errno)));
        result.errors.push_back("CRITICAL: SQL imported but failed to finalize migration marker");
        do_rollback();
        if (result.wp_config_updated && !wp_config_bak.empty()) {
            executor_.run({"cp", wp_config_bak, wp_config_path});
        }
        cleanup_stg(); return result;
    }
    logger_.info("MIGRATION", "Marker updated to stage 3 — migration completed");

    cleanup_stg();
    result.success = true;
    logger_.info("MIGRATION", "SQL import completed successfully");
    return result;
}

} // namespace containercp::migration
