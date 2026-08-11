#include "config/Config.h"
#include "filesystem/Filesystem.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"
#include "site/SiteManager.h"
#include "wordpress/WordPressConfigService.h"
#include "wordpress/WordPressRuntimeContext.h"

#include "doctest/doctest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct ContextFixture {
    containercp::config::Config& config;
    containercp::site::SiteManager sites;
    std::string domain;
    std::string site_dir;
    std::string fake_dir;
    std::string saved_path;
    uint64_t site_id = 0;

    void cleanup() {
        setenv("PATH", saved_path.c_str(), 1);
        unsetenv("CONTAINERCP_CONTEXT_FAKE_MODE");
        std::filesystem::remove_all(site_dir);
        std::filesystem::remove_all(fake_dir);
    }
};

ContextFixture make_fixture(const std::string& domain,
                            const std::string& web_server = "apache") {
    auto& config = containercp::config::Config::instance();
    ContextFixture fixture{config, {}, domain, config.sites_dir() + domain + "/",
                           "/tmp/containercp-context-fake-docker", {}, 0};
    std::filesystem::remove_all(fixture.site_dir);
    std::filesystem::remove_all(fixture.fake_dir);
    std::filesystem::create_directories(fixture.site_dir + "public");
    std::ofstream(fixture.site_dir + "docker-compose.yml") << "services: {}\n";
    std::ofstream(fixture.site_dir + "public/wp-config.php")
        << "<?php\n"
        << "define('DB_NAME', 'context_db');\n"
        << "define('DB_USER', 'context_user');\n"
        << "define('DB_PASSWORD', 'context_secret');\n"
        << "define('DB_HOST', 'mariadb');\n";
    fixture.site_id = fixture.sites.create(domain, "admin", 1, web_server);
    std::filesystem::create_directories(fixture.fake_dir);

    const std::string image_id = "sha256:" + std::string(64, 'a');
    const std::string other_image_id = "sha256:" + std::string(64, 'b');
    std::ofstream script(fixture.fake_dir + "/docker");
    script << "#!/bin/sh\n"
           << "mode=\"${CONTAINERCP_CONTEXT_FAKE_MODE:-ok}\"\n"
           << "site_dir='" << fixture.site_dir << "'\n"
           << "site_id='" << fixture.site_id << "'\n"
           << "image_id='" << image_id << "'\n"
           << "other_image_id='" << other_image_id << "'\n"
           << "if [ \"$1\" = exec ] && [ \"$mode\" = filesystem-broken ]; then exit 1; fi\n"
           << "if [ \"$1\" = compose ] && [ \"$4\" = ps ]; then\n"
           << "  if [ \"$8\" = php ]; then\n"
           << "    [ \"$mode\" = missing-php ] && exit 0\n"
           << "    printf 'site-%s-php\\n' \"$site_id\"\n"
           << "    [ \"$mode\" = duplicate-php ] && printf 'site-%s-php-duplicate\\n' \"$site_id\"\n"
           << "  fi\n"
           << "  exit 0\n"
           << "fi\n"
           << "if [ \"$1\" = image ] && [ \"$2\" = inspect ]; then\n"
           << "  [ \"$mode\" = wrong-image ] && printf '%s\\n' \"$other_image_id\" || printf '%s\\n' \"$image_id\"\n"
           << "  exit 0\n"
           << "fi\n"
           << "if [ \"$1\" = network ] && [ \"$2\" = inspect ]; then\n"
           << "  case \"$3\" in\n"
           << "    other_project_site) printf '%s|other_project|other-network\\n' \"$3\" ;;\n"
           << "    *) printf '%s|site_project|containercp-site-%s\\n' \"$3\" \"$site_id\" ;;\n"
           << "  esac\n"
           << "  exit 0\n"
           << "fi\n"
           << "if [ \"$1\" = top ]; then\n"
           << "  case \"$mode\" in\n"
           << "    root-fpm) printf '100 0 0 php-fpm: pool www\\n' ;;\n"
           << "    missing-fpm|sftp-identity) printf '100 20001 20001 sftp-server\\n' ;;\n"
           << "    *) printf '100 20001 20001 php-fpm: pool www\\n' ;;\n"
           << "  esac\n"
           << "  exit 0\n"
           << "fi\n"
           << "if [ \"$1\" = inspect ]; then\n"
           << "  case \"$4\" in\n"
           << "    '{{range .Mounts}}'*)\n"
           << "      if [ \"$mode\" = missing-mount ]; then printf '%slogs|/var/log/php|true\\n' \"$site_dir\"; exit 0; fi\n"
           << "      if [ \"$mode\" = cross-mount ]; then printf '/srv/containercp/sites/other.local/public|/var/www/html|true\\n'; exit 0; fi\n"
           << "      if [ \"$mode\" = duplicate-mount ]; then printf '%spublic|/var/www/html|true\\n%spublic|/usr/share/nginx/html|true\\n' \"$site_dir\" \"$site_dir\"; exit 0; fi\n"
           << "      printf '%spublic|/var/www/html|true\\n' \"$site_dir\"; exit 0 ;;\n"
           << "    '{{range $name, $network'*)\n"
           << "      case \"$mode\" in\n"
           << "        public-network) printf 'containercp-public|network-public\\n' ;;\n"
           << "        unrelated-network) printf 'other_project_site|network-other\\n' ;;\n"
           << "        multiple-networks) printf 'site_project_private|network-one\\nsite_project_private_two|network-two\\n' ;;\n"
           << "        *) printf 'site_project_private|network-site\\n' ;;\n"
           << "      esac\n"
           << "      exit 0 ;;\n"
           << "  esac\n"
           << "  case \"$mode\" in\n"
           << "    wrong-site) printf '%s|php|%s|running|custom:php|%s\\n' \"$((site_id + 1))\" \"$site_dir\" \"$image_id\" ;;\n"
           << "    wrong-compose) printf '%s|php|other_project|/srv/containercp/sites/other.local|running|custom:php|%s\\n' \"$site_id\" \"$image_id\" ;;\n"
           << "    stopped-php) printf '%s|php|site_project|%s|exited|custom:php|%s\\n' \"$site_id\" \"$site_dir\" \"$image_id\" ;;\n"
           << "    missing-image) printf '%s|php|site_project|%s|running||\\n' \"$site_id\" \"$site_dir\" ;;\n"
           << "    wrong-image) printf '%s|php|site_project|%s|running|custom:php|%s\\n' \"$site_id\" \"$site_dir\" \"$image_id\" ;;\n"
           << "    *) printf '%s|php|site_project|%s|running|custom:php|%s\\n' \"$site_id\" \"$site_dir\" \"$image_id\" ;;\n"
           << "  esac\n"
           << "  exit 0\n"
           << "fi\n"
           << "exit 0\n";
    script.close();
    ::chmod((fixture.fake_dir + "/docker").c_str(), 0755);

    const char* old_path = std::getenv("PATH");
    fixture.saved_path = old_path == nullptr ? std::string() : old_path;
    setenv("PATH", (fixture.fake_dir + ":" + fixture.saved_path).c_str(), 1);
    return fixture;
}

void expect_failure(const std::string& mode, const std::string& expected_code) {
    auto fixture = make_fixture("context-" + mode + ".local");
    setenv("CONTAINERCP_CONTEXT_FAKE_MODE", mode.c_str(), 1);
    containercp::runtime::CommandExecutor executor;
    containercp::wordpress::WordPressConfigService config_service(fixture.sites);
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, fixture.sites, config_service, fixture.config, containercp::logger::Logger::instance());

    const auto context = resolver.resolve(fixture.site_id);
    CHECK_FALSE(context.ok);
    CHECK(context.failure_code == expected_code);
    fixture.cleanup();
}

} // namespace

TEST_CASE("WordPressRuntimeContext resolves the canonical managed runtime") {
    auto fixture = make_fixture("context-happy.local");
    containercp::runtime::CommandExecutor executor;
    containercp::wordpress::WordPressConfigService config_service(fixture.sites);
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, fixture.sites, config_service, fixture.config, containercp::logger::Logger::instance());

    const auto context = resolver.resolve(fixture.site_id);
    REQUIRE(context.ok);
    CHECK(context.site_id == fixture.site_id);
    CHECK(context.domain == "context-happy.local");
    CHECK(context.php_container == "site-" + std::to_string(fixture.site_id) + "-php");
    CHECK(context.configured_php_image == "custom:php");
    CHECK(context.immutable_php_image_id == "sha256:" + std::string(64, 'a'));
    CHECK(context.private_network == "site_project_private");
    CHECK(context.private_network_id == "network-site");
    CHECK(context.container_document_root == "/var/www/html");
    CHECK(context.php_fpm_uid == 20001);
    CHECK(context.php_fpm_gid == 20001);
    CHECK(context.runtime_capable);
    CHECK(context.read_only_capable);
    CHECK(context.mutation_capable);
    fixture.cleanup();
}

TEST_CASE("WordPressRuntimeContext rejects missing and system sites") {
    auto& config = containercp::config::Config::instance();
    containercp::site::SiteManager sites;
    containercp::runtime::CommandExecutor executor;
    containercp::wordpress::WordPressConfigService config_service(sites);
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, sites, config_service, config, containercp::logger::Logger::instance());

    const auto missing = resolver.resolve(987654321);
    CHECK_FALSE(missing.ok);
    CHECK(missing.failure_code == "site_not_found");

    const auto system_id = sites.create("context-system.local", "system", 0, "apache");
    const auto system = resolver.resolve(system_id);
    CHECK_FALSE(system.ok);
    CHECK(system.failure_code == "system_site_rejected");
}

TEST_CASE("WordPressRuntimeContext rejects unsafe canonical paths") {
    auto& config = containercp::config::Config::instance();
    containercp::runtime::CommandExecutor executor;
    containercp::site::SiteManager sites;
    containercp::wordpress::WordPressConfigService config_service(sites);
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, sites, config_service, config, containercp::logger::Logger::instance());

    const auto traversal_id = sites.create("../context-escape.local", "admin", 1, "apache");
    const auto traversal = resolver.resolve(traversal_id);
    CHECK_FALSE(traversal.ok);
    CHECK(traversal.failure_code == "site_root_escape");

    const std::string domain = "context-symlink.local";
    const std::string site_dir = config.sites_dir() + domain + "/";
    const std::string site_link = config.sites_dir() + domain;
    const std::string target = "/tmp/context-symlink-target";
    std::filesystem::remove_all(site_dir);
    std::filesystem::remove_all(target);
    std::filesystem::create_directories(target);
    std::filesystem::create_directories(config.sites_dir());
    REQUIRE(::symlink(target.c_str(), site_link.c_str()) == 0);
    const auto symlink_id = sites.create(domain, "admin", 1, "apache");
    const auto symlink = resolver.resolve(symlink_id);
    CHECK_FALSE(symlink.ok);
    CHECK(symlink.failure_code == "site_root_unsafe");
    std::filesystem::remove(site_link);
    std::filesystem::remove_all(target);
}

TEST_CASE("WordPressRuntimeContext rejects PHP service identity failures") {
    expect_failure("missing-php", "wordpress_php_service_ambiguous");
    expect_failure("duplicate-php", "wordpress_php_service_ambiguous");
    expect_failure("wrong-site", "wordpress_php_identity_unproven");
    expect_failure("wrong-compose", "wordpress_php_identity_unproven");
    expect_failure("stopped-php", "wordpress_php_identity_unproven");
    expect_failure("missing-image", "wordpress_php_image_unproven");
    expect_failure("wrong-image", "wordpress_php_image_mismatch");
}

TEST_CASE("WordPressRuntimeContext fails closed for unproven filesystem access without root fallback") {
    auto fixture = make_fixture("context-filesystem-broken.local");
    setenv("CONTAINERCP_CONTEXT_FAKE_MODE", "filesystem-broken", 1);
    containercp::runtime::CommandExecutor executor;
    containercp::wordpress::WordPressConfigService config_service(fixture.sites);
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, fixture.sites, config_service, fixture.config, containercp::logger::Logger::instance());

    const auto context = resolver.resolve(fixture.site_id);
    CHECK(context.ok);
    CHECK(context.runtime_capable);
    CHECK_FALSE(context.read_only_capable);
    CHECK_FALSE(context.mutation_capable);
    CHECK_FALSE(context.filesystem_read_access_proven);
    CHECK_FALSE(context.filesystem_mutation_access_proven);
    CHECK(context.failure_code == "wordpress_filesystem_access_unproven");
    fixture.cleanup();
}

TEST_CASE("WordPressRuntimeContext rejects network and mount isolation failures") {
    expect_failure("public-network", "wordpress_public_network_rejected");
    expect_failure("unrelated-network", "wordpress_unrelated_network_rejected");
    expect_failure("multiple-networks", "wordpress_private_network_unproven");
    expect_failure("missing-mount", "wordpress_document_root_mount_unproven");
    expect_failure("cross-mount", "wordpress_cross_site_mount_rejected");
    expect_failure("duplicate-mount", "wordpress_document_root_mount_unproven");
}

TEST_CASE("WordPressRuntimeContext rejects unproven PHP-FPM identity") {
    expect_failure("root-fpm", "wordpress_filesystem_identity_unproven");
    expect_failure("missing-fpm", "wordpress_filesystem_identity_unproven");
    expect_failure("sftp-identity", "wordpress_filesystem_identity_unproven");
}
