#include "migration/VestaSiteImporter.h"
#include "config/Config.h"
#include "filesystem/Filesystem.h"
#include "runtime/CommandExecutor.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <climits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "doctest/doctest.h"

using namespace containercp;

// ─── Shared helper logic tests ───

TEST_CASE("Migration security - valid extensions") {
    auto is_valid_ext = [](const std::string& name) -> bool {
        if (name.size() < 4) return false;
        auto ext4 = name.substr(name.size() - 4);
        if (ext4 == ".tar") return true;
        if (name.size() > 7 && name.substr(name.size() - 7) == ".tar.gz") return true;
        return false;
    };

    CHECK(is_valid_ext("backup.tar"));
    CHECK(is_valid_ext("admin.2026-07-01.tar"));
    CHECK(is_valid_ext("backup.tar.gz"));
    CHECK(is_valid_ext("site.2026-01-01.tar.gz"));
    CHECK_FALSE(is_valid_ext("backup.tar.bak"));
    CHECK_FALSE(is_valid_ext("backup.tgz"));
    CHECK_FALSE(is_valid_ext("backup.zip"));
    CHECK_FALSE(is_valid_ext("backup"));
}

TEST_CASE("Migration security - basename rejects slashes") {
    CHECK_FALSE(std::string("/backup/file.tar").find('/') == std::string::npos);
    CHECK(std::string("file.tar").find('/') == std::string::npos);
    CHECK_FALSE(std::string("../../etc/passwd").find('/') == std::string::npos);
}

TEST_CASE("Migration security - prefix check") {
    auto is_inside = [](const std::string& canon, const std::string& allowed) -> bool {
        if (canon == allowed) return true;
        if (canon.size() > allowed.size() + 1) {
            if (canon.substr(0, allowed.size() + 1) == allowed + "/") return true;
        }
        return false;
    };

    CHECK(is_inside("/backup/file.tar", "/backup"));
    CHECK(is_inside("/backup/sub/file.tar", "/backup"));
    CHECK_FALSE(is_inside("/backup-other/file.tar", "/backup"));
    CHECK_FALSE(is_inside("/backup_other/file.tar", "/backup"));
    CHECK_FALSE(is_inside("/etc/passwd", "/backup"));
}

TEST_CASE("Migration security - symlink detection") {
    std::string tmpdir = "/tmp/test_mig_sym_XXXXXX";
    char* dir = ::mkdtemp(tmpdir.data());
    REQUIRE(dir != nullptr);

    std::string target = std::string(dir) + "/real_file.txt";
    std::string link = std::string(dir) + "/backup.tar";

    // Create a real file
    { std::ofstream f(target); f << "content"; }

    // Create a symlink
    if (::symlink(target.c_str(), link.c_str()) == 0) {
        struct stat lst;
        ::lstat(link.c_str(), &lst);
        CHECK(S_ISLNK(lst.st_mode));

        // stat follows symlink, lstat does not
        struct stat st;
        ::stat(link.c_str(), &st);
        CHECK(S_ISREG(st.st_mode)); // stat follows symlink → sees regular file

        std::remove(link.c_str());
    }

    std::remove(target.c_str());
    std::remove(dir);
}

TEST_CASE("Migration security - regular file not symlink") {
    std::string tmpdir = "/tmp/test_mig_reg_XXXXXX";
    char* dir = ::mkdtemp(tmpdir.data());
    REQUIRE(dir != nullptr);

    std::string fpath = std::string(dir) + "/backup.tar";
    { std::ofstream f(fpath); f << "data"; }

    struct stat lst;
    ::lstat(fpath.c_str(), &lst);
    CHECK_FALSE(S_ISLNK(lst.st_mode));
    CHECK(S_ISREG(lst.st_mode));

    std::remove(fpath.c_str());
    std::remove(dir);
}

TEST_CASE("Migration CLI - command format") {
    std::string backup = "admin.tar";
    std::string domain = "example.com";
    std::string owner = "admin";

    std::string cmd = "migrate-vesta-site|--backup|" + backup
                    + "|--domain|" + domain
                    + "|--owner|" + owner
                    + "|--dry-run";

    CHECK(cmd.find("migrate-vesta-site") == 0);
    CHECK(cmd.find("--backup") != std::string::npos);
    CHECK(cmd.find("--dry-run") != std::string::npos);

    auto first_pipe = cmd.find('|');
    CHECK(first_pipe != std::string::npos);
    std::string cmd_name = cmd.substr(0, first_pipe);
    CHECK(cmd_name == "migrate-vesta-site");
}

TEST_CASE("VestaSiteImporter JSON response has no DB_PASSWORD") {
    std::string json_sample =
        "{\"success\":true,\"data\":{"
        "\"wp_db_name\":\"test\""
        ",\"wp_db_user\":\"test_user\""
        ",\"wp_db_host\":\"localhost\""
        "}}";

    CHECK(json_sample.find("db_password") == std::string::npos);
    CHECK(json_sample.find("DB_PASSWORD") == std::string::npos);
}

TEST_CASE("VestaSiteImporter rejects invalid migration domain before archive access") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    migration::Options opts;
    opts.backup_path = "/tmp/does-not-matter.tar";
    opts.domain = "../../etc/passwd";
    opts.owner = "admin";

    const auto manifest = importer.inspect(opts);
    REQUIRE_FALSE(manifest.errors.empty());
    CHECK(manifest.errors.front() == "Invalid migration domain");
}

TEST_CASE("VestaSiteImporter rejects unsafe backup paths") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance());

    const std::string directory = "/tmp/containercp-migration-path-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const std::string target = directory + "/backup.tar";
    const std::string link = directory + "/backup-link.tar";
    { std::ofstream(target) << "not an archive"; }

    migration::Options opts;
    opts.backup_path = link;
    opts.domain = "example.com";
    opts.owner = "admin";
    REQUIRE(::symlink(target.c_str(), link.c_str()) == 0);

    const auto manifest = importer.inspect(opts);
    REQUIRE_FALSE(manifest.errors.empty());
    CHECK(manifest.errors.front() == "Backup path is not a regular file");

    std::filesystem::remove_all(directory);
}

TEST_CASE("VestaSiteImporter upgrade rejects traversal and unmanaged sites") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    site::SiteManager sites;
    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance(), &sites, nullptr);

    migration::Options traversal;
    traversal.domain = "../outside.example";
    auto traversal_result = importer.upgrade_site(traversal);
    REQUIRE_FALSE(traversal_result.success);
    CHECK(traversal_result.errors.front() == "Invalid migration domain");

    migration::Options missing;
    missing.domain = "managed.example";
    auto missing_result = importer.upgrade_site(missing);
    REQUIRE_FALSE(missing_result.success);
    CHECK(missing_result.errors.front() == "Managed migration site was not found");
}

static std::string write_fake_docker(const std::string& directory,
                                     uint64_t site_id,
                                     const std::string& site_dir,
                                     const std::string& mount_destination,
                                     const std::string& log_path) {
    const std::string script_path = directory + "/docker";
    std::ofstream script(script_path);
    script << "#!/bin/sh\n"
           << "printf '%s\\n' \"$*\" >> '" << log_path << "'\n"
           << "if [ \"$1\" = compose ] && [ \"$4\" = ps ]; then\n"
           << "  if [ \"$8\" = web ]; then printf 'site-" << site_id << "-web\\n';\n"
           << "  else printf 'site-" << site_id << "-php\\n'; fi\n"
           << "  exit 0\n"
           << "fi\n"
           << "if [ \"$1\" = inspect ]; then\n"
           << "  case \"$4\" in\n"
           << "    '{{range .Mounts}}'*) printf '" << site_dir << "public|" << mount_destination << "\\n'; exit 0 ;;\n"
           << "  esac\n"
           << "  case \"$2\" in\n"
           << "    site-" << site_id << "-web) printf '" << site_id << "|web|" << site_dir << "|running\\n' ;;\n"
           << "    site-" << site_id << "-php) printf '" << site_id << "|php|" << site_dir << "|running\\n' ;;\n"
           << "    *) exit 1 ;;\n"
           << "  esac\n"
           << "  exit 0\n"
           << "fi\n"
           << "exit 0\n";
    script.close();
    ::chmod(script_path.c_str(), 0755);
    return script_path;
}

TEST_CASE("VestaSiteImporter resolves the managed Apache runtime and PHP mount") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    site::SiteManager sites;
    const std::string domain = "migration-runtime-apache.local";
    const std::string site_dir = cfg.sites_dir() + domain + "/";
    std::filesystem::remove_all(site_dir);
    std::filesystem::create_directories(site_dir + "public");
    std::filesystem::create_directories(site_dir + "config/apache");
    std::ofstream(site_dir + "docker-compose.yml") << "services: {}\n";
    std::ofstream(site_dir + "public/wp-config.php") << "<?php\n";

    const uint64_t site_id = sites.create(domain, "admin", 1, "apache");
    const std::string fake_dir = "/tmp/containercp-fake-docker-apache";
    std::filesystem::remove_all(fake_dir);
    std::filesystem::create_directories(fake_dir);
    const std::string log_path = fake_dir + "/commands.log";
    write_fake_docker(fake_dir, site_id, site_dir, "/usr/local/apache2/htdocs", log_path);

    const char* old_path = std::getenv("PATH");
    const std::string saved_path = old_path == nullptr ? std::string() : old_path;
    setenv("PATH", (fake_dir + ":" + saved_path).c_str(), 1);

    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance(), &sites, nullptr);
    migration::Options opts;
    opts.domain = domain;
    const auto result = importer.upgrade_site(opts);

    setenv("PATH", saved_path.c_str(), 1);
    REQUIRE(result.success);
    const std::string log = fs.read_file(log_path);
    CHECK(log.find("site-" + std::to_string(site_id) + "-web") != std::string::npos);
    CHECK(log.find("site-N-web") == std::string::npos);
    CHECK(log.find("site-" + std::to_string(site_id) + "-php php -l /usr/local/apache2/htdocs/wp-config.php") != std::string::npos);

    std::filesystem::remove_all(site_dir);
    std::filesystem::remove_all(fake_dir);
}

TEST_CASE("VestaSiteImporter resolves the managed Nginx runtime and PHP mount") {
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    site::SiteManager sites;
    const std::string domain = "migration-runtime-nginx.local";
    const std::string site_dir = cfg.sites_dir() + domain + "/";
    std::filesystem::remove_all(site_dir);
    std::filesystem::create_directories(site_dir + "public");
    std::ofstream(site_dir + "docker-compose.yml") << "services: {}\n";
    std::ofstream(site_dir + "public/wp-config.php") << "<?php\n";

    const uint64_t site_id = sites.create(domain, "admin", 1, "nginx");
    const std::string fake_dir = "/tmp/containercp-fake-docker-nginx";
    std::filesystem::remove_all(fake_dir);
    std::filesystem::create_directories(fake_dir);
    const std::string log_path = fake_dir + "/commands.log";
    write_fake_docker(fake_dir, site_id, site_dir, "/var/www/html", log_path);

    const char* old_path = std::getenv("PATH");
    const std::string saved_path = old_path == nullptr ? std::string() : old_path;
    setenv("PATH", (fake_dir + ":" + saved_path).c_str(), 1);

    migration::VestaSiteImporter importer(exec, fs, cfg, logger::Logger::instance(), &sites, nullptr);
    migration::Options opts;
    opts.domain = domain;
    const auto result = importer.upgrade_site(opts);

    setenv("PATH", saved_path.c_str(), 1);
    REQUIRE(result.success);
    const std::string log = fs.read_file(log_path);
    CHECK(log.find("nginx -t") != std::string::npos);
    CHECK(log.find("site-N-web") == std::string::npos);
    CHECK(log.find("site-" + std::to_string(site_id) + "-php php -l /var/www/html/wp-config.php") != std::string::npos);

    std::filesystem::remove_all(site_dir);
    std::filesystem::remove_all(fake_dir);
}
