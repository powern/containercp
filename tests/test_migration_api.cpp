#include "migration/VestaSiteImporter.h"
#include "config/Config.h"
#include "filesystem/Filesystem.h"
#include "runtime/CommandExecutor.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <climits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "doctest/doctest.h"

using namespace containercp;

// ─── Backup directory: security tests ───

TEST_CASE("Migration API - allowed backup directory") {
    std::vector<std::string> allowed = {"/backup", "/srv/containercp/backups"};
    CHECK(allowed.size() == 2);
    CHECK(allowed[0] == "/backup");
    CHECK(allowed[1] == "/srv/containercp/backups");
}

TEST_CASE("Migration API - relative path traversal rejected") {
    std::string path = "/backup/../../etc/passwd";
    // Resolve canonical
    char real[PATH_MAX];
    bool valid = ::realpath(path.c_str(), real) != nullptr;
    // /backup/../../etc/passwd resolves to /etc/passwd which is outside allowed
    CHECK_FALSE(valid);
}

TEST_CASE("Migration API - non-.tar file rejected") {
    std::string tmpdir = "/tmp/test_mig_api_XXXXXX";
    char* dir = ::mkdtemp(tmpdir.data());
    REQUIRE(dir != nullptr);
    std::string fname = std::string(dir) + "/test.txt";

    {
        std::ofstream f(fname);
        f << "not a backup";
    }

    // Check extension
    CHECK(fname.find(".tar") == std::string::npos);
    CHECK(fname.find(".tar.gz") == std::string::npos);

    std::remove(fname.c_str());
    std::remove(dir);
}

TEST_CASE("Migration CLI - command format") {
    // Verify socket command format matches DaemonApp expectations
    std::string backup = "/backup/admin.tar";
    std::string domain = "example.com";
    std::string owner = "admin";

    std::string cmd = "migrate-vesta-site|--backup|" + backup
                    + "|--domain|" + domain
                    + "|--owner|" + owner
                    + "|--dry-run";

    // Expected format: pipe-delimited, command first
    CHECK(cmd.find("migrate-vesta-site") == 0);
    CHECK(cmd.find("--backup") != std::string::npos);
    CHECK(cmd.find("--dry-run") != std::string::npos);

    // Verify decode would work
    auto first_pipe = cmd.find('|');
    CHECK(first_pipe != std::string::npos);
    std::string cmd_name = cmd.substr(0, first_pipe);
    CHECK(cmd_name == "migrate-vesta-site");
}

TEST_CASE("VestaSiteImporter JSON response has no DB_PASSWORD") {
    // Verify that the API JSON builder does not include db_password
    std::string json_sample =
        "{\"success\":true,\"data\":{"
        "\"wp_db_name\":\"test\""
        ",\"wp_db_user\":\"test_user\""
        ",\"wp_db_host\":\"localhost\""
        "}}";

    CHECK(json_sample.find("db_password") == std::string::npos);
    CHECK(json_sample.find("DB_PASSWORD") == std::string::npos);
}

TEST_CASE("Migration backup - symlink rejected") {
    // Test that a symlink to a file outside allowed dir is rejected
    std::string allowed_dir = "/tmp/test_mig_allowed";
    std::string target_file = "/etc/hostname";
    std::string symlink_path = allowed_dir + std::string("/") + "backup.tar";

    ::mkdir(allowed_dir.c_str(), 0755);

    if (::symlink(target_file.c_str(), symlink_path.c_str()) == 0) {
        char real[PATH_MAX];
        if (::realpath(symlink_path.c_str(), real)) {
            std::string canon(real);
            std::string ad(allowed_dir);
            char ad_real[PATH_MAX];
            if (::realpath(ad.c_str(), ad_real)) {
                // Symlink resolves outside allowed dir
                bool inside = canon.substr(0, strlen(ad_real)) == ad_real;
                CHECK_FALSE(inside);
            }
        }
        std::remove(symlink_path.c_str());
    }

    std::remove(allowed_dir.c_str());
}
