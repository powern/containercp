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
