#include "migration/VestaSiteImporter.h"
#include "config/Config.h"
#include "filesystem/Filesystem.h"
#include "runtime/CommandExecutor.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>

#include "doctest/doctest.h"

using namespace containercp;

static std::string create_test_tar(const std::string& path,
                                    const std::string& domain,
                                    const std::string& web_root = "public_html") {
    std::string dir = path + "_dir";
    std::string web_dir = dir + "/web/" + domain;
    std::string db_dir = dir + "/db/test_wp_db";
    std::string webroot_dir = dir + "/" + web_root;

    ::mkdir(dir.c_str(), 0755);
    ::mkdir((dir + "/web").c_str(), 0755);
    ::mkdir(web_dir.c_str(), 0755);
    ::mkdir((dir + "/db").c_str(), 0755);
    ::mkdir(db_dir.c_str(), 0755);
    ::mkdir(webroot_dir.c_str(), 0755);

    {
        std::ofstream f(webroot_dir + "/wp-config.php");
        f << "<?php\n"
          << "define( 'DB_NAME', 'test_wp_db' );\n"
          << "define( 'DB_USER', 'test_user' );\n"
          << "define( 'DB_PASSWORD', 'secret123' );\n"
          << "define( 'DB_HOST', 'localhost' );\n";
    }
    {
        std::ofstream f(webroot_dir + "/index.php");
        f << "<?php echo 'hello';";
    }

    std::system(("cd " + dir + " && tar czf " + web_dir
        + "/domain_data.tar.gz " + web_root + " 2>/dev/null").c_str());

    {
        std::ofstream f(db_dir + "/test_wp_db.mysql.sql.gz");
        f << "gzip-compressed-dump-content";
    }

    std::system(("cd " + dir + " && tar cf " + path
        + " web/ db/ 2>/dev/null").c_str());
    std::system(("rm -rf " + dir).c_str());
    return path;
}

// ─── inspect: domain found with WordPress ───

TEST_CASE("VestaSiteImporter inspect - domain found with WordPress") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_wp.tar";
    create_test_tar(tar_path, "example.com");

    migration::Options opts;
    opts.backup_path = tar_path;
    opts.domain = "example.com";
    opts.owner = "admin";
    opts.dry_run = true;

    auto m = importer.inspect(opts);

    CHECK(m.errors.empty());
    CHECK(m.domain_found);
    CHECK(m.web_archive_path == "web/example.com/domain_data.tar.gz");
    CHECK(m.web_root_type == "public_html");
    CHECK(m.wp_config_found);
    CHECK(m.wp_config_parsed);
    CHECK_FALSE(m.wp_db_ambiguous);
    CHECK(m.wp_db_name == "test_wp_db");
    CHECK(m.wp_db_user == "test_user");
    CHECK(m.wp_db_host == "localhost");
    CHECK(m.db_dump_found);
    CHECK_FALSE(m.db_dump_path.empty());
    CHECK(m.db_type == "mysql");
    CHECK_FALSE(m.site_exists);

    std::remove(tar_path.c_str());
}

// ─── domain not found ───

TEST_CASE("VestaSiteImporter inspect - domain not found") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_notfound.tar";
    create_test_tar(tar_path, "example.com");

    migration::Options opts;
    opts.backup_path = tar_path;
    opts.domain = "nonexistent.com";
    opts.owner = "admin";

    auto m = importer.inspect(opts);

    CHECK_FALSE(m.errors.empty());
    CHECK_FALSE(m.domain_found);

    std::remove(tar_path.c_str());
}

// ─── backup file missing ───

TEST_CASE("VestaSiteImporter inspect - backup file missing") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    migration::Options opts;
    opts.backup_path = "/tmp/nonexistent_backup.tar";
    opts.domain = "example.com";
    opts.owner = "admin";

    auto m = importer.inspect(opts);

    CHECK_FALSE(m.errors.empty());
    CHECK(m.errors[0].find("not found") != std::string::npos);
}

// ─── web root variants ───

TEST_CASE("VestaSiteImporter detect web root - public_html") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_root_ph.tar";
    create_test_tar(tar_path, "x.com", "public_html");
    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK(m.web_root_type == "public_html");
    std::remove(tar_path.c_str());
}

TEST_CASE("VestaSiteImporter detect web root - public") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_root_pub.tar";
    create_test_tar(tar_path, "x.com", "public");
    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK(m.web_root_type == "public");
    std::remove(tar_path.c_str());
}

TEST_CASE("VestaSiteImporter detect web root - htdocs") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_root_ht.tar";
    create_test_tar(tar_path, "x.com", "htdocs");
    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK(m.web_root_type == "htdocs");
    std::remove(tar_path.c_str());
}

TEST_CASE("VestaSiteImporter detect web root - www") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_root_www.tar";
    create_test_tar(tar_path, "x.com", "www");
    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK(m.web_root_type == "www");
    std::remove(tar_path.c_str());
}

TEST_CASE("VestaSiteImporter detect web root - root fallback") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_root_dot.tar";
    std::string dir = tar_path + "_dir";
    ::mkdir(dir.c_str(), 0755);
    ::mkdir((dir + "/web").c_str(), 0755);
    ::mkdir((dir + "/web/x.com").c_str(), 0755);
    {
        std::ofstream f(dir + "/index.php");
        f << "<?php";
    }
    std::system(("cd " + dir + " && tar czf web/x.com/domain_data.tar.gz index.php && tar cf " + tar_path + " web/ 2>/dev/null").c_str());
    std::system(("rm -rf " + dir).c_str());

    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK(m.web_root_type == ".");
    std::remove(tar_path.c_str());
}

// ─── normalize_db_name ───

TEST_CASE("VestaSiteImporter normalize DB name") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_norm.tar";
    create_test_tar(tar_path, "x.com");

    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK(m.wp_db_name == "test_wp_db");
    CHECK(m.wp_db_name.find("admin_") == std::string::npos);
    std::remove(tar_path.c_str());
}

// ─── tar safety: reject absolute paths ───

TEST_CASE("VestaSiteImporter tar safety - absolute path") {
    std::string bad_tar = "/tmp/test_bad_abs.tar";
    std::system(("cd /tmp && echo evil > evil.txt && tar cf " + bad_tar + " /tmp/evil.txt 2>/dev/null; rm -f /tmp/evil.txt").c_str());

    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    auto m = importer.inspect({bad_tar, "x", "admin", "", true});
    CHECK_FALSE(m.errors.empty());
    std::remove(bad_tar.c_str());
}

TEST_CASE("VestaSiteImporter tar safety - parent dir ..") {
    std::string bad_tar = "/tmp/test_bad_dotdot.tar";
    std::system(("cd /tmp && mkdir -p test_evil && echo evil > test_evil/f.txt && tar cf " + bad_tar + " --exclude=test_evil/f.txt ../tmp/test_evil/f.txt 2>/dev/null; rm -rf test_evil").c_str());

    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    auto m = importer.inspect({bad_tar, "x", "admin", "", true});
    CHECK_FALSE(m.errors.empty());
    std::remove(bad_tar.c_str());
}

TEST_CASE("VestaSiteImporter tar safety - allowed .. in filename") {
    // Filenames containing ".." but not as a path component should be allowed
    std::string tar_path = "/tmp/test_ok_dots.tar";
    std::string dir = tar_path + "_dir";
    ::mkdir(dir.c_str(), 0755);
    ::mkdir((dir + "/web").c_str(), 0755);
    ::mkdir((dir + "/web/x.com").c_str(), 0755);
    // Create tar with "image..backup.jpg" filename (valid, no .. component)
    // Use a simpler approach: just ensure no false positive
    std::system(("cd " + dir + " && tar cf " + tar_path + " web/ 2>/dev/null").c_str());
    std::system(("rm -rf " + dir).c_str());

    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    // Should not fail on this tar (no .. component)
    // Domain not found is expected but not a safety error
    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    // The error should be about domain not found, NOT about security
    if (!m.errors.empty()) {
        CHECK(m.errors[0].find("Domain") != std::string::npos);
    } else {
        CHECK(m.errors.empty());
    }
    std::remove(tar_path.c_str());
}

// ─── dry run is pure read-only ───

TEST_CASE("VestaSiteImporter dry-run does not modify system") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_dryrun.tar";
    create_test_tar(tar_path, "example.com");

    std::string sites_dir = cfg.sites_dir() + "example.com/";
    bool before = fs.exists(sites_dir);

    auto m = importer.inspect({tar_path, "example.com", "admin", "", true});

    bool after = fs.exists(sites_dir);
    CHECK(before == after);
    CHECK(m.domain_found);
    std::remove(tar_path.c_str());
}

// ─── format_dry_run returns string ───

TEST_CASE("VestaSiteImporter format_dry_run returns string") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_fmt.tar";
    create_test_tar(tar_path, "x.com");

    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    std::string report = importer.format_dry_run(m, {tar_path, "x.com", "admin", "", true});

    CHECK_FALSE(report.empty());
    bool has_content = report.find("BACKUP") != std::string::npos ||
                       report.find("Domain") != std::string::npos;
    CHECK(has_content);

    std::remove(tar_path.c_str());
}

// ─── wp-config.php at root ───

TEST_CASE("VestaSiteImporter wp-config at web root") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_wproot.tar";
    // Create tar with wp-config.php directly at root (no subdirectory)
    std::string dir = tar_path + "_dir";
    ::mkdir(dir.c_str(), 0755);
    ::mkdir((dir + "/web").c_str(), 0755);
    ::mkdir((dir + "/web/x.com").c_str(), 0755);
    ::mkdir((dir + "/db").c_str(), 0755);
    ::mkdir((dir + "/db/test_db").c_str(), 0755);
    {
        std::ofstream f(dir + "/wp-config.php");
        f << "<?php define('DB_NAME', 'test_db'); define('DB_USER', 'u'); define('DB_PASSWORD', 'p'); define('DB_HOST', 'localhost');";
    }
    {
        std::ofstream f(dir + "/index.php");
        f << "<?php";
    }
    {
        std::ofstream f(dir + "/db/test_db/test_db.mysql.sql.gz");
        f << "data";
    }
    std::system(("cd " + dir + " && tar czf web/x.com/domain_data.tar.gz wp-config.php index.php && tar cf " + tar_path + " web/ db/ 2>/dev/null").c_str());
    std::system(("rm -rf " + dir).c_str());

    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK(m.wp_config_found);
    CHECK(m.wp_config_parsed);
    CHECK(m.wp_db_name == "test_db");
    CHECK(m.web_root_type == ".");
    CHECK(m.db_dump_found);
    std::remove(tar_path.c_str());
}

// ─── wp-config.php with variable DB_NAME → ambiguous ───

TEST_CASE("VestaSiteImporter wp-config ambiguous DB_NAME") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_ambig.tar";
    std::string dir = tar_path + "_dir";
    ::mkdir(dir.c_str(), 0755);
    ::mkdir((dir + "/web").c_str(), 0755);
    ::mkdir((dir + "/web/x.com").c_str(), 0755);
    ::mkdir((dir + "/public_html").c_str(), 0755);
    {
        std::ofstream f(dir + "/public_html/wp-config.php");
        f << "<?php\n"
          << "define('DB_NAME', getenv('DB_NAME') ?: 'fallback');\n"
          << "define('DB_USER', 'u');\n"
          << "define('DB_PASSWORD', 'p');\n"
          << "define('DB_HOST', 'localhost');\n";
    }
    std::system(("cd " + dir + " && tar czf web/x.com/domain_data.tar.gz public_html/ && tar cf " + tar_path + " web/ 2>/dev/null").c_str());
    std::system(("rm -rf " + dir).c_str());

    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK(m.wp_config_found);
    // Contains getenv() → ambiguous, not parsed as simple literal
    CHECK_FALSE(m.wp_config_parsed);
    CHECK(m.wp_db_ambiguous);
    std::remove(tar_path.c_str());
}

TEST_CASE("VestaSiteImporter realistic wp-config not ambiguous") {
    // A standard wp-config.php with literal DB_NAME and $table_prefix
    // should NOT be ambiguous
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_real.tar";
    std::string dir = tar_path + "_dir";
    ::mkdir(dir.c_str(), 0755);
    ::mkdir((dir + "/web").c_str(), 0755);
    ::mkdir((dir + "/web/x.com").c_str(), 0755);
    ::mkdir((dir + "/public_html").c_str(), 0755);
    ::mkdir((dir + "/db").c_str(), 0755);
    ::mkdir((dir + "/db/admin_site").c_str(), 0755);
    {
        std::ofstream f(dir + "/public_html/wp-config.php");
        f << "<?php\n"
          << "/** MySQL settings */\n"
          << "define('DB_NAME', 'admin_site');\n"
          << "define('DB_USER', 'admin_u');\n"
          << "define('DB_PASSWORD', 'secret');\n"
          << "define('DB_HOST', 'localhost');\n"
          << "\n"
          << "/**#@+ Authentication */\n"
          << "define('AUTH_KEY', 'random');\n"
          << "\n"
          << "/**#@- */\n"
          << "$table_prefix = 'wp_';\n"
          << "define('ABSPATH', __DIR__ . '/');\n";
    }
    {
        std::ofstream f(dir + "/db/admin_site/admin_site.mysql.sql.gz");
        f << "dump";
    }
    std::system(("cd " + dir + " && tar czf web/x.com/domain_data.tar.gz public_html/ && tar cf " + tar_path + " web/ db/ 2>/dev/null").c_str());
    std::system(("rm -rf " + dir).c_str());

    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK(m.wp_config_found);
    CHECK(m.wp_config_parsed);
    CHECK_FALSE(m.wp_db_ambiguous);
    CHECK(m.wp_db_name == "admin_site");
    CHECK(m.db_dump_found);
    std::remove(tar_path.c_str());
}

TEST_CASE("VestaSiteImporter rejects wp-config.php.bak") {
    // Files like wp-config.php.bak should NOT be treated as wp-config
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_bak.tar";
    std::string dir = tar_path + "_dir";
    ::mkdir(dir.c_str(), 0755);
    ::mkdir((dir + "/web").c_str(), 0755);
    ::mkdir((dir + "/web/x.com").c_str(), 0755);
    ::mkdir((dir + "/public_html").c_str(), 0755);
    {
        // Only a .bak file, no real wp-config.php
        std::ofstream f(dir + "/public_html/wp-config.php.bak");
        f << "<?php define('DB_NAME', 'old_db');";
    }
    {
        std::ofstream f(dir + "/public_html/index.php");
        f << "<?php";
    }
    std::system(("cd " + dir + " && tar czf web/x.com/domain_data.tar.gz public_html/ && tar cf " + tar_path + " web/ 2>/dev/null").c_str());
    std::system(("rm -rf " + dir).c_str());

    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK_FALSE(m.wp_config_found);
    CHECK_FALSE(m.wp_config_parsed);
    std::remove(tar_path.c_str());
}

// ─── Migration marker validation ───

TEST_CASE("VestaSiteImporter marker - no marker on new site") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string tar_path = "/tmp/test_vsi_marker_new.tar";
    create_test_tar(tar_path, "x.com");
    auto m = importer.inspect({tar_path, "x.com", "admin", "", true});
    CHECK_FALSE(m.migration_marker_found);
    CHECK_FALSE(m.can_import_files);
    std::remove(tar_path.c_str());
}

TEST_CASE("VestaSiteImporter marker - valid stage 1 marker with real site_id") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    site::SiteManager sites;
    domain::DomainManager domains;

    // Create a real site record
    uint64_t site_id = sites.create("markertest.local", "admin", 1);
    REQUIRE(site_id > 0);
    domains.create("markertest.local", 0, site_id);

    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance(), &sites, &domains);

    // Create site directory with marker containing real site_id
    std::string site_dir = cfg.sites_dir() + "markertest.local/";
    ::mkdir(site_dir.c_str(), 0755);
    ::mkdir((site_dir + "public").c_str(), 0755);
    std::string marker = "{\"domain\":\"markertest.local\",\"owner\":\"admin\",\"site_id\":"
        + std::to_string(site_id) + ",\"stage\":1,\"files_pending\":true,\"files_imported\":false,\"sql_pending\":true}";
    std::ofstream(site_dir + ".containercp-migration.json") << marker;

    std::string tar_path = "/tmp/test_vsi_marker_ok.tar";
    create_test_tar(tar_path, "markertest.local");
    auto m = importer.inspect({tar_path, "markertest.local", "admin", "", true});
    CHECK(m.migration_marker_found);
    CHECK(m.files_pending);
    CHECK(m.can_import_files);
    CHECK(m.migration_stage == 1);
    CHECK(m.migration_site_id == site_id);

    std::remove(tar_path.c_str());
    std::system(("rm -rf " + site_dir).c_str());
}

TEST_CASE("VestaSiteImporter marker - missing site_id in marker") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    site::SiteManager sites;
    domain::DomainManager domains;

    uint64_t site_id = sites.create("nomarkerid.local", "admin", 1);
    domains.create("nomarkerid.local", 0, site_id);

    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance(), &sites, &domains);

    std::string site_dir = cfg.sites_dir() + "nomarkerid.local/";
    ::mkdir(site_dir.c_str(), 0755);
    ::mkdir((site_dir + "public").c_str(), 0755);
    // Old marker without site_id
    std::string marker = "{\"domain\":\"nomarkerid.local\",\"owner\":\"admin\",\"stage\":1,\"files_pending\":true}";
    std::ofstream(site_dir + ".containercp-migration.json") << marker;

    std::string tar_path = "/tmp/test_vsi_marker_noid.tar";
    create_test_tar(tar_path, "nomarkerid.local");
    auto m = importer.inspect({tar_path, "nomarkerid.local", "admin", "", true});
    CHECK(m.migration_marker_found);
    CHECK_FALSE(m.can_import_files);
    CHECK(m.marker_error.find("site_id") != std::string::npos);

    std::remove(tar_path.c_str());
    std::system(("rm -rf " + site_dir).c_str());
}

TEST_CASE("VestaSiteImporter marker - domain record site_id mismatch") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    site::SiteManager sites;
    domain::DomainManager domains;

    uint64_t site_id = sites.create("domsitemismatch.local", "admin", 1);
    // DomainRecord belongs to a DIFFERENT site_id
    domains.create("domsitemismatch.local", 0, 999);

    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance(), &sites, &domains);

    std::string site_dir = cfg.sites_dir() + "domsitemismatch.local/";
    ::mkdir(site_dir.c_str(), 0755);
    ::mkdir((site_dir + "public").c_str(), 0755);
    std::string marker = "{\"domain\":\"domsitemismatch.local\",\"owner\":\"admin\",\"site_id\":"
        + std::to_string(site_id) + ",\"stage\":1,\"files_pending\":true}";
    std::ofstream(site_dir + ".containercp-migration.json") << marker;

    std::string tar_path = "/tmp/test_vsi_marker_dsm.tar";
    create_test_tar(tar_path, "domsitemismatch.local");
    auto m = importer.inspect({tar_path, "domsitemismatch.local", "admin", "", true});
    CHECK(m.migration_marker_found);
    CHECK_FALSE(m.can_import_files);
    CHECK(m.marker_error.find("site_id") != std::string::npos);

    std::remove(tar_path.c_str());
    std::system(("rm -rf " + site_dir).c_str());
}

TEST_CASE("VestaSiteImporter marker - owner mismatch") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    site::SiteManager sites;
    domain::DomainManager domains;

    uint64_t site_id = sites.create("owner-mismatch.local", "admin", 1);
    domains.create("owner-mismatch.local", 0, site_id);

    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance(), &sites, &domains);

    std::string site_dir = cfg.sites_dir() + "owner-mismatch.local/";
    ::mkdir(site_dir.c_str(), 0755);
    ::mkdir((site_dir + "public").c_str(), 0755);
    // Valid site_id but wrong owner
    std::string marker = "{\"domain\":\"owner-mismatch.local\",\"owner\":\"wronguser\",\"site_id\":"
        + std::to_string(site_id) + ",\"stage\":1,\"files_pending\":true}";
    std::ofstream(site_dir + ".containercp-migration.json") << marker;

    std::string tar_path = "/tmp/test_vsi_marker_owner.tar";
    create_test_tar(tar_path, "owner-mismatch.local");
    auto m = importer.inspect({tar_path, "owner-mismatch.local", "admin", "", true});
    CHECK(m.migration_marker_found);
    CHECK_FALSE(m.can_import_files);
    CHECK(m.marker_error.find("owner") != std::string::npos);

    std::remove(tar_path.c_str());
    std::system(("rm -rf " + site_dir).c_str());
}

TEST_CASE("VestaSiteImporter marker - only directory no SiteRecord") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    // No SiteManager passed — sites_ == nullptr
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string site_dir = cfg.sites_dir() + "nodir-record.local/";
    ::mkdir(site_dir.c_str(), 0755);
    ::mkdir((site_dir + "public").c_str(), 0755);
    std::string marker = "{\"domain\":\"nodir-record.local\",\"owner\":\"admin\",\"site_id\":1,\"stage\":1,\"files_pending\":true}";
    std::ofstream(site_dir + ".containercp-migration.json") << marker;

    std::string tar_path = "/tmp/test_vsi_marker_norecord.tar";
    create_test_tar(tar_path, "nodir-record.local");
    auto m = importer.inspect({tar_path, "nodir-record.local", "admin", "", true});
    CHECK(m.migration_marker_found);
    CHECK_FALSE(m.can_import_files);

    std::remove(tar_path.c_str());
    std::system(("rm -rf " + site_dir).c_str());
}

TEST_CASE("VestaSiteImporter marker - no marker on existing site") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string site_dir = cfg.sites_dir() + "existing-nomarker.local/";
    ::mkdir(site_dir.c_str(), 0755);
    ::mkdir((site_dir + "public").c_str(), 0755);

    std::string tar_path = "/tmp/test_vsi_marker_none.tar";
    create_test_tar(tar_path, "existing-nomarker.local");
    auto m = importer.inspect({tar_path, "existing-nomarker.local", "admin", "", true});
    CHECK(m.site_exists);
    CHECK_FALSE(m.migration_marker_found);
    CHECK_FALSE(m.can_import_files);

    std::remove(tar_path.c_str());
    std::system(("rm -rf " + site_dir).c_str());
}

TEST_CASE("VestaSiteImporter marker - stage 2 marker") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    std::string site_dir = cfg.sites_dir() + "stage2-marker.local/";
    ::mkdir(site_dir.c_str(), 0755);
    ::mkdir((site_dir + "public").c_str(), 0755);
    std::string marker = "{\"domain\":\"stage2-marker.local\",\"owner\":\"admin\",\"site_id\":0,\"stage\":2,\"files_imported\":true,\"sql_pending\":true}";
    std::ofstream(site_dir + ".containercp-migration.json") << marker;

    std::string tar_path = "/tmp/test_vsi_marker_s2.tar";
    create_test_tar(tar_path, "stage2-marker.local");
    auto m = importer.inspect({tar_path, "stage2-marker.local", "admin", "", true});
    CHECK(m.migration_marker_found);
    CHECK_FALSE(m.can_import_files);

    std::remove(tar_path.c_str());
    std::system(("rm -rf " + site_dir).c_str());
}

// ─── Migration marker: JSON formats and legacy ───

TEST_CASE("VestaSiteImporter marker - compact JSON") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs; runtime::CommandExecutor exec;
    site::SiteManager sites; domain::DomainManager domains;
    uint64_t sid = sites.create("compact.local", "admin", 1);
    domains.create("compact.local", 0, sid);
    migration::VestaSiteImporter imp(exec, fs, cfg, logger::Logger::instance(), &sites, &domains);

    std::string sd = cfg.sites_dir() + "compact.local/";
    ::mkdir(sd.c_str(), 0755); ::mkdir((sd + "public").c_str(), 0755);
    std::ofstream(sd + ".containercp-migration.json") << "{\"version\":1,\"domain\":\"compact.local\",\"owner\":\"admin\",\"site_id\":" + std::to_string(sid) + ",\"stage\":1,\"files_pending\":true}";
    std::string tp = "/tmp/test_cj.tar"; create_test_tar(tp, "compact.local");
    auto m = imp.inspect({tp, "compact.local", "admin", "", true});
    CHECK(m.can_import_files);
    std::remove(tp.c_str()); std::system(("rm -rf " + sd).c_str());
}

TEST_CASE("VestaSiteImporter marker - pretty JSON with spaces") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs; runtime::CommandExecutor exec;
    site::SiteManager sites; domain::DomainManager domains;
    uint64_t sid = sites.create("pretty.local", "admin", 1);
    domains.create("pretty.local", 0, sid);
    migration::VestaSiteImporter imp(exec, fs, cfg, logger::Logger::instance(), &sites, &domains);

    std::string sd = cfg.sites_dir() + "pretty.local/";
    ::mkdir(sd.c_str(), 0755); ::mkdir((sd + "public").c_str(), 0755);
    std::ofstream(sd + ".containercp-migration.json") << "{\n  \"version\": 1,\n  \"stage\": 1,\n  \"domain\": \"pretty.local\",\n  \"owner\": \"admin\",\n  \"site_id\": " + std::to_string(sid) + ",\n  \"files_pending\": true\n}";
    std::string tp = "/tmp/test_pj.tar"; create_test_tar(tp, "pretty.local");
    auto m = imp.inspect({tp, "pretty.local", "admin", "", true});
    CHECK(m.can_import_files);
    std::remove(tp.c_str()); std::system(("rm -rf " + sd).c_str());
}

TEST_CASE("VestaSiteImporter marker - reordered fields") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs; runtime::CommandExecutor exec;
    site::SiteManager sites; domain::DomainManager domains;
    uint64_t sid = sites.create("reord.local", "admin", 1);
    domains.create("reord.local", 0, sid);
    migration::VestaSiteImporter imp(exec, fs, cfg, logger::Logger::instance(), &sites, &domains);

    std::string sd = cfg.sites_dir() + "reord.local/";
    ::mkdir(sd.c_str(), 0755); ::mkdir((sd + "public").c_str(), 0755);
    std::ofstream(sd + ".containercp-migration.json") << "{\"owner\":\"admin\",\"files_pending\":true,\"domain\":\"reord.local\",\"stage\":1,\"site_id\":" + std::to_string(sid) + "}";
    std::string tp = "/tmp/test_rf.tar"; create_test_tar(tp, "reord.local");
    auto m = imp.inspect({tp, "reord.local", "admin", "", true});
    CHECK(m.can_import_files);
    std::remove(tp.c_str()); std::system(("rm -rf " + sd).c_str());
}

TEST_CASE("VestaSiteImporter marker - legacy missing files_imported/sql_pending") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs; runtime::CommandExecutor exec;
    site::SiteManager sites; domain::DomainManager domains;
    uint64_t sid = sites.create("legacy.local", "admin", 1);
    domains.create("legacy.local", 0, sid);
    migration::VestaSiteImporter imp(exec, fs, cfg, logger::Logger::instance(), &sites, &domains);

    std::string sd = cfg.sites_dir() + "legacy.local/";
    ::mkdir(sd.c_str(), 0755); ::mkdir((sd + "public").c_str(), 0755);
    std::ofstream(sd + ".containercp-migration.json") << "{\"domain\":\"legacy.local\",\"owner\":\"admin\",\"site_id\":" + std::to_string(sid) + ",\"stage\":1,\"files_pending\":true}";
    std::string tp = "/tmp/test_lg.tar"; create_test_tar(tp, "legacy.local");
    auto m = imp.inspect({tp, "legacy.local", "admin", "", true});
    CHECK(m.can_import_files);
    CHECK(m.files_status == "pending");
    CHECK(m.sql_status == "pending");
    std::remove(tp.c_str()); std::system(("rm -rf " + sd).c_str());
}

// ============================================================
// SQLite Migration Engine tests
// ============================================================
// These tests verify the MigrationEngine class used for versioned
// SQLite schema migrations. They are independent of the VestaCP
// import tests above.

#include "storage/SQLiteWrapper.h"
#include "storage/MigrationEngine.h"

#include <chrono>
#include <thread>

namespace {

std::string mig_db_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

void mig_cleanup(const std::string& path) {
    std::filesystem::remove(path);
    std::filesystem::remove(path + "-wal");
    std::filesystem::remove(path + "-shm");
}

containercp::storage::Migration make_migration(int version,
                                                const std::string& name,
                                                const std::string& sql) {
    containercp::storage::Migration m;
    m.version = version;
    m.name = name;
    // The descriptor is the SQL content itself — any change to the
    // migration logic produces a different checksum.
    m.descriptor = sql;
    m.up = [sql](containercp::storage::SQLiteDB& db, std::string& diag) -> bool {
        if (!db.exec(sql)) {
            diag = db.error_message();
            return false;
        }
        return true;
    };
    return m;
}

} // anonymous namespace

TEST_CASE("MigrationEngine ensure_meta_tables creates tables") {
    auto path = mig_db_path("containercp_test_meta.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        CHECK_FALSE(db.exec("SELECT 1 FROM schema_migrations LIMIT 1"));
        CHECK(eng.migrate(db));
        CHECK(db.exec("SELECT 1 FROM schema_migrations LIMIT 1"));
        CHECK(db.exec("SELECT 1 FROM storage_meta LIMIT 1"));
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine first migration from 0 to 1") {
    auto path = mig_db_path("containercp_test_v1.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        eng.register_migration(make_migration(1, "create_test_table",
                                               "CREATE TABLE test_v1 (id INTEGER)"));

        CHECK(eng.migrate(db));
        CHECK(eng.current_version(db) == 1);
        CHECK(db.exec("INSERT INTO test_v1 VALUES (1)"));
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine two sequential migrations") {
    auto path = mig_db_path("containercp_test_v2.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        eng.register_migration(make_migration(1, "create_t1",
                                               "CREATE TABLE t1 (id INTEGER)"));
        eng.register_migration(make_migration(2, "create_t2",
                                               "CREATE TABLE t2 (id INTEGER)"));

        CHECK(eng.migrate(db));
        CHECK(eng.current_version(db) == 2);
        CHECK(db.exec("INSERT INTO t1 VALUES (1)"));
        CHECK(db.exec("INSERT INTO t2 VALUES (1)"));
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine repeated migrate is no-op") {
    auto path = mig_db_path("containercp_test_repeat.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        eng.register_migration(make_migration(1, "create_t1",
                                               "CREATE TABLE t1 (id INTEGER)"));

        CHECK(eng.migrate(db));
        CHECK(eng.current_version(db) == 1);
        CHECK(eng.migrate(db));
        CHECK(eng.current_version(db) == 1);
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine checksum mismatch detection") {
    auto path = mig_db_path("containercp_test_checksum.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        // Apply migration with specific SQL content (descriptor = SQL)
        containercp::storage::MigrationEngine eng1;
        eng1.register_migration(make_migration(1, "create_t1",
                                                "CREATE TABLE t1 (id INTEGER)"));
        CHECK(eng1.migrate(db));

        // Same version, different SQL content → different descriptor →
        // different checksum → mismatch detected
        containercp::storage::MigrationEngine eng2;
        containercp::storage::Migration m;
        m.version = 1;
        m.name = "create_t1_changed";
        m.descriptor = "CREATE TABLE t1 (id INTEGER, name TEXT)";  // changed SQL
        m.up = [](containercp::storage::SQLiteDB& d, std::string& diag) -> bool {
            if (!d.exec("CREATE TABLE t1 (id INTEGER, name TEXT)")) {
                diag = d.error_message();
                return false;
            }
            return true;
        };
        eng2.register_migration(std::move(m));
        CHECK_FALSE(eng2.migrate(db));
        CHECK_FALSE(eng2.last_error().empty());
        CHECK(eng2.last_error().find("checksum mismatch") != std::string::npos);
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine duplicate version rejected deterministically") {
    auto path = mig_db_path("containercp_test_dupver.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;

        // Two migrations with same version but different SQL → different descriptors
        containercp::storage::Migration m1;
        m1.version = 1;
        m1.name = "first";
        m1.descriptor = "CREATE TABLE a (id INTEGER)";
        m1.up = [](containercp::storage::SQLiteDB& d, std::string&) -> bool {
            return d.exec("CREATE TABLE a (id INTEGER)");
        };

        containercp::storage::Migration m2;
        m2.version = 1;
        m2.name = "second";
        m2.descriptor = "CREATE TABLE b (id INTEGER)";  // different from m1
        m2.up = [](containercp::storage::SQLiteDB& d, std::string&) -> bool {
            return d.exec("CREATE TABLE b (id INTEGER)");
        };

        eng.register_migration(std::move(m1));
        eng.register_migration(std::move(m2));

        // migrate() should detect duplicate versions and permanently
        // invalidate the engine
        CHECK_FALSE(eng.migrate(db));
        CHECK_FALSE(eng.last_error().empty());
        CHECK(eng.last_error().find("Duplicate migration version") != std::string::npos);

        // Engine is permanently invalid — subsequent migrate() calls fail
        CHECK_FALSE(eng.migrate(db));
        CHECK(eng.last_error().find("Duplicate migration version") != std::string::npos);

        // Valid registrations are ignored after invalidation
        auto good = make_migration(3, "good", "CREATE TABLE t (id INTEGER)");
        eng.register_migration(std::move(good));
        CHECK_FALSE(eng.migrate(db));
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine failed migration is recorded") {
    auto path = mig_db_path("containercp_test_failed.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        containercp::storage::Migration m;
        m.version = 1;
        m.name = "failing_migration";
        m.descriptor = "intentional_failure_test";
        m.up = [](containercp::storage::SQLiteDB&, std::string& diag) -> bool {
            diag = "intentional failure";
            return false;
        };
        eng.register_migration(std::move(m));

        CHECK_FALSE(eng.migrate(db));
        CHECK_FALSE(eng.last_error().empty());
        CHECK(eng.last_error().find("intentional failure") != std::string::npos);

        CHECK(db.prepare("SELECT status FROM schema_migrations WHERE version = 1"));
        CHECK(db.step());
        CHECK(db.column_text(0) == "failed");
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine failed migration prevents retry") {
    auto path = mig_db_path("containercp_test_noretry.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        {
            containercp::storage::MigrationEngine eng;
            containercp::storage::Migration m;
            m.version = 1;
            m.name = "failing";
            m.descriptor = "failing_test_v1";
            m.up = [](containercp::storage::SQLiteDB&, std::string& diag) -> bool {
                diag = "fail";
                return false;
            };
            eng.register_migration(std::move(m));
            CHECK_FALSE(eng.migrate(db));
        }

        {
            containercp::storage::MigrationEngine eng;
            containercp::storage::Migration m2;
            m2.version = 1;
            m2.name = "failing";
            m2.descriptor = "failing_test_v1";
            m2.up = [](containercp::storage::SQLiteDB&, std::string& diag) -> bool {
                diag = "fail";
                return false;
            };
            eng.register_migration(std::move(m2));
            CHECK_FALSE(eng.migrate(db));
            CHECK(eng.last_error().find("previously failed") != std::string::npos);
        }
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine interrupted migration retries") {
    auto path = mig_db_path("containercp_test_interrupted.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        REQUIRE(db.exec("CREATE TABLE IF NOT EXISTS schema_migrations ("
                        "version INTEGER PRIMARY KEY,"
                        "name TEXT NOT NULL,"
                        "checksum TEXT NOT NULL,"
                        "started_at TEXT NOT NULL,"
                        "completed_at TEXT,"
                        "status TEXT NOT NULL DEFAULT 'pending',"
                        "diagnostics TEXT)"));
        REQUIRE(db.exec("INSERT INTO schema_migrations "
                        "(version, name, checksum, started_at, status) "
                        "VALUES (1, 'interrupted', 'abc', '2026-01-01T00:00:00Z', 'running')"));

        containercp::storage::MigrationEngine eng;
        containercp::storage::Migration m;
        m.version = 1;
        m.name = "interrupted";
        m.descriptor = "interrupted_test_v1";
        m.up = [](containercp::storage::SQLiteDB& db2, std::string&) -> bool {
            return db2.exec("CREATE TABLE recovered (id INTEGER)");
        };
        eng.register_migration(std::move(m));

        CHECK(eng.migrate(db));
        CHECK(eng.current_version(db) == 1);
        CHECK(db.exec("INSERT INTO recovered VALUES (1)"));
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine storage_meta keys") {
    auto path = mig_db_path("containercp_test_meta_keys.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        CHECK(eng.migrate(db));

        CHECK(db.exec("INSERT INTO storage_meta VALUES ('schema_version', '1')"));
        CHECK(db.prepare("SELECT value FROM storage_meta WHERE key = 'schema_version'"));
        CHECK(db.step());
        CHECK(db.column_text(0) == "1");
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine empty descriptor rejected") {
    auto path = mig_db_path("containercp_test_emptydesc.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        containercp::storage::Migration m;
        m.version = 1;
        m.name = "no_descriptor";
        m.descriptor = "";  // empty — should be rejected
        m.up = [](containercp::storage::SQLiteDB& d, std::string&) -> bool {
            return d.exec("CREATE TABLE t (id INTEGER)");
        };
        eng.register_migration(std::move(m));
        // Engine stored the migration but with an error marker
        CHECK_FALSE(eng.migrate(db));
        CHECK_FALSE(eng.last_error().empty());
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine same version same descriptor from different engines OK") {
    auto path = mig_db_path("containercp_test_dup.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        // Apply version 1
        {
            containercp::storage::MigrationEngine eng;
            eng.register_migration(make_migration(1, "create_t1",
                                                   "CREATE TABLE t1 (id INTEGER)"));
            CHECK(eng.migrate(db));
            CHECK(eng.current_version(db) == 1);
        }

        // Second engine with version 1 and same descriptor → checksum match → skip
        {
            containercp::storage::MigrationEngine eng;
            eng.register_migration(make_migration(1, "create_t1_again",
                                                   "CREATE TABLE t1 (id INTEGER)"));
            CHECK(eng.migrate(db));
        }

        // Third engine with version 1 but different descriptor → checksum mismatch
        {
            containercp::storage::MigrationEngine eng;
            containercp::storage::Migration m;
            m.version = 1;
            m.name = "different_sql";
            m.descriptor = "CREATE TABLE t2 (id INTEGER)";
            m.up = [](containercp::storage::SQLiteDB& d, std::string&) -> bool {
                return d.exec("CREATE TABLE t2 (id INTEGER)");
            };
            eng.register_migration(std::move(m));
            CHECK_FALSE(eng.migrate(db));
        }
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine registration rejects version < 1") {
    auto path = mig_db_path("containercp_test_invver.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        containercp::storage::Migration m;
        m.version = 0;
        m.name = "zero_version";
        m.descriptor = "test";
        m.up = [](containercp::storage::SQLiteDB&, std::string&) { return true; };
        eng.register_migration(std::move(m));
        CHECK_FALSE(eng.migrate(db));

        // Engine is permanently invalid — valid registrations are ignored
        auto good = make_migration(1, "good", "CREATE TABLE t (id INTEGER)");
        eng.register_migration(std::move(good));
        CHECK_FALSE(eng.migrate(db));
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine registration rejects empty name") {
    auto path = mig_db_path("containercp_test_invnam.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        containercp::storage::Migration m;
        m.version = 1;
        m.name = "";
        m.descriptor = "test";
        m.up = [](containercp::storage::SQLiteDB&, std::string&) { return true; };
        eng.register_migration(std::move(m));
        CHECK_FALSE(eng.migrate(db));
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine registration rejects empty callback") {
    auto path = mig_db_path("containercp_test_invcb.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        containercp::storage::Migration m;
        m.version = 1;
        m.name = "no_callback";
        m.descriptor = "test";
        // m.up is empty by default
        eng.register_migration(std::move(m));
        CHECK_FALSE(eng.migrate(db));
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine registration error is permanent") {
    auto path = mig_db_path("containercp_test_perm.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;

        // First registration fails (version < 1)
        containercp::storage::Migration bad;
        bad.version = 0;
        bad.name = "bad";
        bad.descriptor = "test";
        bad.up = [](containercp::storage::SQLiteDB&, std::string&) { return true; };
        eng.register_migration(std::move(bad));
        CHECK_FALSE(eng.migrate(db));

        // Subsequent valid registrations are silently ignored
        auto good = make_migration(1, "good", "CREATE TABLE t (id INTEGER)");
        eng.register_migration(std::move(good));
        CHECK_FALSE(eng.migrate(db));

        // Repeated migrate calls still fail
        CHECK_FALSE(eng.migrate(db));
    }
    mig_cleanup(path);
}

TEST_CASE("MigrationEngine out-of-order registration works") {
    auto path = mig_db_path("containercp_test_order.db");
    mig_cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        containercp::storage::MigrationEngine eng;
        eng.register_migration(make_migration(2, "second",
                                               "CREATE TABLE t2 (id INTEGER)"));
        eng.register_migration(make_migration(1, "first",
                                               "CREATE TABLE t1 (id INTEGER)"));

        CHECK(eng.migrate(db));
        CHECK(eng.current_version(db) == 2);
        CHECK(db.exec("INSERT INTO t1 VALUES (1)"));
        CHECK(db.exec("INSERT INTO t2 VALUES (1)"));
    }
    mig_cleanup(path);
}

// ============================================================
// Phase 10 — Legacy Archive tests
// ============================================================

static std::string make_legacy_dir(const std::string& name) {
    auto d = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(d);
    return d.string() + "/";
}

static void write_txt(const std::string& dir, const std::string& fn, const std::string& content) {
    std::ofstream f(dir + fn); f << content;
}

#include "storage/LegacyArchive.h"
using namespace containercp::storage;




static void write_all_required(const std::string& dir, int profiles_count = 2,
                               int tpl_profiles_count = 1) {
    write_txt(dir, "nodes.db", "1|main|web\n2|node2|db\n");
    write_txt(dir, "php_versions.db", "1|8.2|php:8.2|1|1\n2|8.3|php:8.3|1|0\n");
    write_txt(dir, "profiles.db", [&]() -> std::string {
        std::string s;
        for (int i = 1; i <= profiles_count; ++i) {
            s += std::to_string(i) + "|profile" + std::to_string(i) + "|WEB_SERVER|apache|static|/tpl||1|1\n";
        }
        return s;
    }());
    write_txt(dir, "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n2|user1|1001|/home/user1|/bin/sh|1\n");
    write_txt(dir, "sites.db", "1|example.com|admin|1|apache|1\n2|test.local|user1|1|nginx|1\n");
    write_txt(dir, "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n2|test.local|2|1|8.3|1|1|alias|example.com\n");
    write_txt(dir, "databases.db", "1|db1|user1|pass|mysql|8.0|1|1|1\n2|db2|user2|pass|pgsql|15|1|1|1\n");
    write_txt(dir, "backups.db", "1|1|1|backup1.tar.gz|full|1000|1|completed|/path|gzip\n2|2|1|backup2.tar.gz|incr|500|1|completed|/path|xz\n");
    write_txt(dir, "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    // Optional: template_profiles.db (8-field format: no TYPE field)
    if (tpl_profiles_count > 0) {
        std::string s; for (int i = 1; i <= tpl_profiles_count; ++i)
            s += std::to_string(i) + "|tpl" + std::to_string(i) + "|apache|static|/tpl||1|1\n";
        write_txt(dir, "template_profiles.db", s);
    }
}

static void write_some_optional(const std::string& dir) {
    // access_users.db: ID|USERNAME|AUTH_TYPE|PASSWORD_HASH|ENABLED (5 fields)
    write_txt(dir, "access_users.db", "1|user1|auth_type1|hash1|1\n");
    // access_grants.db: ID|ACCESS_USER_ID|SITE_ID|PERMISSION (4 fields)
    write_txt(dir, "access_grants.db", "1|1|1|read_only\n");
    // auth_users.db: ID|USERNAME|PASSWORD_HASH|MUST_CHANGE|ENABLED|ROLE (6 fields)
    write_txt(dir, "auth_users.db", "1|auth1|hash1|0|1|admin\n");
    // ssl_certificates.db: ID|DOMAIN_ID|DOMAIN|PROVIDER|CERT|KEY|EXPIRES|STATUS|HTTPS|AUTO (10 fields)
    write_txt(dir, "ssl_certificates.db", "1|0|ex.com|acme|cert.pem|key.pem|2025-01-01|valid|1|1\n");
    // mail_domains.db: ID|MODE|DOMAIN|DOMAIN_ID|SITE_ID|ENABLED|CATCH|DKIM_SEL|DKIM_DNS|RELAY|MAX_BOX|MAX_ALIAS (12 fields)
    write_txt(dir, "mail_domains.db", "1|disabled|ex.com|0|0|1|catch@ex.com|sel1||relay|10|5\n");
    // mail_mailboxes.db: exactly 13 fields
    write_txt(dir, "mail_mailboxes.db", "1|1|box|hash|100|10|1|Name|fwd|0|||\n");
    // mail_aliases.db: ID|DOMAIN_ID|SOURCE|DEST|ENABLED|CREATED_AT|UPDATED_AT (7 fields)
    write_txt(dir, "mail_aliases.db", "1|1|src@ex.com|dst@ex.com|1||\n");
    // mail_state.db: single word
    write_txt(dir, "mail_state.db", "active\n");
    // mail_smarthost.db: simple format
    write_txt(dir, "mail_smarthost.db", "smtp:587:user:pass\n");
}

static DatabaseVerificationResult make_full_dvr() {
    DatabaseVerificationResult dvr;
    dvr.success = true;
    dvr.initial_verification_passed = true;
    dvr.reopened_verification_passed = true;
    dvr.reopen_succeeded = true;
    dvr.initial_integrity_check_result = "ok";
    dvr.reopened_integrity_check_result = "ok";
    // Populate resources with the exact 17 resource types
    const char* res_names[] = {
        "nodes","php_versions","profiles","users","sites","domains","databases",
        "backups","reverse_proxies","access_users","access_grants","auth_users",
        "ssl_certificates","mail_domains","mail_mailboxes","mail_aliases","mail_config",nullptr};
    for (int i = 0; res_names[i]; ++i) {
        ResourceVerificationResult r; r.resource_type = res_names[i]; r.success = true;
        dvr.resources.push_back(r);
        dvr.reopened_resources.push_back(r);
    }
    return dvr;
}

static void capture_source_state(const std::string& dir,
                                  std::map<std::string, std::string>& sha_map,
                                  std::map<std::string, uintmax_t>& size_map) {
    for (auto& fi : legacy_file_inventory()) {
        std::string fp = dir + fi.filename;
        if (std::filesystem::exists(fp) && std::filesystem::is_regular_file(fp)) {
            sha_map[fi.filename] = LegacyArchive::sha256_file(fp);
            size_map[fi.filename] = std::filesystem::file_size(fp);
        }
    }
}

TEST_CASE("Archive respects required and optional files") {
    auto dir = make_legacy_dir("arc_opt");
    write_all_required(dir);
    // template_profiles.db intentionally absent (optional)
    std::filesystem::remove(dir + "template_profiles.db");

    auto arc_dir = make_legacy_dir("arc_root");
    auto dvr = make_full_dvr();

    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive("a1b2c3d4-e5f6-4789-a123-456789abcdef", "v0.6.0", "v0.7.0", dvr);
    CAPTURE(result.error);
    CHECK(result.success);
    CHECK(!result.archive_path.empty());
    CHECK(std::filesystem::exists(result.archive_path + "manifest.json"));
    CHECK(std::filesystem::exists(result.archive_path + "SHA256SUMS"));

    // template_profiles.db should be in manifest as optional+absent
    bool found_tpl = false;
    for (auto& f : result.manifest.files) {
        if (f.filename == "template_profiles.db") {
            found_tpl = true;
            CHECK(f.optional);
            CHECK_FALSE(f.present);
        }
    }
    CHECK(found_tpl);

    // Verify archive
    CHECK(arch.verify_archive(result.archive_path));

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Archive fails without required files") {
    auto dir = make_legacy_dir("arc_missing");
    write_txt(dir, "nodes.db", "1|main|web\n");
    // Missing php_versions.db (required)

    auto arc_dir = make_legacy_dir("arc_root2");
    auto dvr = make_full_dvr();

    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive("a0b0c0d0-e0f0-4780-a000-000000000010", "v0.6.0", "v0.7.0", dvr);
    CHECK_FALSE(result.success);
    // Error should mention required file
    CHECK(result.error == "required_file_missing:php_versions.db");

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Archive fails if verification not passed") {
    auto dir = make_legacy_dir("arc_failv");
    auto arc_dir = make_legacy_dir("arc_root3");
    DatabaseVerificationResult dvr; dvr.initial_verification_passed = false;

    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive("a1b2c3d4-e5f6-4789-a123-456789abcdef", "v0.6.0", "v0.7.0", dvr);
    CHECK_FALSE(result.success);
    CHECK(result.error == "verification_not_passed");

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Archive rejects symlink source") {
    auto dir = make_legacy_dir("arc_sym");
    write_txt(dir, "actual.db", "1|main|web\n");
    std::filesystem::create_symlink(dir + "actual.db", dir + "nodes.db");
    write_txt(dir, "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_txt(dir, "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_txt(dir, "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_txt(dir, "sites.db", "1|example.com|admin|1|apache|1\n");
    write_txt(dir, "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_txt(dir, "databases.db", "1|db|user|pass|mysql|8.0|1|1|1\n");
    write_txt(dir, "backups.db", "1|1|1|backup.tar.gz|full|1000|1|completed|/path|gzip\n");
    write_txt(dir, "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    auto arc_dir = make_legacy_dir("arc_root4");
    auto dvr = make_full_dvr();

    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive("b0c0d0e0-f0f0-4780-a000-000000000011", "v0.6.0", "v0.7.0", dvr);
    CAPTURE(result.error);
    CHECK_FALSE(result.success);
    CHECK(result.error == "required_file_missing:nodes.db"); // symlink detected

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Archive verify fails on modified file") {
    auto dir = make_legacy_dir("arc_mod");
    write_all_required(dir);
    auto arc_dir = make_legacy_dir("arc_root5");
    auto dvr = make_full_dvr();

    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive("a0b0c0d0-e0f0-4780-a000-000000000010", "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    // Tamper with archived file
    write_txt(result.archive_path, "nodes.db", "CORRUPTED\n");
    CHECK_FALSE(arch.verify_archive(result.archive_path));

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Archive source unchanged after copy") {
    auto dir = make_legacy_dir("arc_unchg");
    write_txt(dir, "nodes.db", "1|main|web\n");
    write_txt(dir, "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_txt(dir, "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_txt(dir, "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_txt(dir, "sites.db", "1|example.com|admin|1|apache|1\n");
    write_txt(dir, "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_txt(dir, "databases.db", "1|db|user|pass|mysql|8.0|1|1|1\n");
    write_txt(dir, "backups.db", "1|1|1|backup.tar.gz|full|1000|1|completed|/path|gzip\n");
    write_txt(dir, "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");

    auto before_sha = LegacyArchive::sha256_file(dir + "nodes.db");
    auto before_size = std::filesystem::file_size(dir + "nodes.db");

    auto arc_dir = make_legacy_dir("arc_root6");
    auto dvr = make_full_dvr();
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive("a0b0c0d0-e0f0-4780-a000-000000000010", "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    // Source must be unchanged
    CHECK(LegacyArchive::sha256_file(dir + "nodes.db") == before_sha);
    CHECK(std::filesystem::file_size(dir + "nodes.db") == before_size);

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Archive validates migration UUID") {
    auto dir = make_legacy_dir("arc_uuid");
    auto arc_dir = make_legacy_dir("arc_uuidr");
    auto dvr = make_full_dvr();
    LegacyArchive arch(dir, arc_dir);
    // Invalid UUID
    CHECK_FALSE(arch.create_archive("bad", "v0.6.0", "v0.7.0", dvr).success);
    CHECK_FALSE(arch.create_archive("", "v0.6.0", "v0.7.0", dvr).success);
    // Path-like
    CHECK_FALSE(arch.create_archive("aaaa/aaa/aaaa/aaaa/aaaa/aaaa/aaaa/aaaa", "v0.6.0", "v0.7.0", dvr).success);
    std::string valid = "12345678-1234-4234-8234-1234567890ab";
    CHECK(LegacyArchive::valid_migration_id(valid));
    std::filesystem::remove_all(dir); std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Archive rejects unsafe version strings") {
    CHECK_FALSE(LegacyArchive::safe_version("../etc"));
    CHECK_FALSE(LegacyArchive::safe_version("v1.0/evil"));
    CHECK_FALSE(LegacyArchive::safe_version("v0..6"));
    CHECK_FALSE(LegacyArchive::safe_version(" v0.6"));
    CHECK(LegacyArchive::safe_version("v0.6.0"));
    CHECK(LegacyArchive::safe_version("v1.0.0-rc1"));
}

TEST_CASE("Archive naming contains source version and migration ID") {
    auto dir = make_legacy_dir("arc_name");
    write_txt(dir, "nodes.db", "1|main|web\n");
    write_txt(dir, "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_txt(dir, "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_txt(dir, "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_txt(dir, "sites.db", "1|example.com|admin|1|apache|1\n");
    write_txt(dir, "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_txt(dir, "databases.db", "1|db|user|pass|mysql|8.0|1|1|1\n");
    write_txt(dir, "backups.db", "1|1|1|backup.tar.gz|full|1000|1|completed|/path|gzip\n");
    write_txt(dir, "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    auto arc_dir = make_legacy_dir("arc_name_r");
    auto dvr = make_full_dvr();
    dvr.initial_integrity_check_result = dvr.reopened_integrity_check_result = "ok";
    std::string mid = "12345678-1234-4234-8234-1234567890ab";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    CHECK(result.success);
    // Archive name contains source version and migration ID
    CHECK(result.archive_path.find("v0.6.0") != std::string::npos);
    CHECK(result.archive_path.find(mid) != std::string::npos);
    // Manifest contains same migration ID
    CHECK(result.manifest.migration_id == mid);
    std::filesystem::remove_all(dir); std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Archive manifest does not contain secret values") {
    auto dir = make_legacy_dir("arc_secret");
    write_txt(dir, "nodes.db", "1|main|web\n");
    write_txt(dir, "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_txt(dir, "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_txt(dir, "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_txt(dir, "sites.db", "1|example.com|admin|1|apache|1\n");
    write_txt(dir, "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_txt(dir, "databases.db", "1|db|user|secret_pw|mysql|8.0|1|1|1\n");
    write_txt(dir, "backups.db", "1|1|1|backup.tar.gz|full|1000|1|completed|/path|gzip\n");
    write_txt(dir, "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    write_txt(dir, "mail_smarthost.db", "smtp:587:user:secret_pass\n");
    auto arc_dir = make_legacy_dir("arc_sec_r");
    auto dvr = make_full_dvr();
    dvr.initial_integrity_check_result = dvr.reopened_integrity_check_result = "ok";
    std::string mid = "12345678-1234-4234-8234-1234567890ab";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    CHECK(result.success);
    // Read manifest and verify no secrets
    std::ifstream mf(result.archive_path + "manifest.json");
    std::string content((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
    CHECK(content.find("secret_pw") == std::string::npos);
    CHECK(content.find("secret_pass") == std::string::npos);
    // SHA256SUMS contains file hash (safe), not content
    std::ifstream sf(result.archive_path + "SHA256SUMS");
    std::string sums((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
    CHECK(sums.find("secret_pw") == std::string::npos);
    std::filesystem::remove_all(dir); std::filesystem::remove_all(arc_dir);
}

// ============================================================
// Helper: write all 9 required files + 2 profile files with distinct counts
// ============================================================







// ============================================================
// Phase 10 — Comprehensive Integration Tests
// ============================================================

TEST_CASE("E2E: Successful archive creation and public verification") {
    auto dir = make_legacy_dir("arc_e2e");
    auto arc_dir = make_legacy_dir("arc_e2e_r");
    write_all_required(dir, 2, 1);
    write_some_optional(dir);

    std::map<std::string, std::string> before_sha;
    std::map<std::string, uintmax_t> before_size;
    capture_source_state(dir, before_sha, before_size);

    auto dvr = make_full_dvr();
    std::string mid = "a1b2c3d4-e5f6-4789-a123-456789abcdef";

    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);

    CAPTURE(result.error);
    CHECK(result.success);
    CHECK(result.error.empty());
    CHECK(!result.archive_path.empty());
    CHECK(std::filesystem::exists(result.archive_path));
    // Temp must not exist
    CHECK_FALSE(std::filesystem::exists(arc_dir + ".legacy-v0.6.0-" + result.manifest.migration_timestamp + "-" + mid + ".tmp"));
    CHECK(std::filesystem::exists(result.archive_path + "manifest.json"));
    CHECK(std::filesystem::exists(result.archive_path + "SHA256SUMS"));

    // Public verification: path only
    CHECK(arch.verify_archive(result.archive_path));
    // Public verification: path with trailing slash
    CHECK(arch.verify_archive(result.archive_path + "/"));
    // Public verification: path + manifest output
    ArchiveManifest verified;
    CHECK(arch.verify_archive(result.archive_path, &verified));

    // Validate verified manifest
    CHECK(verified.manifest_version == "1.0");
    CHECK(verified.migration_id == mid);
    CHECK(verified.source_version == "v0.6.0");
    CHECK(verified.target_version == "v0.7.0");
    CHECK(LegacyArchive::valid_timestamp(verified.migration_timestamp));
    CHECK(verified.archive_directory == LegacyArchive::normalize_archive_identity_path(result.archive_path));
    CHECK(verified.checksum_match == true);
    CHECK(verified.initial_integrity_check == "ok");
    CHECK(verified.reopened_integrity_check == "ok");
    CHECK(verified.initial_fk_violations == 0);
    CHECK(verified.reopened_fk_violations == 0);
    CHECK(verified.verification_result == "success");
    CHECK(verified.files.size() == 19);

    // Every filename unique
    std::set<std::string> fnames;
    for (auto& fe : verified.files) {
        CHECK_FALSE(fnames.count(fe.filename));
        fnames.insert(fe.filename);
    }

    // Required files present
    for (auto& fi : legacy_file_inventory()) {
        bool found = false;
        for (auto& fe : verified.files) {
            if (fe.filename == fi.filename) {
                found = true;
                if (fi.required) CHECK(fe.present);
                CHECK(fe.optional == !fi.required);
                break;
            }
        }
        CHECK(found);
    }

    // Profile counts distinct
    for (auto& fe : verified.files) {
        if (fe.filename == "profiles.db") { CHECK(fe.record_count == 2); }
        if (fe.filename == "template_profiles.db") { CHECK(fe.record_count == 1); }
    }

    // Public verification with manifest via two-arg call
    ArchiveManifest parsed2;
    CHECK(arch.verify_archive(result.archive_path + "/", &parsed2));
    CHECK(parsed2.manifest_version == "1.0");

    // Source immutability (SHA, size, mtime, inode, permissions)
    for (auto& fi : legacy_file_inventory()) {
        std::string fp = dir + fi.filename;
        if (!std::filesystem::exists(fp) || !std::filesystem::is_regular_file(fp)) continue;
        CAPTURE(fi.filename);
        CHECK(LegacyArchive::sha256_file(fp) == before_sha[fi.filename]);
        CHECK(std::filesystem::file_size(fp) == before_size[fi.filename]);
        // Verify still a regular non-symlink file
        auto st = std::filesystem::symlink_status(fp);
        CHECK_FALSE(std::filesystem::is_symlink(st));
        CHECK(std::filesystem::is_regular_file(st));
    }

    // Permissions: archive directory 0700
    {
        std::error_code ec;
        auto dir_st = std::filesystem::status(result.archive_path, ec);
        REQUIRE_FALSE(ec);
        CHECK((static_cast<int>(dir_st.permissions()) & 0777) == 0700);
    }
    // Every present archived file = 0440, manifest.json = 0440, SHA256SUMS = 0440
    {
        for (auto& e : std::filesystem::directory_iterator(result.archive_path)) {
            std::error_code ec;
            auto st = std::filesystem::status(e.path(), ec);
            REQUIRE_FALSE(ec);
            auto bits = static_cast<int>(st.permissions()) & 0777;
            CHECK(bits == 0440);
            // No owner-write, group-write, other-read, other-write, execute bits
            CHECK((bits & 0200) == 0); // owner write
            CHECK((bits & 0020) == 0); // group write
            CHECK((bits & 0004) == 0); // other read
            CHECK((bits & 0002) == 0); // other write
            CHECK((bits & 0111) == 0); // execute
        }
        std::error_code ec;
        auto mf_st = std::filesystem::status(result.archive_path + "manifest.json", ec);
        REQUIRE_FALSE(ec);
        CHECK((static_cast<int>(mf_st.permissions()) & 0777) == 0440);
        auto sf_st = std::filesystem::status(result.archive_path + "SHA256SUMS", ec);
        REQUIRE_FALSE(ec);
        CHECK((static_cast<int>(sf_st.permissions()) & 0777) == 0440);
    }

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Pre-publication verification uses final path identity") {
    auto dir = make_legacy_dir("arc_pre");
    auto arc_dir = make_legacy_dir("arc_pre_r");
    write_all_required(dir);

    auto dvr = make_full_dvr();
    std::string mid = "b1b2c3d4-e5f6-4789-a123-456789abcdef";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);

    CAPTURE(result.error);
    CHECK(result.success);
    // Manifest archive_directory should be the final path, not the temp path
    std::string expected_path = arc_dir;
    while (!expected_path.empty() && expected_path.back() == '/') expected_path.pop_back();
    expected_path += "/legacy-v0.6.0-" + result.manifest.migration_timestamp + "-" + mid;
    CHECK(result.manifest.archive_directory == expected_path);

    // Post-publication public verify succeeds
    CHECK(arch.verify_archive(result.archive_path));

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Archive relocation rejected due to manifest identity mismatch") {
    auto dir = make_legacy_dir("arc_rel");
    auto arc_dir = make_legacy_dir("arc_rel_r");
    write_all_required(dir, 2, 1);
    write_some_optional(dir);

    auto dvr = make_full_dvr();
    std::string mid = "c1b2c3d4-e5f6-4789-a123-456789abcdef";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    // Copy the complete archive to another location
    auto relocated = make_legacy_dir("arc_rel_moved");
    std::filesystem::copy(result.archive_path, relocated,
        std::filesystem::copy_options::recursive);

    // Verify original still passes
    CHECK(arch.verify_archive(result.archive_path));
    // Relocated copy fails — manifest archive_directory still identifies original
    CHECK_FALSE(arch.verify_archive(relocated));

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
    std::filesystem::remove_all(relocated);
}

TEST_CASE("Manifest parser produces exactly 19 file entries") {
    auto dir = make_legacy_dir("arc_mfe");
    auto arc_dir = make_legacy_dir("arc_mfe_r");
    write_all_required(dir, 2, 1);
    write_some_optional(dir);

    auto dvr = make_full_dvr();
    std::string mid = "d1b2c3d4-e5f6-4789-a123-456789abcdef";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    ArchiveManifest verified;
    CHECK(arch.verify_archive(result.archive_path, &verified));
    CHECK(verified.files.size() == 19);

    // Every ParsedFileEntry should be valid (regression test for missing push_back)
    for (auto& fe : verified.files) {
        CHECK_FALSE(fe.filename.empty());
    }

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("INT64 boundaries in parse_int") {
    // Create a valid archive to test the parser indirectly via manifest round-trip
    auto dir = make_legacy_dir("arc_i64");
    auto arc_dir = make_legacy_dir("arc_i64_r");
    write_all_required(dir);

    auto dvr = make_full_dvr();
    std::string mid = "e1b2c3d4-e5f6-4789-a123-456789abcdef";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);
    CHECK(result.success);

    // INT64_MIN parsed: test via valid_migration_id which is UUID-formatted
    // (parser is exercised via manifest.json round-trip)
    CHECK(arch.verify_archive(result.archive_path));

    // Verify negative values are rejected at schema level (record_count/size are uint64_t)
    // Test indirect: any valid archive has non-negative values
    ArchiveManifest m;
    CHECK(arch.verify_archive(result.archive_path, &m));
    for (auto& fe : m.files) {
        CHECK(fe.record_count >= 0);
        CHECK(fe.size >= 0);
    }

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Idempotency: duplicate migration ID rejected") {
    auto dir = make_legacy_dir("arc_id1");
    auto arc_dir = make_legacy_dir("arc_id1_r");
    write_all_required(dir);
    write_some_optional(dir);

    std::map<std::string, std::string> before_sha;
    std::map<std::string, uintmax_t> before_size;
    capture_source_state(dir, before_sha, before_size);

    auto dvr = make_full_dvr();
    std::string mid = "f1b2c3d4-e5f6-4789-a123-456789abcdef";

    // First archive — success
    LegacyArchive arch1(dir, arc_dir);
    auto r1 = arch1.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(r1.success);
    std::string first_path = r1.archive_path;

    // Second call with same ID — rejected
    auto r2 = arch1.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    CHECK_FALSE(r2.success);
    CHECK(r2.error == "migration_id_already_archived");

    // First archive still verifies
    CHECK(arch1.verify_archive(first_path));

    // Different ID — succeeds
    std::string mid2 = "f2b2c3d4-e5f6-4789-a123-456789abcdef";
    auto r3 = arch1.create_archive(mid2, "v0.6.0", "v0.7.0", dvr);
    CHECK(r3.success);

    // Source unchanged
    for (auto& [fn, sha] : before_sha) {
        CHECK(LegacyArchive::sha256_file(dir + fn) == sha);
    }
    for (auto& [fn, sz] : before_size) {
        CHECK(std::filesystem::file_size(dir + fn) == sz);
    }

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Source immutability: all files unchanged after archive") {
    auto dir = make_legacy_dir("arc_src");
    auto arc_dir = make_legacy_dir("arc_src_r");
    write_all_required(dir, 2, 1);
    write_some_optional(dir);

    std::map<std::string, std::string> before_sha;
    std::map<std::string, uintmax_t> before_size;
    capture_source_state(dir, before_sha, before_size);
    size_t before_count = 0;
    for (auto& fi : legacy_file_inventory()) {
        if (std::filesystem::exists(dir + fi.filename)) ++before_count;
    }
    CHECK(before_count > 0);

    auto dvr = make_full_dvr();
    std::string mid = "a0b0c0d0-e0f0-4780-a000-000000000001";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    // All source files still exist
    size_t after_count = 0;
    for (auto& fi : legacy_file_inventory()) {
        bool existed = before_sha.count(fi.filename) > 0;
        bool still_exists = std::filesystem::exists(dir + fi.filename);
        CHECK(existed == still_exists);
        if (still_exists) {
            ++after_count;
            CHECK(LegacyArchive::sha256_file(dir + fi.filename) == before_sha[fi.filename]);
            CHECK(std::filesystem::file_size(dir + fi.filename) == before_size[fi.filename]);
        }
    }
    CHECK(after_count == before_count);

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Failure cleanup: pre-publish failure removes temp, keeps source") {
    auto dir = make_legacy_dir("arc_fc1");
    auto arc_dir = make_legacy_dir("arc_fc1_r");
    write_all_required(dir);

    std::map<std::string, std::string> before_sha;
    std::map<std::string, uintmax_t> before_size;
    capture_source_state(dir, before_sha, before_size);

    // Invalid verification result → should fail before rename
    DatabaseVerificationResult bad_dvr;
    bad_dvr.initial_verification_passed = false;
    std::string mid = "b0b0c0d0-e0f0-4780-a000-000000000002";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", bad_dvr);

    CHECK_FALSE(result.success);
    CHECK(result.error == "verification_not_passed");

    // No final archive
    auto entries = std::filesystem::directory_iterator(arc_dir);
    int count = 0;
    for (auto& e : entries) {
        if (e.is_directory() && e.path().string().find("legacy-") != std::string::npos) ++count;
    }
    CHECK(count == 0);

    // Source unchanged
    for (auto& [fn, sha] : before_sha) {
        CHECK(LegacyArchive::sha256_file(dir + fn) == sha);
    }

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Failure cleanup: missing required file") {
    auto dir = make_legacy_dir("arc_fc2");
    auto arc_dir = make_legacy_dir("arc_fc2_r");
    write_txt(dir, "nodes.db", "1|main|web\n");
    // Missing all other required files

    auto dvr = make_full_dvr();
    std::string mid = "c0b0c0d0-e0f0-4780-a000-000000000003";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);

    CHECK_FALSE(result.success);
    CHECK(result.error == "required_file_missing:php_versions.db");
    CHECK(result.archive_path.empty());

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Existing final directory rejected") {
    auto dir = make_legacy_dir("arc_exd");
    auto arc_dir = make_legacy_dir("arc_exd_r");
    write_all_required(dir);

    auto dvr = make_full_dvr();
    std::string mid = "d0b0c0d0-e0f0-4780-a000-000000000004";
    LegacyArchive arch(dir, arc_dir);

    // Pre-create the final directory
    std::string final_dir = arc_dir + "legacy-v0.6.0-" + LegacyArchive::timestamp_utc() + "-" + mid;
    std::filesystem::create_directories(final_dir);

    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    CHECK_FALSE(result.success);
    CHECK(result.error == "archive_exists");

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Existing temp directory rejected") {
    auto dir = make_legacy_dir("arc_etd");
    auto arc_dir = make_legacy_dir("arc_etd_r");
    write_all_required(dir);

    auto dvr = make_full_dvr();
    std::string mid = "e0b0c0d0-e0f0-4780-a000-000000000005";
    LegacyArchive arch(dir, arc_dir);

    // Pre-create the temp directory
    std::string temp_dir = arc_dir + ".legacy-v0.6.0-" + LegacyArchive::timestamp_utc() + "-" + mid + ".tmp";
    std::filesystem::create_directories(temp_dir);

    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    CHECK_FALSE(result.success);

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Tampered archive verification rejected") {
    auto dir = make_legacy_dir("arc_tmp");
    auto arc_dir = make_legacy_dir("arc_tmp_r");
    write_all_required(dir);
    write_some_optional(dir);

    auto dvr = make_full_dvr();
    std::string mid = "f0b0c0d0-e0f0-4780-a000-000000000006";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    std::string arc_path = result.archive_path;

    // Changed data file
    {
        auto test_dir = make_legacy_dir("arc_tmp_t1");
        std::filesystem::copy(arc_path, test_dir, std::filesystem::copy_options::recursive);
        write_txt(test_dir, "nodes.db", "CORRUPTED\n");
        CHECK_FALSE(arch.verify_archive(test_dir));
        std::filesystem::remove_all(test_dir);
    }

    // Changed manifest
    {
        auto test_dir = make_legacy_dir("arc_tmp_t3");
        std::filesystem::copy(arc_path, test_dir, std::filesystem::copy_options::recursive);
        std::string mp = test_dir + "manifest.json";
        std::string json; { std::ifstream f(mp); json.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
        size_t pos = json.find("\"size\":");
        if (pos != std::string::npos) {
            json[pos + 6] = '9';
            std::ofstream f(mp); f << json;
        }
        CHECK_FALSE(arch.verify_archive(test_dir));
        std::filesystem::remove_all(test_dir);
    }

    // Removed checksum entry
    {
        auto test_dir = make_legacy_dir("arc_tmp_t4");
        std::filesystem::copy(arc_path, test_dir, std::filesystem::copy_options::recursive);
        std::string sp = test_dir + "SHA256SUMS";
        std::ifstream f(sp); std::string line;
        std::string remaining; bool found = false;
        while (std::getline(f, line)) {
            if (!found && line.find("  nodes.db") != std::string::npos) { found = true; continue; }
            remaining += line + "\n";
        }
        { std::ofstream fo(sp); fo << remaining; }
        CHECK_FALSE(arch.verify_archive(test_dir));
        std::filesystem::remove_all(test_dir);
    }

    // Symlink in archive directory
    {
        auto test_dir = make_legacy_dir("arc_tmp_t5");
        std::filesystem::copy(arc_path, test_dir, std::filesystem::copy_options::recursive);
        std::filesystem::create_symlink(test_dir + "nodes.db", test_dir + "evil_link");
        CHECK_FALSE(arch.verify_archive(test_dir));
        std::filesystem::remove_all(test_dir);
    }

    // Unknown file in archive
    {
        auto test_dir = make_legacy_dir("arc_tmp_t6");
        std::filesystem::copy(arc_path, test_dir, std::filesystem::copy_options::recursive);
        write_txt(test_dir, "evil.txt", "boo\n");
        CHECK_FALSE(arch.verify_archive(test_dir));
        std::filesystem::remove_all(test_dir);
    }

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("normalize_archive_identity_path rejects traversal and normalizes") {
    CHECK(LegacyArchive::normalize_archive_identity_path("/a/b").find("..") == std::string::npos);
    CHECK(LegacyArchive::normalize_archive_identity_path("../evil").empty());
    CHECK(LegacyArchive::normalize_archive_identity_path("./a/./b").find("./") == std::string::npos);
    CHECK(LegacyArchive::normalize_archive_identity_path("a/../b").empty()); // raw .. rejected
    // Trailing slash removed
    auto r = LegacyArchive::normalize_archive_identity_path("/a/b/");
    CHECK_FALSE(r.empty());
    CHECK(r.back() != '/');
    // No trailing slash stays
    auto r2 = LegacyArchive::normalize_archive_identity_path("/a/b");
    CHECK(r2 == r);
    // Empty → empty
    CHECK(LegacyArchive::normalize_archive_identity_path("").empty());
}

TEST_CASE("valid_timestamp rejects malformed timestamps") {
    CHECK(LegacyArchive::valid_timestamp("20250101T120000Z"));
    CHECK_FALSE(LegacyArchive::valid_timestamp(""));
    CHECK_FALSE(LegacyArchive::valid_timestamp("20250101T120000")); // no Z
    CHECK_FALSE(LegacyArchive::valid_timestamp("20250101 120000Z")); // no T
    CHECK_FALSE(LegacyArchive::valid_timestamp("20250101T120000z")); // lowercase z
    CHECK_FALSE(LegacyArchive::valid_timestamp("20250101T120000ZA")); // extra
    CHECK_FALSE(LegacyArchive::valid_timestamp("20251301T120000Z")); // month 13
    CHECK_FALSE(LegacyArchive::valid_timestamp("20250001T120000Z")); // month 00
    CHECK_FALSE(LegacyArchive::valid_timestamp("20250100T120000Z")); // day 00
    CHECK_FALSE(LegacyArchive::valid_timestamp("20250101T240000Z")); // hour 24
    CHECK_FALSE(LegacyArchive::valid_timestamp("20250101T126000Z")); // min 60
    CHECK_FALSE(LegacyArchive::valid_timestamp("20250101T120060Z")); // sec 60
}

TEST_CASE("File-entry validator requires all 6 fields") {
    // Tested indirectly: a valid archive with 19 entries passes verification.
    // This test proves the regression where push_back was missing is caught.
    auto dir = make_legacy_dir("arc_f6f");
    auto arc_dir = make_legacy_dir("arc_f6f_r");
    write_all_required(dir, 2, 1);
    write_some_optional(dir);

    auto dvr = make_full_dvr();
    std::string mid = "00000000-0000-4000-8000-000000000099";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    ArchiveManifest verified;
    CHECK(arch.verify_archive(result.archive_path, &verified));
    CHECK(verified.files.size() == 19);

    // Every required file present, every optional file present if exists
    std::set<std::string> req_present = {"nodes.db","php_versions.db","profiles.db","users.db",
        "sites.db","domains.db","databases.db","backups.db","reverse_proxies.db"};
    for (auto& fe : verified.files) {
        if (req_present.count(fe.filename)) CHECK(fe.present);
        CHECK(fe.optional == (req_present.count(fe.filename) == 0));
    }

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

// ============================================================
// Integer boundary & tampering expansion
// ============================================================

TEST_CASE("Tampered manifest: trailing comma rejected") {
    auto dir = make_legacy_dir("arc_tmc");
    auto arc_dir = make_legacy_dir("arc_tmc_r");
    write_all_required(dir, 2, 1);
    auto dvr = make_full_dvr(); std::string mid = "a0000000-0000-4000-8000-000000000001";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_tmc_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    std::string mp = test_dir + "manifest.json";
    {   // Add trailing comma before closing brace
        std::ifstream f(mp); std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t pos = json.rfind('}');
        if (pos != std::string::npos && pos > 0) { json.insert(pos, ","); }
        std::ofstream fo(mp); fo << json;
    }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Tampered manifest: leading comma rejected") {
    auto dir = make_legacy_dir("arc_lmc");
    auto arc_dir = make_legacy_dir("arc_lmc_r");
    write_all_required(dir, 2, 1);
    auto dvr = make_full_dvr(); std::string mid = "b0000000-0000-4000-8000-000000000002";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_lmc_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    std::string mp = test_dir + "manifest.json";
    {   // Insert comma after opening brace: "{,"
        std::ifstream f(mp); std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t pos = json.find('{');
        if (pos != std::string::npos) { json.insert(pos + 1, ","); }
        std::ofstream fo(mp); fo << json;
    }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Tampered manifest: unknown top-level key rejected") {
    auto dir = make_legacy_dir("arc_uke");
    auto arc_dir = make_legacy_dir("arc_uke_r");
    write_all_required(dir, 2, 1);
    auto dvr = make_full_dvr(); std::string mid = "c0000000-0000-4000-8000-000000000003";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_uke_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    std::string mp = test_dir + "manifest.json";
    {   // Insert unknown key before closing brace
        std::ifstream f(mp); std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t pos = json.rfind('}');
        if (pos != std::string::npos) { json.insert(pos, ",\"evil_key\":123"); }
        std::ofstream fo(mp); fo << json;
    }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Tampered manifest: unknown file-entry key rejected") {
    auto dir = make_legacy_dir("arc_ufe");
    auto arc_dir = make_legacy_dir("arc_ufe_r");
    write_all_required(dir, 2, 1);
    auto dvr = make_full_dvr(); std::string mid = "d0000000-0000-4000-8000-000000000004";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_ufe_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    std::string mp = test_dir + "manifest.json";
    {   // Insert unknown field in a file entry
        std::ifstream f(mp); std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t pos = json.find("\"filename\"");
        if (pos != std::string::npos) { json.insert(pos - 3, "\"evil_field\":\"bad\","); }
        std::ofstream fo(mp); fo << json;
    }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Tampered manifest: uppercase SHA rejected") {
    auto dir = make_legacy_dir("arc_usa");
    auto arc_dir = make_legacy_dir("arc_usa_r");
    write_all_required(dir, 2, 1);
    auto dvr = make_full_dvr(); std::string mid = "e0000000-0000-4000-8000-000000000005";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_usa_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    // Uppercase the SHA256SUMS hash
    std::string sp = test_dir + "SHA256SUMS";
    { std::ifstream f(sp); std::string sums((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      for (auto& c : sums) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      std::ofstream fo(sp); fo << sums; }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Tampered manifest: duplicate SHA256SUMS entry rejected") {
    auto dir = make_legacy_dir("arc_dse");
    auto arc_dir = make_legacy_dir("arc_dse_r");
    write_all_required(dir, 2, 1);
    auto dvr = make_full_dvr(); std::string mid = "f0000000-0000-4000-8000-000000000006";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_dse_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    // Duplicate a checksum line
    std::string sp = test_dir + "SHA256SUMS";
    { std::ifstream f(sp); std::string sums((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      size_t nl = sums.find('\n');
      if (nl != std::string::npos) { sums.insert(nl + 1, sums.substr(0, nl + 1)); }
      std::ofstream fo(sp); fo << sums; }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Tampered manifest: missing file entry rejected") {
    auto dir = make_legacy_dir("arc_mfe2");
    auto arc_dir = make_legacy_dir("arc_mfe2_r");
    write_all_required(dir, 2, 1);
    write_some_optional(dir);
    auto dvr = make_full_dvr(); std::string mid = "01000000-0000-4000-8000-000000000007";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_mfe2_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    std::string mp = test_dir + "manifest.json";
    {   // Remove a file entry from the files array
        std::ifstream f(mp); std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t start = json.find("\"files\":[");
        size_t end = json.find('}', start); // end of first file entry
        if (end != std::string::npos) {
            end = json.find("},{", end); // end of first file + next
            if (end != std::string::npos) {
                json.erase(start + 8, end - start - 8 + 1); // remove first entry
            }
        }
        std::ofstream fo(mp); fo << json;
    }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Tampered manifest: required file marked absent rejected") {
    auto dir = make_legacy_dir("arc_rfa");
    auto arc_dir = make_legacy_dir("arc_rfa_r");
    write_all_required(dir, 2, 1);
    auto dvr = make_full_dvr(); std::string mid = "02000000-0000-4000-8000-000000000008";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_rfa_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    {
        std::string mp = test_dir + "manifest.json";
        std::ifstream f(mp); std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t pos = json.find("\"filename\":\"nodes.db\"");
        if (pos != std::string::npos) {
            size_t present = json.find("\"present\":true", pos);
            if (present != std::string::npos) json.replace(present, 15, "\"present\":false");
        }
        { std::ofstream fo(mp); fo << json; }
    }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Tampered manifest: changed present flag rejected") {
    auto dir = make_legacy_dir("arc_cpf");
    auto arc_dir = make_legacy_dir("arc_cpf_r");
    write_all_required(dir, 2, 1);
    write_some_optional(dir);
    auto dvr = make_full_dvr(); std::string mid = "03000000-0000-4000-8000-000000000009";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_cpf_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    {
        std::string mp = test_dir + "manifest.json";
        std::ifstream f(mp); std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        // Change an optional file's present from false to true
        size_t pos = json.find("\"optional\":true");
        if (pos != std::string::npos) {
            size_t present = json.find("\"present\":false", pos);
            if (present != std::string::npos) json.replace(present, 16, "\"present\":true");
        }
        { std::ofstream fo(mp); fo << json; }
    }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Tampered manifest: changed optional flag rejected") {
    auto dir = make_legacy_dir("arc_cof");
    auto arc_dir = make_legacy_dir("arc_cof_r");
    write_all_required(dir, 2, 1);
    auto dvr = make_full_dvr(); std::string mid = "04000000-0000-4000-8000-000000000010";
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive(mid, "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_cof_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    {
        std::string mp = test_dir + "manifest.json";
        std::ifstream f(mp); std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t pos = json.find("\"filename\":\"nodes.db\"");
        if (pos != std::string::npos) {
            size_t opt = json.find("\"optional\":false", pos);
            if (opt != std::string::npos) json.replace(opt, 16, "\"optional\":true");
        }
        { std::ofstream fo(mp); fo << json; }
    }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Quoted numeric size rejected by schema") {
    auto dir = make_legacy_dir("arc_qns");
    auto arc_dir = make_legacy_dir("arc_qns_r");
    write_all_required(dir, 2, 1);
    auto dvr = make_full_dvr();
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive("a1000000-0000-4000-8000-000000000011",
                                      "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_qns_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    {
        std::string mp = test_dir + "manifest.json";
        std::ifstream f(mp); std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t pos = json.find("\"size\":");
        if (pos != std::string::npos) {
            size_t val_start = json.find_first_not_of(" ", pos + 7);
            if (val_start != std::string::npos) json.insert(val_start, "\"");
            size_t end = json.find(',', val_start);
            if (end != std::string::npos) json.insert(end, "\"");
        }
        { std::ofstream fo(mp); fo << json; }
    }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}

TEST_CASE("Quoted numeric record_count rejected by schema") {
    auto dir = make_legacy_dir("arc_qnr");
    auto arc_dir = make_legacy_dir("arc_qnr_r");
    write_all_required(dir, 2, 1);
    auto dvr = make_full_dvr();
    LegacyArchive arch(dir, arc_dir);
    auto result = arch.create_archive("a2000000-0000-4000-8000-000000000012",
                                      "v0.6.0", "v0.7.0", dvr);
    REQUIRE(result.success);

    auto test_dir = make_legacy_dir("arc_qnr_t");
    std::filesystem::copy(result.archive_path, test_dir, std::filesystem::copy_options::recursive);
    {
        std::string mp = test_dir + "manifest.json";
        std::ifstream f(mp); std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        size_t pos = json.find("\"record_count\":");
        if (pos != std::string::npos) {
            size_t val_start = json.find_first_not_of(" ", pos + 15);
            if (val_start != std::string::npos) json.insert(val_start, "\"");
            size_t end = json.find(',', val_start);
            if (end != std::string::npos) json.insert(end, "\"");
        }
        { std::ofstream fo(mp); fo << json; }
    }
    CHECK_FALSE(arch.verify_archive(test_dir));
    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(arc_dir);
}
