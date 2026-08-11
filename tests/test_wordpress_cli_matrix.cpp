#include "config/Config.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"
#include "site/SiteManager.h"
#include "wordpress/WordPressCliService.h"
#include "wordpress/WordPressConfigService.h"
#include "wordpress/WordPressRuntimeContext.h"

#include "doctest/doctest.h"

#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
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

    const auto php_container_result = compose({"ps", "-q", "php"});
    REQUIRE(php_container_result.exit_code == 0);
    const auto php_container = first_line(php_container_result.out);
    REQUIRE(!php_container.empty());
    const auto php_top = executor.run({"/usr/bin/docker", "top", php_container, "-eo", "pid,uid,gid,args"});
    REQUIRE(php_top.exit_code == 0);
    int64_t php_uid = -1;
    int64_t php_gid = -1;
    std::istringstream php_lines(php_top.out);
    std::string php_line;
    while (std::getline(php_lines, php_line)) {
        if (php_line.find("php-fpm") == std::string::npos || php_line.find("pool") == std::string::npos) continue;
        std::istringstream fields(php_line);
        std::string pid_token;
        std::string uid_token;
        std::string gid_token;
        if (fields >> pid_token >> uid_token >> gid_token &&
            std::all_of(uid_token.begin(), uid_token.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) &&
            std::all_of(gid_token.begin(), gid_token.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            php_uid = std::stoll(uid_token);
            php_gid = std::stoll(gid_token);
            break;
        }
    }
    REQUIRE(php_uid > 0);
    REQUIRE(php_gid > 0);
    REQUIRE(executor.run({"/usr/bin/chown", "-R", std::to_string(php_uid) + ":" + std::to_string(php_gid),
                          public_dir.string()}).exit_code == 0);
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

    const auto run_full_mutations = [&](uint64_t site_id, const std::filesystem::path& root) {
        const auto public_dir = root / "public";
        const auto plugin_source = public_dir / "matrix-plugin";
        std::filesystem::create_directories(plugin_source);
        std::ofstream(plugin_source / "matrix-plugin.php")
            << "<?php\n/* Plugin Name: ContainerCP Matrix Plugin */\n";
        REQUIRE(executor.run({"/usr/bin/zip", "-qr", (public_dir / "matrix-plugin.zip").string(),
                              plugin_source.filename().string()}, public_dir.string()).exit_code == 0);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::PluginInstall,
                                     "matrix-plugin.zip").success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::PluginActivate,
                                     "matrix-plugin").success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::PluginDeactivate,
                                     "matrix-plugin").success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::PluginUpdate,
                                     "matrix-plugin").success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::PluginDelete,
                                     "matrix-plugin").success);

        const auto theme_source = public_dir / "matrix-theme";
        std::filesystem::create_directories(theme_source);
        std::ofstream(theme_source / "style.css") << "/*\nTheme Name: ContainerCP Matrix Theme\n*/\n";
        std::ofstream(theme_source / "index.php") << "<?php\n";
        REQUIRE(executor.run({"/usr/bin/zip", "-qr", (public_dir / "matrix-theme.zip").string(),
                              theme_source.filename().string()}, public_dir.string()).exit_code == 0);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::ThemeInstall,
                                     "matrix-theme.zip").success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::ThemeActivate,
                                     "matrix-theme").success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::ThemeUpdate,
                                     "matrix-theme").success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::ThemeActivate,
                                     "twentytwentyfive").success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::ThemeDelete,
                                     "matrix-theme").success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::CoreUpdate).success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::LanguageInstall,
                                     "en_US").success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::LanguageUpdate).success);
        REQUIRE(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::CacheFlush).success);

        for (const auto& entry : std::filesystem::recursive_directory_iterator(public_dir / "wp-content")) {
            struct stat metadata{};
            if (std::filesystem::is_regular_file(entry.symlink_status()) &&
                ::lstat(entry.path().c_str(), &metadata) == 0) {
                CHECK(metadata.st_uid != 0);
            }
        }
    };

    const std::filesystem::path sites_root = config.sites_dir();
    run_full_mutations(nginx_id, sites_root / "wpcli-matrix-nginx.local");
    run_full_mutations(apache_id, sites_root / "wpcli-matrix-apache.local");

    const auto nginx_sentinel = sites_root / "wpcli-matrix-nginx.local/public/matrix-sentinel.txt";
    const auto apache_sentinel = sites_root / "wpcli-matrix-apache.local/public/matrix-sentinel.txt";
    std::ofstream(nginx_sentinel) << "nginx-only";
    std::ofstream(apache_sentinel) << "apache-only";
    CHECK(service.run(nginx_id, containercp::wordpress::WordPressCliOperation::CoreVersion).success);
    CHECK(service.run(apache_id, containercp::wordpress::WordPressCliOperation::CoreVersion).success);
    CHECK(std::filesystem::exists(nginx_sentinel));
    CHECK(std::filesystem::exists(apache_sentinel));

    const auto timeout_wrapper = std::filesystem::path("/tmp/containercp-wpcli-matrix-timeout-docker.sh");
    const auto timeout_name_file = std::filesystem::path("/tmp/containercp-wpcli-matrix-timeout-name");
    struct TimeoutCleanup {
        containercp::runtime::CommandExecutor& executor;
        std::filesystem::path wrapper;
        std::filesystem::path name_file;
        ~TimeoutCleanup() {
            std::ifstream input(name_file);
            std::string name;
            std::getline(input, name);
            if (!name.empty()) (void)executor.run({"/usr/bin/docker", "rm", "-f", name});
            std::filesystem::remove(wrapper);
            std::filesystem::remove(name_file);
        }
    } timeout_cleanup{executor, timeout_wrapper, timeout_name_file};
    std::ofstream(timeout_wrapper)
        << "#!/bin/sh\n"
        << "if [ \"$1\" = run ]; then\n"
        << "  shift; runner=; site=; operation=; execution=; runner_id=\n"
        << "  while [ $# -gt 0 ]; do\n"
        << "    case \"$1\" in\n"
        << "      --name) runner=\"$2\"; shift 2;;\n"
        << "      --label) case \"$2\" in containercp.wpcli.site.id=*) site=\"${2#*=}\";; containercp.wpcli.operation=*) operation=\"${2#*=}\";; containercp.wpcli.execution.id=*) execution=\"${2#*=}\";; containercp.wpcli.runner.id=*) runner_id=\"${2#*=}\";; esac; shift 2;;\n"
        << "      *) shift;;\n"
        << "    esac\n"
        << "  done\n"
        << "  printf '%s\\n' \"$runner\" > '" << timeout_name_file.string() << "'\n"
        << "  /usr/bin/docker run -d --name \"$runner\" --label containercp.wpcli.managed=true --label containercp.wpcli.site.id=\"$site\" --label containercp.wpcli.operation=\"$operation\" --label containercp.wpcli.execution.id=\"$execution\" --label containercp.wpcli.runner.id=\"$runner_id\" alpine sleep 30 >/dev/null\n"
        << "  sleep 10\n"
        << "  exit 124\n"
        << "fi\n"
        << "exec /usr/bin/docker \"$@\"\n";
    REQUIRE(::chmod(timeout_wrapper.c_str(), 0555) == 0);
    containercp::wordpress::WordPressCliService real_timeout_service(
        executor, resolver, config, logger, timeout_wrapper.string(), 1, 5);
    const auto real_timeout = real_timeout_service.run(
        nginx_id, containercp::wordpress::WordPressCliOperation::CoreVersion);
    CHECK_FALSE(real_timeout.success);
    CHECK(real_timeout.failure_code == "wordpress_cli_timeout");
    CHECK(real_timeout.cleanup_succeeded);
    const auto remaining_runners = executor.run({"/usr/bin/docker", "ps", "-a", "--filter",
                                                 "label=containercp.wpcli.managed=true", "--format", "{{.Names}}"});
    REQUIRE(remaining_runners.exit_code == 0);
    CHECK(remaining_runners.out.empty());

    CHECK(service.run(nginx_id, containercp::wordpress::WordPressCliOperation::CoreVersion).success);
    CHECK(service.run(apache_id, containercp::wordpress::WordPressCliOperation::CoreVersion).success);
    const auto runners = executor.run({"/usr/bin/docker", "ps", "-a", "--filter",
                                       "label=containercp.wpcli.managed=true", "--format", "{{.ID}}"});
    REQUIRE(runners.exit_code == 0);
    CHECK(runners.out.empty());
}
