#include "config/Config.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"
#include "site/SiteManager.h"
#include "wordpress/WordPressCliService.h"
#include "wordpress/WordPressConfigService.h"
#include "wordpress/WordPressRuntimeContext.h"

#include "doctest/doctest.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

struct MatrixSite {
    containercp::runtime::CommandExecutor& executor;
    std::filesystem::path root;
    std::filesystem::path compose_file;

    ~MatrixSite() {
        (void)executor.run({"/usr/bin/docker", "compose", "-f", compose_file.string(), "down", "-v", "--remove-orphans"});
        std::filesystem::remove_all(root);
    }
};

std::string first_line(std::string value) {
    const auto newline = value.find_first_of("\r\n");
    if (newline != std::string::npos) value.resize(newline);
    return value;
}

void write_wordpress_config(const std::filesystem::path& public_dir,
                            const std::string& database_password) {
    std::ofstream(public_dir / "wp-config.php")
        << "<?php\n"
        << "define('DB_NAME', 'wpcli');\n"
        << "define('DB_USER', 'wpcli');\n"
        << "define('DB_PASSWORD', '" << database_password << "');\n"
        << "define('DB_HOST', 'db');\n"
        << "define('DB_CHARSET', 'utf8mb4');\n"
        << "define('DB_COLLATE', '');\n"
        << "$table_prefix = 'wp_';\n"
        << "define('WP_DEBUG', false);\n"
        << "if (!defined('ABSPATH')) define('ABSPATH', __DIR__ . '/');\n"
        << "require_once ABSPATH . 'wp-settings.php';\n";
}

MatrixSite create_matrix_site(containercp::runtime::CommandExecutor& executor,
                              const std::filesystem::path& sites_root,
                              const std::string& domain,
                              uint64_t site_id,
                              const std::string& php_image,
                              const std::string& web_image,
                              const std::string& subnet,
                              const std::filesystem::path& wordpress_source,
                              const std::filesystem::path& artifact) {
    const auto root = sites_root / domain;
    const auto public_dir = root / "public";
    const auto compose_file = root / "docker-compose.yml";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(public_dir);
    for (const auto& entry : std::filesystem::directory_iterator(wordpress_source)) {
        std::filesystem::copy(entry.path(), public_dir / entry.path().filename(),
                               std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    }
    write_wordpress_config(public_dir, "wpcli-password");
    std::ofstream(compose_file)
        << "services:\n"
        << "  php:\n"
        << "    image: " << php_image << "\n"
        << "    volumes:\n"
        << "      - ./public:/var/www/html\n"
        << "    command: [\"php-fpm\", \"-F\"]\n"
        << "    labels:\n"
        << "      containercp.site.id: \"" << site_id << "\"\n"
        << "    networks: [private]\n"
        << "    depends_on: [db]\n"
        << "  db:\n"
        << "    image: mariadb:lts\n"
        << "    environment:\n"
        << "      MARIADB_DATABASE: wpcli\n"
        << "      MARIADB_USER: wpcli\n"
        << "      MARIADB_PASSWORD: wpcli-password\n"
        << "      MARIADB_ROOT_PASSWORD: root-password\n"
        << "    networks: [private]\n"
        << "  web:\n"
        << "    image: " << web_image << "\n"
        << "    networks: [private]\n"
        << "networks:\n"
        << "  private:\n"
        << "    ipam:\n"
        << "      config:\n"
        << "        - subnet: " << subnet << "\n";

    std::vector<std::string> compose_base{"/usr/bin/docker", "compose", "-f", compose_file.string()};
    auto compose = [&](const std::vector<std::string>& args) {
        std::vector<std::string> command = compose_base;
        command.insert(command.end(), args.begin(), args.end());
        return executor.run(command);
    };
    const auto up = compose({"up", "-d"});
    CAPTURE(up.exit_code);
    CAPTURE(up.out);
    CAPTURE(up.err);
    REQUIRE(up.exit_code == 0);
    bool database_ready = false;
    for (int attempt = 0; attempt < 60 && !database_ready; ++attempt) {
        database_ready = compose({"exec", "-T", "db", "mariadb", "-uwpcli", "-pwpcli-password",
                                  "-e", "SELECT 1", "wpcli"}).exit_code == 0;
        if (!database_ready) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    REQUIRE(database_ready);
    const auto install = compose({"run", "--rm", "-T", "-v",
                                  artifact.string() + ":/tmp/wp-cli.phar:ro", "php", "php", "/tmp/wp-cli.phar",
                                  "core", "install", "--url=http://" + domain,
                                  "--title=ContainerCP Matrix", "--admin_user=admin",
                                  "--admin_password=admin-password", "--admin_email=admin@example.com",
                                  "--skip-email", "--allow-root"});
    CAPTURE(install.exit_code);
    CAPTURE(install.out);
    CAPTURE(install.err);
    REQUIRE(install.exit_code == 0);
    return {executor, root, compose_file};
}

} // namespace

TEST_CASE("WordPressCliService disposable Apache Nginx multi-site multi-PHP matrix") {
    const char* enabled = std::getenv("CONTAINERCP_WPCLI_MATRIX");
    if (enabled == nullptr || std::string(enabled) != "1") {
        MESSAGE("Set CONTAINERCP_WPCLI_MATRIX=1 to run the Apache/Nginx multi-PHP matrix gate");
        return;
    }
    const char* source_env = std::getenv("CONTAINERCP_WPCLI_WORDPRESS_ROOT");
    REQUIRE(source_env != nullptr);
    const std::filesystem::path wordpress_source(source_env);
    REQUIRE(std::filesystem::is_directory(wordpress_source));

    auto& config = containercp::config::Config::instance();
    const std::filesystem::path artifact = config.data_root() + "/wp-cli/wp-cli.phar";
    REQUIRE(std::filesystem::is_regular_file(artifact));
    containercp::runtime::CommandExecutor executor;
    containercp::site::SiteManager sites;
    const auto nginx_id = sites.create("wpcli-matrix-nginx.local", "admin", 1, "nginx");
    const auto apache_id = sites.create("wpcli-matrix-apache.local", "admin", 1, "apache");
    REQUIRE(nginx_id == 1);
    REQUIRE(apache_id == 2);

    auto nginx = create_matrix_site(executor, config.sites_dir(), "wpcli-matrix-nginx.local", nginx_id,
                                    "containercp-wpcli-php:8.4", "nginx:alpine", "10.250.0.0/24", wordpress_source, artifact);
    auto apache = create_matrix_site(executor, config.sites_dir(), "wpcli-matrix-apache.local", apache_id,
                                     "containercp-wpcli-php:8.3", "httpd:alpine", "10.251.0.0/24", wordpress_source, artifact);

    auto& logger = containercp::logger::Logger::instance();
    containercp::wordpress::WordPressConfigService config_service(sites);
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, sites, config_service, config, logger);
    containercp::wordpress::WordPressCliService service(executor, resolver, config, logger);

    const auto nginx_context = resolver.resolve(nginx_id);
    const auto apache_context = resolver.resolve(apache_id);
    REQUIRE(nginx_context.ok);
    REQUIRE(apache_context.ok);
    CHECK(nginx_context.configured_php_image == "containercp-wpcli-php:8.4");
    CHECK(apache_context.configured_php_image == "containercp-wpcli-php:8.3");
    CHECK(nginx_context.immutable_php_image_id != apache_context.immutable_php_image_id);
    CHECK(nginx_context.private_network != apache_context.private_network);
    CHECK(nginx_context.document_root != apache_context.document_root);
    CHECK(nginx_context.php_container != apache_context.php_container);
    CHECK(nginx_context.php_fpm_uid > 0);
    CHECK(apache_context.php_fpm_uid > 0);
    CHECK(nginx_context.php_fpm_gid > 0);
    CHECK(apache_context.php_fpm_gid > 0);

    const auto nginx_images = executor.run({"/usr/bin/docker", "compose", "-f", nginx.compose_file.string(), "config", "--images"});
    const auto apache_images = executor.run({"/usr/bin/docker", "compose", "-f", apache.compose_file.string(), "config", "--images"});
    REQUIRE(nginx_images.exit_code == 0);
    REQUIRE(apache_images.exit_code == 0);
    CHECK(nginx_images.out.find("nginx:alpine") != std::string::npos);
    CHECK(apache_images.out.find("httpd:alpine") != std::string::npos);

    const auto nginx_web_container = executor.run({"/usr/bin/docker", "compose", "-f", nginx.compose_file.string(), "ps", "-q", "web"});
    const auto apache_web_container = executor.run({"/usr/bin/docker", "compose", "-f", apache.compose_file.string(), "ps", "-q", "web"});
    REQUIRE(nginx_web_container.exit_code == 0);
    REQUIRE(apache_web_container.exit_code == 0);
    REQUIRE(!nginx_web_container.out.empty());
    REQUIRE(!apache_web_container.out.empty());
    const auto nginx_web_image = executor.run({"/usr/bin/docker", "inspect", first_line(nginx_web_container.out),
                                               "--format", "{{.Config.Image}}"});
    const auto apache_web_image = executor.run({"/usr/bin/docker", "inspect", first_line(apache_web_container.out),
                                                "--format", "{{.Config.Image}}"});
    REQUIRE(nginx_web_image.exit_code == 0);
    REQUIRE(apache_web_image.exit_code == 0);
    CHECK(nginx_web_image.out.find("nginx:alpine") != std::string::npos);
    CHECK(apache_web_image.out.find("httpd:alpine") != std::string::npos);

    for (const auto site_id : {nginx_id, apache_id}) {
        const auto installed = service.run(site_id, containercp::wordpress::WordPressCliOperation::CoreIsInstalled);
        REQUIRE(installed.success);
        CHECK(installed.cleanup_succeeded);
        CHECK(service.run(site_id, containercp::wordpress::WordPressCliOperation::CoreVersion).success);
        CHECK(service.run(site_id, containercp::wordpress::WordPressCliOperation::PluginList).success);
        CHECK(service.run(site_id, containercp::wordpress::WordPressCliOperation::ThemeList).success);
        CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::CacheFlush).success);
    }

    CHECK(service.run(nginx_id, containercp::wordpress::WordPressCliOperation::CoreVersion).success);
    CHECK(service.run(apache_id, containercp::wordpress::WordPressCliOperation::CoreVersion).success);
    const auto runners = executor.run({"/usr/bin/docker", "ps", "-a", "--filter",
                                       "label=containercp.wpcli.managed=true", "--format", "{{.ID}}"});
    REQUIRE(runners.exit_code == 0);
    CHECK(runners.out.empty());
}
