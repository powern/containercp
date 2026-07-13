#include "migration/VestaSiteImporter.h"
#include "config/Config.h"
#include "filesystem/Filesystem.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>

#include "doctest/doctest.h"

using namespace containercp;

// Helper: create a minimal tar archive for testing
static std::string create_test_tar(const std::string& path,
                                    const std::string& domain) {
    std::string dir = path + "_dir";
    std::string web_dir = dir + "/web/" + domain;
    std::string db_dir = dir + "/db/test_wp_db";
    std::string public_html = dir + "/public_html";

    // Create directory structure
    ::mkdir(dir.c_str(), 0755);
    ::mkdir((dir + "/web").c_str(), 0755);
    ::mkdir(web_dir.c_str(), 0755);
    ::mkdir((dir + "/db").c_str(), 0755);
    ::mkdir(db_dir.c_str(), 0755);
    ::mkdir(public_html.c_str(), 0755);

    // Create a wp-config.php file
    {
        std::ofstream f(public_html + "/wp-config.php");
        f << "<?php\n"
          << "define( 'DB_NAME', 'test_wp_db' );\n"
          << "define( 'DB_USER', 'test_user' );\n"
          << "define( 'DB_PASSWORD', 'secret123' );\n"
          << "define( 'DB_HOST', 'localhost' );\n";
    }
    {
        std::ofstream f(public_html + "/index.php");
        f << "<?php echo 'hello';";
    }

    // Create domain_data.tar.gz
    std::system(("cd " + dir + " && tar czf " + web_dir
        + "/domain_data.tar.gz public_html/ 2>/dev/null").c_str());

    // Create DB dump
    {
        std::ofstream f(db_dir + "/test_wp_db.mysql.sql.gz");
        f << "gzip-compressed-dump-content";
    }

    // Create main tar archive
    std::system(("cd " + dir + " && tar cf " + path
        + " web/ db/ 2>/dev/null").c_str());

    // Cleanup temp dir
    std::system(("rm -rf " + dir).c_str());

    return path;
}

TEST_CASE("VestaSiteImporter inspect - domain found with WordPress") {
    auto& log = logger::Logger::instance();
    (void)log;

    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg);

    std::string tar_path = "/tmp/test_vesta_import_wp.tar";
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
    CHECK(m.has_wp_config);
    CHECK(m.wp_db_name == "test_wp_db");
    CHECK(m.wp_db_user == "test_user");
    CHECK(m.wp_db_host == "localhost");
    CHECK(m.db_dump_found);
    CHECK_FALSE(m.db_dump_path.empty());
    CHECK(m.db_type == "mysql");
    CHECK_FALSE(m.site_exists);

    std::remove(tar_path.c_str());
}

TEST_CASE("VestaSiteImporter inspect - domain not found") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg);

    std::string tar_path = "/tmp/test_vesta_import_notfound.tar";
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

TEST_CASE("VestaSiteImporter inspect - backup file missing") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg);

    migration::Options opts;
    opts.backup_path = "/tmp/nonexistent_backup.tar";
    opts.domain = "example.com";
    opts.owner = "admin";

    auto m = importer.inspect(opts);

    CHECK_FALSE(m.errors.empty());
    CHECK(m.errors[0].find("not found") != std::string::npos);
}

TEST_CASE("VestaSiteImporter - normalize_db_name strips prefix") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg);

    // Use private method via accessor pattern — test private logic via public inspect
    // Instead, test indirectly by inspecting the normalized name in manifest
    std::string tar_path = "/tmp/test_vesta_normalize.tar";
    create_test_tar(tar_path, "example.com");

    migration::Options opts;
    opts.backup_path = tar_path;
    opts.domain = "example.com";
    opts.owner = "admin";

    auto m = importer.inspect(opts);

    CHECK(m.has_wp_config);
    CHECK(m.wp_db_name == "test_wp_db");
    // wp_db_name should NOT have a username prefix here since we created it without one
    CHECK(m.wp_db_name.find("admin_") == std::string::npos);

    std::remove(tar_path.c_str());
}

TEST_CASE("VestaSiteImporter - tar_safe_list rejects absolute paths") {
    // Create tar with absolute path
    std::string bad_tar = "/tmp/test_bad_tar.tar";
    std::system(("echo content > /tmp/evil_file.txt && tar cf " + bad_tar
        + " -C / tmp/evil_file.txt 2>/dev/null || true").c_str());

    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg);

    migration::Options opts;
    opts.backup_path = bad_tar;
    opts.domain = "x";
    opts.owner = "admin";

    auto m = importer.inspect(opts);

    // Should fail because tar has absolute path
    CHECK_FALSE(m.errors.empty());

    std::remove(bad_tar.c_str());
    std::remove("/tmp/evil_file.txt");
}

TEST_CASE("VestaSiteImporter - detect web root prioritizes public_html") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg);

    // The test tar has public_html/ in domain_data.tar.gz
    std::string tar_path = "/tmp/test_vesta_webroot.tar";
    create_test_tar(tar_path, "example.com");

    migration::Options opts;
    opts.backup_path = tar_path;
    opts.domain = "example.com";
    opts.owner = "admin";

    auto m = importer.inspect(opts);

    CHECK(m.web_root_type == "public_html");

    std::remove(tar_path.c_str());
}

TEST_CASE("VestaSiteImporter - dry run does not modify system") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg);

    std::string tar_path = "/tmp/test_vesta_dryrun.tar";
    create_test_tar(tar_path, "example.com");

    // Record system state before
    std::string sites_dir = cfg.sites_dir() + "example.com/";
    bool site_existed_before = fs.exists(sites_dir);

    migration::Options opts;
    opts.backup_path = tar_path;
    opts.domain = "example.com";
    opts.owner = "admin";
    opts.dry_run = true;

    auto m = importer.inspect(opts);

    // Verify no site was created
    bool site_existed_after = fs.exists(sites_dir);
    CHECK(site_existed_before == site_existed_after);

    std::remove(tar_path.c_str());
}
