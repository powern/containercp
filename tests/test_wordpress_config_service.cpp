#include "wordpress/WordPressConfigService.h"

#include "doctest/doctest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace containercp::wordpress;
using namespace containercp;

namespace {

namespace fs = std::filesystem;

fs::path service_root(const std::string& name) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("containercp_wp_service_" + name + "_" + std::to_string(unique));
}

void write_service_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

std::string read_service_file(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

TEST_CASE("WordPressConfigService inspects valid migrated site by id and domain") {
    site::SiteManager sites;
    const uint64_t site_id = sites.create("example.com", "admin", 1);
    const auto root = service_root("valid");
    write_service_file(root / "example.com" / "public" / "wp-config.php", R"PHP(<?php
define('DB_NAME', 'wp_example');
define('DB_USER', 'wp_user');
define('DB_PASSWORD', 'secret');
define('DB_HOST', 'mariadb');
)PHP");

    WordPressConfigService service(sites, root);
    const auto by_id = service.inspect_site(site_id);
    CHECK(by_id.ok);
    CHECK(by_id.status == WordPressCredentialStatus::Complete);
    CHECK(by_id.site_id == site_id);
    CHECK(by_id.domain == "example.com");
    CHECK(by_id.document_root.filename() == "public");
    CHECK(by_id.config_path.filename() == "wp-config.php");
    CHECK(by_id.inspection.credentials.db_name.value == "wp_example");
    CHECK(by_id.inspection.credentials.db_password.value.empty());

    const auto by_domain = service.inspect_domain("example.com");
    CHECK(by_domain.ok);
    CHECK(by_domain.site_id == site_id);

    fs::remove_all(root);
}

TEST_CASE("WordPressConfigService reports missing site and missing roots") {
    site::SiteManager sites;
    const uint64_t site_id = sites.create("missing-root.test", "admin", 1);
    const auto root = service_root("missing_root");
    WordPressConfigService service(sites, root);

    const auto missing_site = service.inspect_site(999);
    CHECK_FALSE(missing_site.ok);
    CHECK(missing_site.status == WordPressCredentialStatus::Error);
    CHECK(missing_site.code == "site_not_found");

    const auto missing_domain = service.inspect_domain("absent.test");
    CHECK_FALSE(missing_domain.ok);
    CHECK(missing_domain.code == "site_not_found");

    const auto missing_root = service.inspect_site(site_id);
    CHECK_FALSE(missing_root.ok);
    CHECK(missing_root.status == WordPressCredentialStatus::ConfigMissing);
    CHECK(missing_root.code == "site_root_missing");
}

TEST_CASE("WordPressConfigService reports non-WordPress site with no active config") {
    site::SiteManager sites;
    const uint64_t site_id = sites.create("static.test", "admin", 1);
    const auto root = service_root("non_wordpress");
    write_service_file(root / "static.test" / "public" / "index.php", "<?php echo 'hello';");

    WordPressConfigService service(sites, root);
    const auto result = service.inspect_site(site_id);
    CHECK_FALSE(result.ok);
    CHECK(result.status == WordPressCredentialStatus::ConfigMissing);
    CHECK(result.code == "config_missing");
    CHECK(result.site_root.filename() == "static.test");

    fs::remove_all(root);
}

TEST_CASE("WordPressConfigService rejects site_id zero") {
    site::SiteManager sites;
    WordPressConfigService service(sites, service_root("site_zero"));

    const auto result = service.inspect_site(0);
    CHECK_FALSE(result.ok);
    CHECK(result.status == WordPressCredentialStatus::Unsupported);
    CHECK(result.code == "system_site_unsupported");
}

TEST_CASE("WordPressConfigService rejects resolved site root escapes") {
    site::SiteManager sites;
    std::vector<site::Site> records;
    site::Site bad;
    bad.id = 7;
    bad.name = "../outside";
    bad.domain = "../outside";
    bad.owner = "admin";
    bad.node_id = 1;
    records.push_back(bad);
    sites.set_sites(records);

    const auto root = service_root("escape") / "sites";
    fs::create_directories(root.parent_path() / "outside");
    WordPressConfigService service(sites, root);

    const auto result = service.inspect_site(7);
    CHECK_FALSE(result.ok);
    CHECK(result.status == WordPressCredentialStatus::UnsafePath);
    CHECK(result.code == "site_root_escape");

    fs::remove_all(root.parent_path());
}

TEST_CASE("WordPressConfigService public view redacts password and paths") {
    site::SiteManager sites;
    const uint64_t site_id = sites.create("public-view.test", "admin", 1);
    const auto root = service_root("public_view");
    write_service_file(root / "public-view.test" / "public" / "wp-config.php", R"PHP(<?php
define('DB_NAME', 'wp_public');
define('DB_USER', 'wp_user');
define('DB_PASSWORD', 'do-not-expose');
define('DB_HOST', 'mariadb');
)PHP");

    WordPressConfigService service(sites, root);
    const auto result = service.inspect_site(site_id);
    const auto view = service.public_view(result);

    CHECK(view.available);
    CHECK(view.site_id == site_id);
    CHECK(view.domain == "public-view.test");
    CHECK(view.status == "complete");
    CHECK(view.source == "direct_constant");
    CHECK(view.mutability == "mutable_direct_constant");
    CHECK(view.db_name == "wp_public");
    CHECK(view.db_user == "wp_user");
    CHECK(view.db_host == "mariadb");
    CHECK(view.db_password_present);
    CHECK(view.db_name.find("do-not-expose") == std::string::npos);
    CHECK(view.db_user.find("do-not-expose") == std::string::npos);
    CHECK(view.db_host.find("do-not-expose") == std::string::npos);

    fs::remove_all(root);
}

TEST_CASE("WordPressConfigService inspection leaves config bytes unchanged") {
    site::SiteManager sites;
    const uint64_t site_id = sites.create("unchanged.test", "admin", 1);
    const auto root = service_root("unchanged");
    const auto config = root / "unchanged.test" / "public" / "wp-config.php";
    const std::string content = R"PHP(<?php
define('DB_NAME', 'wp_same');
define('DB_USER', 'wp_user');
define('DB_PASSWORD', 'secret');
)PHP";
    write_service_file(config, content);

    WordPressConfigService service(sites, root);
    const auto before = read_service_file(config);
    const auto result = service.inspect_site(site_id);
    const auto after = read_service_file(config);

    CHECK(result.ok);
    CHECK(before == content);
    CHECK(after == before);

    fs::remove_all(root);
}

TEST_CASE("WordPressConfigService public view includes unsafe permission warning") {
    site::SiteManager sites;
    const uint64_t site_id = sites.create("permissions.test", "admin", 1);
    const auto root = service_root("permissions");
    const auto config = root / "permissions.test" / "public" / "wp-config.php";
    write_service_file(config, R"PHP(<?php
define('DB_NAME', 'wp_permissions');
define('DB_USER', 'wp_user');
define('DB_PASSWORD', 'secret');
)PHP");
    fs::permissions(config, fs::perms::owner_read | fs::perms::owner_write |
                                fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace);

    WordPressConfigService service(sites, root);
    const auto view = service.public_view(service.inspect_site(site_id));

    bool found_warning = false;
    for (const auto& issue : view.issues) {
        if (issue.code == "unsafe_permissions" && issue.severity == WordPressConfigIssueSeverity::Warning) {
            found_warning = true;
        }
        CHECK(issue.message.find(config.string()) == std::string::npos);
    }
    CHECK(found_warning);

    fs::remove_all(root);
}

TEST_CASE("WordPressConfigService public view preserves ambiguous status safely") {
    site::SiteManager sites;
    const uint64_t site_id = sites.create("ambiguous.test", "admin", 1);
    const auto root = service_root("ambiguous");
    write_service_file(root / "ambiguous.test" / "public" / "wp-config.php", R"PHP(<?php
define('DB_NAME', 'wp_one');
define('DB_NAME', 'wp_two');
define('DB_USER', 'wp_user');
define('DB_PASSWORD', 'secret');
)PHP");

    WordPressConfigService service(sites, root);
    const auto view = service.public_view(service.inspect_site(site_id));

    CHECK_FALSE(view.available);
    CHECK(view.status == "ambiguous");
    CHECK(view.mutability == "ambiguous");
    CHECK(view.db_password_present);
    REQUIRE_FALSE(view.issues.empty());
    CHECK(view.issues[0].code == "duplicate_constant");

    fs::remove_all(root);
}
