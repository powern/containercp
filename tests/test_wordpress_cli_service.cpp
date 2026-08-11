#include "config/Config.h"
#include "jobs/JobManager.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"
#include "site/SiteManager.h"
#include "storage/Verification.h"
#include "wordpress/WordPressCliService.h"
#include "wordpress/WordPressCliAudit.h"
#include "wordpress/WordPressConfigService.h"
#include "wordpress/WordPressRuntimeContext.h"

#include "doctest/doctest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {

struct ArtifactFixture {
    std::filesystem::path directory;

    ArtifactFixture()
        : directory(containercp::config::Config::instance().data_root() + "/wp-cli") {
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
    }

    ~ArtifactFixture() {
        std::filesystem::remove_all(directory);
    }

    void write(const std::string& content,
               const std::string& version = "2.11.0",
               const std::string& sha = "") {
        std::ofstream(directory / "wp-cli.phar", std::ios::binary) << content;
        std::ofstream(directory / "version") << version << "\n";
        const auto checksum = sha.empty() ? containercp::storage::Verification::sha256(content) : sha;
        std::ofstream(directory / "sha256") << checksum << "\n";
        ::chmod((directory / "wp-cli.phar").c_str(), 0444);
        ::chmod((directory / "version").c_str(), 0444);
        ::chmod((directory / "sha256").c_str(), 0444);
    }
};

containercp::wordpress::WordPressCliService make_service(
    containercp::runtime::CommandExecutor& executor,
    containercp::wordpress::WordPressRuntimeContextResolver& resolver,
    containercp::config::Config& config) {
    return containercp::wordpress::WordPressCliService(
        executor, resolver, config, containercp::logger::Logger::instance());
}

} // namespace

TEST_CASE("WordPressCliService maps only the typed read-only allowlist") {
    const auto core_installed = containercp::wordpress::WordPressCliService::operation_arguments(
        containercp::wordpress::WordPressCliOperation::CoreIsInstalled);
    CHECK(core_installed == std::vector<std::string>{"--no-color", "--skip-plugins", "--skip-themes", "core", "is-installed"});

    const auto core_version = containercp::wordpress::WordPressCliService::operation_arguments(
        containercp::wordpress::WordPressCliOperation::CoreVersion);
    CHECK(core_version == std::vector<std::string>{"--no-color", "--skip-plugins", "--skip-themes", "core", "version"});

    const auto plugins = containercp::wordpress::WordPressCliService::operation_arguments(
        containercp::wordpress::WordPressCliOperation::PluginList);
    CHECK(plugins == std::vector<std::string>{"--no-color", "--skip-plugins", "--skip-themes", "plugin", "list"});

    const auto themes = containercp::wordpress::WordPressCliService::operation_arguments(
        containercp::wordpress::WordPressCliOperation::ThemeList);
    CHECK(themes == std::vector<std::string>{"--no-color", "--skip-plugins", "--skip-themes", "theme", "list"});

    containercp::wordpress::WordPressCliOperation parsed;
    CHECK(containercp::wordpress::parseWordPressCliOperation("core-version", parsed));
    CHECK(parsed == containercp::wordpress::WordPressCliOperation::CoreVersion);
    CHECK_FALSE(containercp::wordpress::parseWordPressCliOperation("eval", parsed));
    CHECK_FALSE(containercp::wordpress::parseWordPressCliOperation("core version", parsed));
}

TEST_CASE("WordPressCliService maps typed mutations and rejects shell syntax") {
    const std::vector<std::pair<std::string, containercp::wordpress::WordPressCliMutation>> mutations{
        {"plugin-install", containercp::wordpress::WordPressCliMutation::PluginInstall},
        {"plugin-activate", containercp::wordpress::WordPressCliMutation::PluginActivate},
        {"plugin-deactivate", containercp::wordpress::WordPressCliMutation::PluginDeactivate},
        {"plugin-update", containercp::wordpress::WordPressCliMutation::PluginUpdate},
        {"plugin-delete", containercp::wordpress::WordPressCliMutation::PluginDelete},
        {"theme-install", containercp::wordpress::WordPressCliMutation::ThemeInstall},
        {"theme-activate", containercp::wordpress::WordPressCliMutation::ThemeActivate},
        {"theme-update", containercp::wordpress::WordPressCliMutation::ThemeUpdate},
        {"theme-delete", containercp::wordpress::WordPressCliMutation::ThemeDelete},
        {"core-update", containercp::wordpress::WordPressCliMutation::CoreUpdate},
        {"language-install", containercp::wordpress::WordPressCliMutation::LanguageInstall},
        {"language-update", containercp::wordpress::WordPressCliMutation::LanguageUpdate},
        {"cache-flush", containercp::wordpress::WordPressCliMutation::CacheFlush},
    };
    for (const auto& [name, mutation] : mutations) {
        containercp::wordpress::WordPressCliMutation parsed;
        CHECK(containercp::wordpress::parseWordPressCliMutation(name, parsed));
        CHECK(parsed == mutation);
    }
    CHECK(containercp::wordpress::WordPressCliService::mutation_arguments(
              containercp::wordpress::WordPressCliMutation::PluginInstall, "akismet") ==
          std::vector<std::string>{"--no-color", "plugin", "install", "akismet"});
    CHECK(containercp::wordpress::WordPressCliService::mutation_arguments(
              containercp::wordpress::WordPressCliMutation::CoreUpdate, "") ==
          std::vector<std::string>{"--no-color", "core", "update"});
    CHECK(containercp::wordpress::validWordPressCliPackageIdentifier("vendor/plugin"));
    CHECK_FALSE(containercp::wordpress::validWordPressCliPackageIdentifier("plugin;id"));
    CHECK_FALSE(containercp::wordpress::validWordPressCliPackageIdentifier("../plugin"));
    CHECK_FALSE(containercp::wordpress::validWordPressCliPackageIdentifier("/tmp/plugin.zip"));
    containercp::wordpress::WordPressCliMutation rejected;
    CHECK_FALSE(containercp::wordpress::parseWordPressCliMutation("eval", rejected));
}

TEST_CASE("WordPress CLI jobs expose cleanup state and safe audit fields") {
    containercp::jobs::JobManager jobs;
    const auto job_id = jobs.create("wordpress-cli-plugin-install", {"Validating typed mutation"});
    jobs.update_cleanup(job_id, "succeeded", "Runner removed");
    const auto* job = jobs.find(job_id);
    REQUIRE(job != nullptr);
    CHECK(job->cleanup_status == "succeeded");
    CHECK(job->cleanup_message == "Runner removed");

    const auto audit = containercp::wordpress::WordPressCliAuditLogger::format({
        7, 4, "example.test", "admin\noperator", "plugin-install", "vendor/plugin",
        "failure", "wordpress_cli_timeout", true, true,
        containercp::wordpress::WordPressCliAuditEvent::Level::Error});
    CHECK(audit.find("admin_operator") != std::string::npos);
    CHECK(audit.find("wordpress_cli_timeout") != std::string::npos);
    CHECK(audit.find("password") == std::string::npos);
}

TEST_CASE("WordPressCliService fails closed when the Phar is missing") {
    ArtifactFixture fixture;
    auto& config = containercp::config::Config::instance();
    containercp::site::SiteManager sites;
    containercp::wordpress::WordPressConfigService config_service(sites);
    containercp::runtime::CommandExecutor executor;
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, sites, config_service, config, containercp::logger::Logger::instance());
    auto service = make_service(executor, resolver, config);

    const auto artifact = service.validate_artifact();
    CHECK_FALSE(artifact.ok);
    CHECK(artifact.failure_code == "wordpress_cli_artifact_missing");
}

TEST_CASE("WordPressCliService verifies pinned Phar metadata and SHA-256") {
    ArtifactFixture fixture;
    fixture.write("stable-phar-content");
    auto& config = containercp::config::Config::instance();
    containercp::site::SiteManager sites;
    containercp::wordpress::WordPressConfigService config_service(sites);
    containercp::runtime::CommandExecutor executor;
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, sites, config_service, config, containercp::logger::Logger::instance());
    auto service = make_service(executor, resolver, config);

    const auto artifact = service.validate_artifact();
    REQUIRE(artifact.ok);
    CHECK(artifact.version == "2.11.0");
    CHECK(artifact.sha256 == containercp::storage::Verification::sha256("stable-phar-content"));
}

TEST_CASE("WordPressCliService rejects a wrong Phar SHA-256") {
    ArtifactFixture fixture;
    fixture.write("stable-phar-content", "2.11.0", std::string(64, '0'));
    auto& config = containercp::config::Config::instance();
    containercp::site::SiteManager sites;
    containercp::wordpress::WordPressConfigService config_service(sites);
    containercp::runtime::CommandExecutor executor;
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, sites, config_service, config, containercp::logger::Logger::instance());
    auto service = make_service(executor, resolver, config);

    const auto artifact = service.validate_artifact();
    CHECK_FALSE(artifact.ok);
    CHECK(artifact.failure_code == "wordpress_cli_artifact_integrity_failed");
}

TEST_CASE("WordPressCliService rejects an unreviewed Phar version") {
    ArtifactFixture fixture;
    fixture.write("stable-phar-content", "2.12.0");
    auto& config = containercp::config::Config::instance();
    containercp::site::SiteManager sites;
    containercp::wordpress::WordPressConfigService config_service(sites);
    containercp::runtime::CommandExecutor executor;
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, sites, config_service, config, containercp::logger::Logger::instance());
    auto service = make_service(executor, resolver, config);

    const auto artifact = service.validate_artifact();
    CHECK_FALSE(artifact.ok);
    CHECK(artifact.failure_code == "wordpress_cli_artifact_metadata_invalid");
}

TEST_CASE("WordPressCliService rejects a symlink Phar") {
    ArtifactFixture fixture;
    fixture.write("stable-phar-content");
    std::filesystem::remove(fixture.directory / "wp-cli.phar");
    REQUIRE(::symlink("/etc/hosts", (fixture.directory / "wp-cli.phar").c_str()) == 0);

    auto& config = containercp::config::Config::instance();
    containercp::site::SiteManager sites;
    containercp::wordpress::WordPressConfigService config_service(sites);
    containercp::runtime::CommandExecutor executor;
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, sites, config_service, config, containercp::logger::Logger::instance());
    auto service = make_service(executor, resolver, config);

    const auto artifact = service.validate_artifact();
    CHECK_FALSE(artifact.ok);
    CHECK(artifact.failure_code == "wordpress_cli_artifact_untrusted");
}

TEST_CASE("WordPressCliService real disposable WordPress lifecycle") {
    const char* enabled = std::getenv("CONTAINERCP_WPCLI_INTEGRATION");
    if (enabled == nullptr || std::string(enabled) != "1") {
        MESSAGE("Set CONTAINERCP_WPCLI_INTEGRATION=1 to run the real Docker integration gate");
        return;
    }
    const char* source_env = std::getenv("CONTAINERCP_WPCLI_WORDPRESS_ROOT");
    REQUIRE(source_env != nullptr);

    constexpr const char* domain = "wpcli-integration.local";
    auto& config = containercp::config::Config::instance();
    const std::filesystem::path site_dir = config.sites_dir() + domain + "/";
    const std::filesystem::path public_dir = site_dir / "public";
    const std::filesystem::path compose_file = site_dir / "docker-compose.yml";
    const std::filesystem::path artifact = config.data_root() + "/wp-cli/wp-cli.phar";
    std::filesystem::remove_all(site_dir);
    std::filesystem::create_directories(public_dir);
    for (const auto& entry : std::filesystem::directory_iterator(source_env)) {
        std::filesystem::copy(entry.path(), public_dir / entry.path().filename(),
                               std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    }
    REQUIRE(std::filesystem::exists(artifact));

    containercp::runtime::CommandExecutor executor;

    struct Cleanup {
        containercp::runtime::CommandExecutor& executor;
        std::filesystem::path compose;
        std::filesystem::path site;
        ~Cleanup() {
            executor.run({"/usr/bin/docker", "compose", "-f", compose.string(), "down"});
            std::filesystem::remove_all(site);
        }
    } cleanup{executor, compose_file, site_dir};

    std::ofstream(compose_file)
        << "services:\n"
        << "  php:\n"
        << "    image: containercp-wpcli-php:8.4\n"
        << "    volumes:\n"
        << "      - ./public:/var/www/html\n"
        << "    command: [\"php-fpm\", \"-F\"]\n"
        << "    labels:\n"
        << "      containercp.site.id: \"1\"\n"
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
        << "networks:\n"
        << "  private:\n";
    std::ofstream(public_dir / "wp-config.php")
        << "<?php\n"
        << "define('DB_NAME', 'wpcli');\n"
        << "define('DB_USER', 'wpcli');\n"
        << "define('DB_PASSWORD', 'wpcli-password');\n"
        << "define('DB_HOST', 'db');\n"
        << "define('DB_CHARSET', 'utf8mb4');\n"
        << "define('DB_COLLATE', '');\n"
        << "$table_prefix = 'wp_';\n"
        << "define('WP_DEBUG', false);\n"
        << "if (!defined('ABSPATH')) define('ABSPATH', __DIR__ . '/');\n"
        << "require_once ABSPATH . 'wp-settings.php';\n";

    auto compose = [&](const std::vector<std::string>& args) {
        std::vector<std::string> command{"/usr/bin/docker", "compose", "-f", compose_file.string()};
        command.insert(command.end(), args.begin(), args.end());
        return executor.run(command);
    };
    auto up = compose({"up", "-d"});
    REQUIRE(up.exit_code == 0);
    bool database_ready = false;
    for (int attempt = 0; attempt < 60 && !database_ready; ++attempt) {
        const auto ping = compose({"exec", "-T", "db", "mariadb", "-uwpcli", "-pwpcli-password",
                                   "-e", "SELECT 1", "wpcli"});
        database_ready = ping.exit_code == 0;
        if (!database_ready) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    REQUIRE(database_ready);

    const auto install = compose({"run", "--rm", "-T", "-v",
                                  artifact.string() + ":/tmp/wp-cli.phar:ro", "php", "php", "/tmp/wp-cli.phar",
                                  "core", "install", "--url=http://wpcli-integration.local",
                                  "--title=ContainerCP WP-CLI Integration", "--admin_user=admin",
                                  "--admin_password=admin-password", "--admin_email=admin@example.com",
                                  "--skip-email", "--allow-root"});
    CAPTURE(install.exit_code);
    CAPTURE(install.out);
    CAPTURE(install.err);
    REQUIRE(install.exit_code == 0);

    containercp::site::SiteManager sites;
    const auto site_id = sites.create(domain, "admin", 1, "nginx");
    containercp::wordpress::WordPressConfigService config_service(sites);
    containercp::wordpress::WordPressRuntimeContextResolver resolver(
        executor, sites, config_service, config, containercp::logger::Logger::instance());
    containercp::wordpress::WordPressCliService service(
        executor, resolver, config, containercp::logger::Logger::instance());
    const auto context = resolver.resolve(site_id);
    REQUIRE(context.ok);
    CHECK(context.immutable_php_image_id.rfind("sha256:", 0) == 0);
    CHECK(context.private_network.find("_private") != std::string::npos);
    CHECK(context.container_document_root == "/var/www/html");
    CHECK(context.php_fpm_uid > 0);
    CHECK(context.php_fpm_gid > 0);
    CHECK(context.document_root_mount_read_write);
    CHECK(context.mutation_capable);

    const auto installed = service.run(site_id, containercp::wordpress::WordPressCliOperation::CoreIsInstalled);
    CAPTURE(installed.failure_code);
    CAPTURE(installed.message);
    CAPTURE(installed.diagnostic);
    REQUIRE(installed.success);
    CHECK(installed.cleanup_succeeded);
    const auto version = service.run(site_id, containercp::wordpress::WordPressCliOperation::CoreVersion);
    REQUIRE(version.success);
    CHECK_FALSE(version.output.empty());
    const auto plugins = service.run(site_id, containercp::wordpress::WordPressCliOperation::PluginList);
    REQUIRE(plugins.success);
    CHECK_FALSE(plugins.output.empty());
    const auto themes = service.run(site_id, containercp::wordpress::WordPressCliOperation::ThemeList);
    REQUIRE(themes.success);
    CHECK_FALSE(themes.output.empty());

    const auto plugin_source = public_dir / "wpcli-test-plugin";
    std::filesystem::create_directories(plugin_source);
    std::ofstream(plugin_source / "wpcli-test-plugin.php")
        << "<?php\n/* Plugin Name: ContainerCP WP-CLI Test Plugin */\n";
    const auto plugin_archive = public_dir / "wpcli-test-plugin.zip";
    REQUIRE(executor.run({"/usr/bin/zip", "-qr", plugin_archive.string(), plugin_source.filename().string()}, public_dir.string()).exit_code == 0);
    REQUIRE(executor.run({"/usr/bin/chown", "-R",
                          std::to_string(context.php_fpm_uid) + ":" + std::to_string(context.php_fpm_gid),
                          (public_dir / "wp-content").string()}).exit_code == 0);
    const auto plugin_install = service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::PluginInstall, "wpcli-test-plugin.zip");
    CAPTURE(plugin_install.failure_code);
    CAPTURE(plugin_install.message);
    CAPTURE(plugin_install.output);
    CAPTURE(plugin_install.diagnostic);
    CAPTURE(plugin_install.exit_code);
    REQUIRE(plugin_install.success);
    CHECK(plugin_install.cleanup_succeeded);
    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::PluginActivate, "wpcli-test-plugin").success);
    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::PluginDeactivate, "wpcli-test-plugin").success);
    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::PluginUpdate, "wpcli-test-plugin").success);
    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::PluginDelete, "wpcli-test-plugin").success);

    const auto theme_source = public_dir / "wpcli-test-theme";
    std::filesystem::create_directories(theme_source);
    std::ofstream(theme_source / "style.css")
        << "/*\nTheme Name: ContainerCP WP-CLI Test Theme\n*/\n";
    std::ofstream(theme_source / "index.php") << "<?php\n";
    const auto theme_archive = public_dir / "wpcli-test-theme.zip";
    REQUIRE(executor.run({"/usr/bin/zip", "-qr", theme_archive.string(), theme_source.filename().string()}, public_dir.string()).exit_code == 0);
    const auto theme_install = service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::ThemeInstall, "wpcli-test-theme.zip");
    CAPTURE(theme_install.failure_code);
    REQUIRE(theme_install.success);
    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::ThemeActivate, "wpcli-test-theme").success);
    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::ThemeUpdate, "wpcli-test-theme").success);
    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::ThemeActivate, "twentytwentyfive").success);
    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::ThemeDelete, "wpcli-test-theme").success);

    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::CoreUpdate).success);
    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::LanguageInstall, "en_US").success);
    const auto language_update = service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::LanguageUpdate);
    CAPTURE(language_update.failure_code);
    CAPTURE(language_update.message);
    CAPTURE(language_update.output);
    CAPTURE(language_update.diagnostic);
    CHECK(language_update.success);
    CHECK(service.run_mutation(site_id, containercp::wordpress::WordPressCliMutation::CacheFlush).success);

    const auto timeout_docker = std::filesystem::path("/tmp/containercp-wpcli-timeout-docker.sh");
    struct TimeoutStubCleanup {
        std::filesystem::path path;
        ~TimeoutStubCleanup() { std::filesystem::remove(path); }
    } timeout_stub_cleanup{timeout_docker};
    std::ofstream(timeout_docker)
        << "#!/bin/sh\n"
        << "if [ \"$1\" = \"run\" ]; then sleep 3; exit 124; fi\n"
        << "if [ \"$1\" = \"rm\" ]; then exit 0; fi\n"
        << "if [ \"$1\" = \"inspect\" ]; then exit 1; fi\n"
        << "exit 1\n";
    REQUIRE(::chmod(timeout_docker.c_str(), 0555) == 0);
    containercp::wordpress::WordPressCliService timeout_service(
        executor, resolver, config, containercp::logger::Logger::instance(), timeout_docker.string(), 1, 5);
    const auto timed_out = timeout_service.run(site_id, containercp::wordpress::WordPressCliOperation::CoreVersion);
    CHECK_FALSE(timed_out.success);
    CHECK(timed_out.failure_code == "wordpress_cli_timeout");
    CHECK(timed_out.cleanup_succeeded);

    for (const auto& entry : std::filesystem::recursive_directory_iterator(public_dir / "wp-content")) {
        struct stat metadata{};
        if (std::filesystem::is_regular_file(entry.symlink_status()) &&
            ::lstat(entry.path().c_str(), &metadata) == 0) {
            CHECK(metadata.st_uid != 0);
        }
    }

    std::filesystem::create_directories(public_dir / "wp-content/plugins");
    std::ofstream(public_dir / "wp-content/plugins/broken.php")
        << "<?php throw new RuntimeException('broken disposable plugin');\n";
    const auto plugins_with_broken = service.run(site_id, containercp::wordpress::WordPressCliOperation::PluginList);
    REQUIRE(plugins_with_broken.success);
    CHECK_FALSE(plugins_with_broken.output.empty());

    REQUIRE(compose({"stop", "db"}).exit_code == 0);
    const auto failed = service.run(site_id, containercp::wordpress::WordPressCliOperation::PluginList);
    CHECK_FALSE(failed.success);
    CHECK(failed.cleanup_succeeded);
    const auto runners = executor.run({"/usr/bin/docker", "ps", "-a", "--filter",
                                       "label=containercp.wpcli.managed=true", "--format", "{{.ID}}"});
    CHECK(runners.exit_code == 0);
    CHECK(runners.out.empty());
}
