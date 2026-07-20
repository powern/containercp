#include "wordpress/WordPressConfigService.h"
#include "wordpress/WordPressDatabaseCredentialResolver.h"
#include "database/DatabaseManager.h"

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

std::string service_wp_config(const std::string& db_name,
                              const std::string& db_user,
                              const std::string& db_host = "mariadb") {
    return "<?php\n"
           "define('DB_NAME', '" + db_name + "');\n"
           "define('DB_USER', '" + db_user + "');\n"
           "define('DB_PASSWORD', 'secret');\n"
           "define('DB_HOST', '" + db_host + "');\n";
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
    CHECK(by_id.container_document_root == "/usr/local/apache2/htdocs");
    CHECK(by_id.inspection.credentials.db_name.value == "wp_example");
    CHECK(by_id.inspection.credentials.db_password.value.empty());

    const auto verification = service.runtime_verification_request(by_id);
    CHECK(verification.compose_dir == by_id.site_root);
    CHECK(verification.document_root == by_id.document_root);
    CHECK(verification.config_path == by_id.config_path);
    CHECK(verification.container_document_root == "/usr/local/apache2/htdocs");

    const auto by_domain = service.inspect_domain("example.com");
    CHECK(by_domain.ok);
    CHECK(by_domain.site_id == site_id);

    fs::remove_all(root);
}

TEST_CASE("WordPressConfigService maps nginx runtime verification document root") {
    site::SiteManager sites;
    const uint64_t site_id = sites.create("nginx.test", "admin", 1, "nginx");
    const auto root = service_root("nginx_runtime");
    write_service_file(root / "nginx.test" / "public" / "wp-config.php", R"PHP(<?php
define('DB_NAME', 'wp_nginx');
define('DB_USER', 'wp_user');
define('DB_PASSWORD', 'secret');
define('DB_HOST', 'mariadb');
)PHP");

    WordPressConfigService service(sites, root);
    const auto result = service.inspect_site(site_id);
    const auto verification = service.runtime_verification_request(result);

    CHECK(result.ok);
    CHECK(result.container_document_root == "/var/www/html");
    CHECK(verification.container_document_root == "/var/www/html");

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

TEST_CASE("WordPressConfigService resolves site_id zero as unsupported system site when WordPress is absent") {
    site::SiteManager sites;
    site::Site system_site;
    system_site.id = 0;
    system_site.name = "ContainerCP Admin";
    system_site.domain = "admin.test";
    system_site.owner = "system";
    system_site.node_id = 0;
    system_site.web_server = "nginx";
    sites.set_sites({system_site});

    const auto root = service_root("site_zero");
    write_service_file(root / "admin.test" / "public" / "index.php", "<?php echo 'admin';");
    WordPressConfigService service(sites, root);

    const auto result = service.inspect_site(0);
    CHECK_FALSE(result.ok);
    CHECK(result.status == WordPressCredentialStatus::Unsupported);
    CHECK(result.code == "wordpress_not_detected");
    CHECK(result.site_id == 0);
    CHECK(result.domain == "admin.test");

    const auto by_domain = service.inspect_domain("admin.test");
    CHECK_FALSE(by_domain.ok);
    CHECK(by_domain.code == "wordpress_not_detected");
    CHECK(by_domain.site_id == 0);

    fs::remove_all(root);
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

TEST_CASE("WordPressConfigService treats trailing sites root separator as equivalent") {
    site::SiteManager sites;
    const uint64_t site_id = sites.create("unity.softico.ua", "admin", 1);
    const auto base = service_root("trailing_sites_root") / "sites";
    write_service_file(base / "unity.softico.ua" / "public" / "wp-config.php",
                       service_wp_config("unity_softico_ua_db", "unity_softico_ua_user"));

    WordPressConfigService no_trailing(sites, base);
    WordPressConfigService trailing(sites, fs::path(base.string() + "/"));

    const auto no_trailing_result = no_trailing.inspect_site(site_id);
    const auto trailing_result = trailing.inspect_site(site_id);

    CHECK(no_trailing_result.ok);
    CHECK(trailing_result.ok);
    CHECK(no_trailing_result.site_root == trailing_result.site_root);
    CHECK(trailing_result.config_path.filename() == "wp-config.php");

    fs::remove_all(base.parent_path());
}

TEST_CASE("WordPressConfigService rejects traversal with trailing sites root separator") {
    site::SiteManager sites;
    std::vector<site::Site> records;
    site::Site bad;
    bad.id = 11;
    bad.name = "../../etc";
    bad.domain = "../../etc";
    bad.owner = "admin";
    bad.node_id = 1;
    records.push_back(bad);
    sites.set_sites(records);

    WordPressConfigService service(sites, "/srv/containercp/sites/");
    const auto result = service.inspect_site(11);

    CHECK_FALSE(result.ok);
    CHECK(result.status == WordPressCredentialStatus::UnsafePath);
    CHECK(result.code == "site_root_escape");
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

TEST_CASE("WordPressDatabaseCredentialResolver resolves exact enabled database target") {
    site::SiteManager sites;
    database::DatabaseManager databases;
    const uint64_t site_id = sites.create("resolver.test", "admin", 1, "nginx");
    const uint64_t other_id = sites.create("other-resolver.test", "admin", 1, "nginx");
    const auto root = service_root("resolver_exact");
    write_service_file(root / "resolver.test" / "public" / "wp-config.php", service_wp_config("wp_exact", "wp_user"));
    databases.create("wp_other", "wp_user", "otherpass", 1, site_id);
    const uint64_t database_id = databases.create("wp_exact", "wp_user", "oldpass", 1, site_id);
    databases.create("wp_exact", "wp_user", "wrongsite", 1, other_id);

    WordPressConfigService service(sites, root);
    WordPressDatabaseCredentialResolver resolver(service, databases);
    const auto status = resolver.resolve_site(site_id);

    CHECK(status.view.available);
    CHECK(status.target.available);
    CHECK(status.target.status == "resolved");
    CHECK(status.target.database_id == database_id);
    CHECK(status.target.db_name == "wp_exact");
    CHECK(status.target.db_user == "wp_user");
    CHECK(status.target.db_host == "mariadb");

    fs::remove_all(root);
}

TEST_CASE("WordPressDatabaseCredentialResolver fails closed for missing ambiguous and unmanaged targets") {
    site::SiteManager sites;
    database::DatabaseManager databases;
    const uint64_t missing_site_id = sites.create("missing-db.test", "admin", 1, "nginx");
    const uint64_t ambiguous_site_id = sites.create("ambiguous-db.test", "admin", 1, "nginx");
    const uint64_t external_site_id = sites.create("external-db.test", "admin", 1, "nginx");
    const auto root = service_root("resolver_failures");
    write_service_file(root / "missing-db.test" / "public" / "wp-config.php", service_wp_config("wp_missing", "wp_user"));
    write_service_file(root / "ambiguous-db.test" / "public" / "wp-config.php", service_wp_config("wp_dupe", "wp_user"));
    write_service_file(root / "external-db.test" / "public" / "wp-config.php", service_wp_config("wp_ext", "wp_user", "external-db"));
    databases.create("wp_dupe", "wp_user", "first", 1, ambiguous_site_id);
    databases.create("wp_dupe", "wp_user", "second", 1, ambiguous_site_id);
    databases.create("wp_ext", "wp_user", "external", 1, external_site_id);

    WordPressConfigService service(sites, root);
    WordPressDatabaseCredentialResolver resolver(service, databases);

    const auto missing = resolver.resolve_site(missing_site_id);
    CHECK_FALSE(missing.target.available);
    CHECK(missing.target.status == "database_target_missing");

    const auto ambiguous = resolver.resolve_site(ambiguous_site_id);
    CHECK_FALSE(ambiguous.target.available);
    CHECK(ambiguous.target.status == "database_target_ambiguous");

    const auto external = resolver.resolve_site(external_site_id);
    CHECK_FALSE(external.target.available);
    CHECK(external.target.status == "database_host_unsupported");

    fs::remove_all(root);
}
