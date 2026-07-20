#include "database/DatabaseViewService.h"

#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

using namespace containercp;
using namespace containercp::database;

namespace {

runtime::ContainerStatus runtime_status(std::string status) {
    runtime::ContainerStatus result;
    result.name = "mariadb";
    result.status = std::move(status);
    return result;
}

DatabaseViewCredential credential(std::string state,
                                  bool available,
                                  std::string source,
                                  std::string password = "secret") {
    DatabaseViewCredential result;
    result.state = std::move(state);
    result.available = available;
    result.source = std::move(source);
    result.db_name = "app_db";
    result.db_user = "app_user";
    result.db_host = "mariadb";
    result.password = available ? std::move(password) : std::string{};
    return result;
}

DatabaseConnectionCheck connection_result(std::string status, bool success) {
    DatabaseConnectionCheck result;
    result.attempted = true;
    result.status = std::move(status);
    result.success = success;
    return result;
}

} // namespace

TEST_CASE("DatabaseViewService returns managed database inventory without exposing passwords") {
    site::SiteManager sites;
    DatabaseManager databases;
    const auto site_id = sites.create("managed.test", "admin", 1, "apache");
    const auto database_id = databases.create("app_db", "app_user", "top-secret", 0, site_id);
    bool verified = false;

    DatabaseViewService service(
        logger::Logger::instance(), databases, sites,
        [](const site::Site&) { return runtime_status("Running"); },
        [](const Database&, const site::Site*) { return credential("available", true, "metadata", "top-secret"); },
        [&verified](const Database&, const site::Site&, const DatabaseViewCredential&) {
            verified = true;
            return connection_result("verified", true);
        });

    const auto json = service.build_enriched_json();
    CHECK(verified);
    CHECK(json.find("\"database_id\":" + std::to_string(database_id)) != std::string::npos);
    CHECK(json.find("\"site_id\":" + std::to_string(site_id)) != std::string::npos);
    CHECK(json.find("\"domain\":\"managed.test\"") != std::string::npos);
    CHECK(json.find("\"database_name\":\"app_db\"") != std::string::npos);
    CHECK(json.find("\"database_user\":\"app_user\"") != std::string::npos);
    CHECK(json.find("\"engine\":\"mariadb\"") != std::string::npos);
    CHECK(json.find("\"engine_version\":\"lts\"") != std::string::npos);
    CHECK(json.find("\"runtime_status\":\"Running\"") != std::string::npos);
    CHECK(json.find("\"connection_status\":\"verified\"") != std::string::npos);
    CHECK(json.find("\"credential_state\":\"available\"") != std::string::npos);
    CHECK(json.find("\"ownership_state\":\"managed\"") != std::string::npos);
    CHECK(json.find("\"imported_state\":\"none\"") != std::string::npos);
    CHECK(json.find("\"created_at\":\"") != std::string::npos);
    CHECK(json.find("\"updated_at\":\"") != std::string::npos);
    CHECK(json.find("top-secret") == std::string::npos);
    CHECK(json.find("db_password") == std::string::npos);
    CHECK(json.find("DB_PASSWORD") == std::string::npos);
}

TEST_CASE("DatabaseViewService supports imported database credentials from WordPress boundary") {
    site::SiteManager sites;
    DatabaseManager databases;
    const auto site_id = sites.create("imported.test", "admin", 1, "nginx");
    databases.create("app_db", "app_user", "", 0, site_id);
    bool verified = false;

    DatabaseViewService service(
        logger::Logger::instance(), databases, sites,
        [](const site::Site&) { return runtime_status("Running"); },
        [](const Database&, const site::Site*) { return credential("available", true, "wordpress_config", "wp-secret"); },
        [&verified](const Database&, const site::Site&, const DatabaseViewCredential& c) {
            verified = true;
            CHECK(c.password == "wp-secret");
            return connection_result("verified", true);
        });

    const auto view = service.build_view(databases.list().front());
    CHECK(verified);
    CHECK(view.ownership_state == "imported");
    CHECK(view.imported_state == "detected");
    CHECK(view.connection_status == "verified");
}

TEST_CASE("DatabaseViewService handles runtime stopped and unknown without connection attempts") {
    site::SiteManager sites;
    DatabaseManager databases;
    const auto site_id = sites.create("runtime.test", "admin", 1);
    databases.create("app_db", "app_user", "secret", 0, site_id);

    SUBCASE("stopped") {
        int attempts = 0;
        DatabaseViewService service(
            logger::Logger::instance(), databases, sites,
            [](const site::Site&) { return runtime_status("Stopped"); },
            [](const Database&, const site::Site*) { return credential("available", true, "metadata"); },
            [&attempts](const Database&, const site::Site&, const DatabaseViewCredential&) {
                ++attempts;
                return connection_result("verified", true);
            });
        const auto view = service.build_view(databases.list().front());
        CHECK(view.runtime_status == "Stopped");
        CHECK(view.connection_status == "not_checked");
        CHECK(attempts == 0);
    }

    SUBCASE("unknown") {
        int attempts = 0;
        DatabaseViewService service(
            logger::Logger::instance(), databases, sites,
            [](const site::Site&) { return runtime_status("Error"); },
            [](const Database&, const site::Site*) { return credential("available", true, "metadata"); },
            [&attempts](const Database&, const site::Site&, const DatabaseViewCredential&) {
                ++attempts;
                return connection_result("verified", true);
            });
        const auto view = service.build_view(databases.list().front());
        CHECK(view.runtime_status == "Unknown");
        CHECK(view.connection_status == "not_checked");
        CHECK(attempts == 0);
    }
}

TEST_CASE("DatabaseViewService reports connection failure and credential states independently") {
    site::SiteManager sites;
    DatabaseManager databases;
    const auto site_id = sites.create("states.test", "admin", 1);
    databases.create("app_db", "app_user", "secret", 0, site_id);

    SUBCASE("connection failed") {
        DatabaseViewService service(
            logger::Logger::instance(), databases, sites,
            [](const site::Site&) { return runtime_status("Running"); },
            [](const Database&, const site::Site*) { return credential("available", true, "metadata"); },
            [](const Database&, const site::Site&, const DatabaseViewCredential&) {
                return connection_result("connection_failed", false);
            });
        const auto view = service.build_view(databases.list().front());
        CHECK(view.connection_status == "connection_failed");
        CHECK(view.credential_state == "available");
    }

    SUBCASE("credential unavailable") {
        int attempts = 0;
        DatabaseViewService service(
            logger::Logger::instance(), databases, sites,
            [](const site::Site&) { return runtime_status("Running"); },
            [](const Database&, const site::Site*) { return credential("missing", false, "wordpress_config", ""); },
            [&attempts](const Database&, const site::Site&, const DatabaseViewCredential&) {
                ++attempts;
                return connection_result("verified", true);
            });
        const auto view = service.build_view(databases.list().front());
        CHECK(view.credential_state == "missing");
        CHECK(view.connection_status == "not_checked");
        CHECK(attempts == 0);
    }

    SUBCASE("credential invalid") {
        DatabaseViewService service(
            logger::Logger::instance(), databases, sites,
            [](const site::Site&) { return runtime_status("Running"); },
            [](const Database&, const site::Site*) { return credential("invalid", false, "wordpress_config", ""); },
            [](const Database&, const site::Site&, const DatabaseViewCredential&) {
                return connection_result("verified", true);
            });
        const auto view = service.build_view(databases.list().front());
        CHECK(view.credential_state == "invalid");
        CHECK(view.connection_status == "not_checked");
    }
}

TEST_CASE("DatabaseViewService reports missing site and missing runtime safely") {
    site::SiteManager sites;
    DatabaseManager databases;
    databases.create("app_db", "app_user", "secret", 0, 999);

    DatabaseViewService service(
        logger::Logger::instance(), databases, sites,
        [](const site::Site&) { return runtime_status("Running"); },
        [](const Database&, const site::Site*) { return credential("available", true, "metadata"); },
        [](const Database&, const site::Site&, const DatabaseViewCredential&) {
            return connection_result("verified", true);
        });

    const auto view = service.build_view(databases.list().front());
    CHECK(view.domain.empty());
    CHECK(view.runtime_status == "Unknown");
    CHECK(view.connection_status == "not_checked");
    CHECK(view.credential_state == "unknown");
    CHECK(view.imported_state == "site_missing");
    CHECK(service.build_enriched_json(12345) == "null");
}

TEST_CASE("WordPressConfigService internal verification credentials are not part of public view") {
    namespace fs = std::filesystem;
    site::SiteManager sites;
    const auto site_id = sites.create("wp-secret.test", "admin", 1, "nginx");
    const auto root = fs::temp_directory_path() / "containercp_db_view_wp_secret";
    fs::remove_all(root);
    fs::create_directories(root / "wp-secret.test" / "public");
    std::ofstream out(root / "wp-secret.test" / "public" / "wp-config.php");
    out << "<?php\n"
        << "define('DB_NAME', 'app_db');\n"
        << "define('DB_USER', 'app_user');\n"
        << "define('DB_PASSWORD', 'wp-internal-secret');\n"
        << "define('DB_HOST', 'mariadb');\n";
    out.close();

    wordpress::WordPressConfigService wordpress(sites, root);
    const auto public_view = wordpress.public_view(wordpress.inspect_site(site_id));
    const auto secret = wordpress.database_credentials_for_verification(site_id);

    CHECK(public_view.db_password_present);
    CHECK(public_view.db_name == "app_db");
    CHECK(public_view.db_user == "app_user");
    CHECK(public_view.db_password_present);
    CHECK(secret.available);
    CHECK(secret.db_password == "wp-internal-secret");
    CHECK(public_view.db_name.find("wp-internal-secret") == std::string::npos);
    CHECK(public_view.db_user.find("wp-internal-secret") == std::string::npos);
    fs::remove_all(root);
}
