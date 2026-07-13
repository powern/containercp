#include "VestaSiteImporter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <sys/stat.h>

namespace containercp::migration {

VestaSiteImporter::VestaSiteImporter(runtime::CommandExecutor& executor,
                                     filesystem::Filesystem& fs,
                                     config::Config& cfg)
    : executor_(executor)
    , fs_(fs)
    , cfg_(cfg)
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
        // Security: reject absolute paths and parent directory traversal
        if (line[0] == '/') {
            error = "Archive contains absolute path: " + line;
            return false;
        }
        if (line.find("..") != std::string::npos) {
            error = "Archive contains parent directory reference: " + line;
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
    size_t& web_size) {

    // Normalize: entries may be "web/..." or "./web/..."
    // Always normalize output to "web/<domain>/domain_data.tar.gz" (no ./ prefix)
    std::string target_dot = "./web/" + domain + "/domain_data.tar.gz";
    std::string target_no_dot = "web/" + domain + "/domain_data.tar.gz";

    for (const auto& e : entries) {
        if (e == target_dot || e == target_no_dot) {
            web_archive_path = target_no_dot;
            web_size = 0;
            return true;
        }
    }
    return false;
}

std::string VestaSiteImporter::detect_web_root(
    const std::string& staging_dir,
    const std::string& archive,
    const std::string& domain) {

    // Extract domain_data.tar.gz directly to staging directory (binary-safe)
    std::string data_tarball = staging_dir + "/domain_data.tar.gz";

    auto result = executor_.run({
        "tar", "-xf", archive,
        "-C", staging_dir,
        "web/" + domain + "/domain_data.tar.gz"
    });
    if (result.exit_code != 0) {
        result = executor_.run({
            "tar", "-xf", archive,
            "-C", staging_dir,
            "./web/" + domain + "/domain_data.tar.gz"
        });
        if (result.exit_code != 0) {
            return "";
        }
    }

    // The extracted file is at staging_dir/web/<domain>/domain_data.tar.gz
    // Move it to staging_dir/ for easier access
    executor_.run({
        "mv",
        staging_dir + "/web/" + domain + "/domain_data.tar.gz",
        data_tarball
    });
    executor_.run({"rm", "-rf", staging_dir + "/web"});

    // List contents of domain_data.tar.gz to find web root
    auto list_result = executor_.run({
        "tar", "-tzf", data_tarball
    });
    if (list_result.exit_code != 0) {
        std::remove(data_tarball.c_str());
        return "";
    }

    static const char* candidates[] = {
        "public_html", "public", "htdocs", "www", "root"
    };

    std::istringstream stream(list_result.out);
    std::string line;
    while (std::getline(stream, line)) {
        // Check first component of path
        for (const auto* cand : candidates) {
            std::string prefix = std::string(cand) + "/";
            if (line == std::string(cand) ||
                line.substr(0, prefix.size()) == prefix) {
                std::remove(data_tarball.c_str());
                return cand;
            }
        }
    }

    std::remove(data_tarball.c_str());
    return "."; // root of tarball
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
    bool& out_ambiguous) {

    out_ambiguous = false;

    // Extract domain_data.tar.gz to staging (binary-safe: write to disk, not stdout)
    auto result = executor_.run({
        "tar", "-xf", archive,
        "-C", staging_dir,
        "web/" + domain + "/domain_data.tar.gz"
    });
    if (result.exit_code != 0) {
        result = executor_.run({
            "tar", "-xf", archive,
            "-C", staging_dir,
            "./web/" + domain + "/domain_data.tar.gz"
        });
        if (result.exit_code != 0) return false;
    }

    std::string data_tarball = staging_dir + "/domain_data.tar.gz";
    executor_.run({
        "mv", staging_dir + "/web/" + domain + "/domain_data.tar.gz", data_tarball
    });
    executor_.run({"rm", "-rf", staging_dir + "/web"});

    // Extract wp-config.php from domain_data.tar.gz (text output is safe via stdout)
    auto wp_result = executor_.run({
        "tar", "-xzf", data_tarball,
        "--to-stdout",
        "--wildcards",
        "*/wp-config.php"
    });
    std::remove(data_tarball.c_str());

    if (wp_result.exit_code != 0) {
        return false;
    }

    std::string content = wp_result.out;

    // Regex patterns supporting single and double quotes
    std::regex db_name_re(R"(define\s*\(\s*['\"]DB_NAME['\"]\s*,\s*['\"](.+?)['\"]\s*\))");
    std::regex db_user_re(R"(define\s*\(\s*['\"]DB_USER['\"]\s*,\s*['\"](.+?)['\"]\s*\))");
    std::regex db_pass_re(R"(define\s*\(\s*['\"]DB_PASSWORD['\"]\s*,\s*['\"](.+?)['\"]\s*\))");
    std::regex db_host_re(R"(define\s*\(\s*['\"]DB_HOST['\"]\s*,\s*['\"](.+?)['\"]\s*\))");

    std::smatch match;

    if (std::regex_search(content, match, db_name_re)) {
        out_db_name = match[1];
    } else {
        out_ambiguous = true;
    }

    if (std::regex_search(content, match, db_user_re)) {
        out_db_user = match[1];
    }

    if (std::regex_search(content, match, db_pass_re)) {
        out_db_password = match[1];
    }

    if (std::regex_search(content, match, db_host_re)) {
        out_db_host = match[1];
    }

    // Check if DB_NAME is a variable reference or empty
    if (out_db_name.empty() ||
        out_db_name.find("$") != std::string::npos ||
        out_db_name.find("getenv") != std::string::npos ||
        out_db_name.find("_SERVER") != std::string::npos) {
        out_ambiguous = true;
    }

    return !out_db_name.empty() && !out_ambiguous;
}

bool VestaSiteImporter::find_db_in_archive(
    const std::vector<std::string>& entries,
    const std::string& db_name,
    std::string& out_dump_path,
    size_t& out_size,
    std::string& out_type) {

    // Try both "./db/..." and "db/..." forms
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
    // Remove common VestaCP user prefix (e.g., "admin_" or "user_")
    std::string result = raw;
    auto underscore = result.find('_');
    if (underscore != std::string::npos && underscore < 20) {
        // Only strip if it looks like a username prefix (short, alphanumeric)
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

    // 1. Check backup file exists
    if (!fs_.exists(opts.backup_path)) {
        m.errors.push_back("Backup file not found: " + opts.backup_path);
        return m;
    }

    // 2. Get archive size
    struct stat st;
    if (::stat(opts.backup_path.c_str(), &st) == 0) {
        m.archive_size = st.st_size;
    }

    // 3. Safe tar listing
    std::vector<std::string> entries;
    std::string error;
    if (!tar_safe_list(opts.backup_path, entries, error)) {
        m.errors.push_back("Failed to read archive: " + error);
        return m;
    }

    // 4. Check disk space on backup dir
    auto df_result = executor_.run({
        "df", "--output=avail", cfg_.data_root()
    });
    if (df_result.exit_code == 0) {
        std::istringstream ss(df_result.out);
        std::string line;
        std::getline(ss, line); // header
        std::getline(ss, line);
        if (!line.empty()) {
            try { m.available_disk_mb = std::stoull(line) / 1024; }
            catch (...) {}
        }
    }

    // 5. Find domain_data.tar.gz
    std::string web_archive_path;
    if (!find_domain_in_archive(entries, opts.domain, web_archive_path, m.web_size)) {
        // Also try without ./ prefix
        std::string alt_domain = opts.domain;
        if (alt_domain.find(".") != std::string::npos) {
            // Some backups use underscored domain for path
        }
        m.errors.push_back("Domain '" + opts.domain + "' not found in backup archive");
        return m;
    }
    m.domain_found = true;
    m.web_archive_path = web_archive_path;

    // 6. Detect web root type
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

    // 7. Find wp-config.php
    staging = make_staging_dir();
    if (staging.empty()) {
        m.errors.push_back("Failed to create staging directory");
        return m;
    }

    std::string wp_db_pass; // never logged
    bool ambiguous = false;
    if (extract_wp_config(opts.backup_path, opts.domain, m.web_root_type,
                          staging, m.wp_db_name, m.wp_db_user,
                          wp_db_pass, m.wp_db_host, ambiguous)) {
        m.has_wp_config = true;
    }
    cleanup_staging(staging);

    // 8. Check site existence
    m.site_exists = false;
    {
        // Check via filesystem — site directory exists
        std::string site_dir = cfg_.sites_dir() + opts.domain + "/";
        m.site_exists = fs_.exists(site_dir);
    }

    // 9. Find database
    if (m.has_wp_config && !opts.skip_db) {
        std::string db_to_find = opts.database;
        if (db_to_find.empty()) {
            db_to_find = normalize_db_name(m.wp_db_name);
        }

        // List all databases in archive (handle both ./db/ and db/ prefixes)
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
            find_db_in_archive(entries, db_to_find,
                               m.db_dump_path, m.db_dump_size, m.db_type);
        }

        if (m.db_dump_path.empty() && !db_to_find.empty()) {
            // Try with original (non-normalized) name
            find_db_in_archive(entries, m.wp_db_name,
                               m.db_dump_path, m.db_dump_size, m.db_type);
        }

        if (!m.db_dump_path.empty()) {
            m.db_dump_found = true;
        } else {
            m.warnings.push_back("SQL dump not found for database '" + db_to_find + "'");
            if (!opts.database.empty()) {
                m.warnings.push_back("Try without --database or check available DBs in archive");
            }
        }
    }

    // 10. Calculate required disk space
    m.required_disk_mb = (m.web_size / (1024 * 1024)) + (m.db_dump_size / (1024 * 1024)) + 100;

    // 11. Check if wp config is ambiguous
    if (m.has_wp_config && ambiguous) {
        m.warnings.push_back("DB_NAME in wp-config.php is ambiguous (uses variable). "
                            "Use --database to specify manually.");
    }

    return m;
}

void VestaSiteImporter::print_dry_run(const Manifest& m, const Options& opts) {
    std::cout << "\nMyVestaCP → ContainerCP Site Import (DRY RUN)"
              << "\n============================================="
              << "\nBackup file:      " << m.backup_path
              << "\nArchive size:     " << (m.archive_size / (1024*1024)) << " MB"
              << "\nDomain:           " << m.domain
              << "\nOwner:            " << opts.owner
              << "\n";

    if (!m.errors.empty()) {
        std::cout << "\nERRORS:\n";
        for (const auto& e : m.errors) {
            std::cout << "  ❌ " << e << "\n";
        }
        return;
    }

    std::cout << "\nDomain found:           "
              << (m.domain_found ? "✅" : "❌");
    if (m.domain_found) {
        std::cout << "\n  Web archive:          " << m.web_archive_path
                  << "\n  Web root type:        " << m.web_root_type;
    }

    std::cout << "\nSite already exists:    "
              << (m.site_exists ? "❌ (will abort)" : "✅ (ok)");

    std::cout << "\nWordPress detected:     "
              << (m.has_wp_config ? "✅" : "⚠️  (skipping DB import)");

    if (m.has_wp_config) {
        std::cout << "\n  DB_NAME:             " << m.wp_db_name
                  << "\n  DB_USER:             " << m.wp_db_user
                  << "\n  DB_HOST:             " << m.wp_db_host;

        std::cout << "\n  Normalized DB name:  "
                  << normalize_db_name(m.wp_db_name);

        if (m.db_dump_found) {
            std::cout << "\n  SQL dump:            " << m.db_dump_path
                      << "\n  DB type:             " << m.db_type;
        } else {
            std::cout << "\n  SQL dump:            ❌ not found";
        }
    }

    if (!m.all_databases.empty()) {
        std::cout << "\n\nDatabases in archive:";
        for (const auto& db : m.all_databases) {
            std::cout << "\n  - " << db;
        }
    }

    std::cout << "\n\nDisk space:";
    std::cout << "\n  Required:           ~" << m.required_disk_mb << " MB"
              << "\n  Available:          " << m.available_disk_mb << " MB";

    if (m.required_disk_mb > m.available_disk_mb) {
        std::cout << "\n  ❌ NOT ENOUGH DISK SPACE";
    } else {
        std::cout << "\n  ✅ Sufficient";
    }

    if (m.site_exists) {
        std::cout << "\n\n⚠️  Site already exists. Will abort — overwrite not supported in v1.";
    }

    if (!m.warnings.empty()) {
        std::cout << "\n\nWarnings:\n";
        for (const auto& w : m.warnings) {
            std::cout << "  ⚠️  " << w << "\n";
        }
    }

    if (m.errors.empty() && !m.site_exists) {
        std::cout << "\n\nWill do:";
        std::cout << "\n  1. Create site record (SiteCreateOperation)";
        std::cout << "\n  2. Create Docker stack (web, php, mariadb, redis)";
        std::cout << "\n  3. Import web files (" << (m.web_size / 1024) << " KB)";
        if (m.has_wp_config && m.db_dump_found) {
            std::cout << "\n  4. Import SQL dump into existing database";
            std::cout << "\n  5. Update wp-config.php with new credentials";
            std::cout << "\n  6. Restart web+php";
        }
        std::cout << "\n  7. Health check";
    }

    std::cout << "\n" << std::endl;
}

} // namespace containercp::migration
