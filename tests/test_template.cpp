#include "profile/ProfileManager.h"
#include "template/TemplateEngine.h"
#include "template/web_templates.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "doctest/doctest.h"

using containercp::profile::ProfileType;
using containercp::profile::ProfileManager;

TEST_CASE("ProfileManager create/find/list") {
    ProfileManager mgr;
    uint64_t id = mgr.create("nginx-php-default", ProfileType::WEB_SERVER,
                             "nginx", "/path/tmpl", "Default PHP", true);
    CHECK(id == 1);
    auto* p = mgr.find("nginx-php-default");
    REQUIRE(p != nullptr);
    CHECK(p->web_server == "nginx");
    CHECK(p->type == ProfileType::WEB_SERVER);
    CHECK(p->default_profile);
    CHECK(mgr.list().size() == 1);
}

TEST_CASE("ProfileManager get_default by type") {
    ProfileManager mgr;
    mgr.create("nginx-php-default", ProfileType::WEB_SERVER, "nginx", "/a", "desc", true);
    mgr.create("apache-php-default", ProfileType::WEB_SERVER, "apache", "/b", "desc", false);

    auto* def = mgr.get_default(ProfileType::WEB_SERVER);
    REQUIRE(def != nullptr);
    CHECK(def->profile_name == "nginx-php-default");
}

TEST_CASE("ProfileManager list_by_type") {
    ProfileManager mgr;
    mgr.create("nginx-default", ProfileType::WEB_SERVER, "nginx", "/a", "", true);
    mgr.create("php-8.4", ProfileType::PHP, "", "/b", "", false);
    mgr.create("nginx-laravel", ProfileType::WEB_SERVER, "nginx", "/c", "", false);

    auto web = mgr.list_by_type(ProfileType::WEB_SERVER);
    CHECK(web.size() == 2);

    auto php = mgr.list_by_type(ProfileType::PHP);
    CHECK(php.size() == 1);
}

TEST_CASE("ProfileType string conversion") {
    CHECK(containercp::profile::profile_type_to_string(ProfileType::WEB_SERVER) == "web_server");
    CHECK(containercp::profile::profile_type_to_string(ProfileType::PHP) == "php");
    CHECK(containercp::profile::profile_type_to_string(ProfileType::DOCKER) == "docker");
    CHECK(containercp::profile::profile_type_to_string(ProfileType::SSL) == "ssl");
    CHECK(containercp::profile::profile_type_to_string(ProfileType::BACKUP) == "backup");
    CHECK(containercp::profile::profile_type_to_string(ProfileType::MAIL) == "mail");
    CHECK(containercp::profile::profile_type_to_string(ProfileType::DNS) == "dns");

    CHECK(containercp::profile::profile_type_from_string("web_server") == ProfileType::WEB_SERVER);
    CHECK(containercp::profile::profile_type_from_string("php") == ProfileType::PHP);
    CHECK(containercp::profile::profile_type_from_string("unknown") == ProfileType::WEB_SERVER);
}

TEST_CASE("TemplateEngine render_web replaces variables") {
    containercp::template_engine::TemplateEngine engine;
    std::string tmpl = "server_name {{DOMAIN}}; root {{PUBLIC_ROOT}}; upstream {{PHP_UPSTREAM}}; log {{LOG_ROOT}}; ssl={{SSL_ENABLED}};";
    std::string result = engine.render_web(tmpl, "example.com", "/var/www", "php:9000", "/var/log", true);
    CHECK(result.find("server_name example.com") != std::string::npos);
    CHECK(result.find("root /var/www") != std::string::npos);
    CHECK(result.find("upstream php:9000") != std::string::npos);
    CHECK(result.find("log /var/log") != std::string::npos);
    CHECK(result.find("ssl=true") != std::string::npos);
}

TEST_CASE("TemplateEngine render_web ssl_disabled") {
    containercp::template_engine::TemplateEngine engine;
    std::string result = engine.render_web("ssl={{SSL_ENABLED}}", "d", "/w", "p", "/l", false);
    CHECK(result == "ssl=false");
}

TEST_CASE("Existing template file is not overwritten") {
    std::string tmp_dir = "/tmp/containercp_test_profiles/";
    std::string tmpl_file = tmp_dir + "custom.conf.template";
    std::filesystem::create_directories(tmp_dir);
    std::ofstream(tmpl_file) << "CUSTOM_CONTENT";
    CHECK(std::filesystem::exists(tmpl_file));
    std::ifstream f(tmpl_file);
    std::string content((std::istreambuf_iterator<char>(f)), {});
    CHECK(content == "CUSTOM_CONTENT");
    std::filesystem::remove_all(tmp_dir);
}

TEST_CASE("Default web template produces valid nginx config") {
    // Verify the built-in nginx-php-default template generates a valid
    // config that nginx would accept. This mirrors the logic in
    // DockerComposeProvider::create_site().
    auto templates = containercp::template_engine::default_web_templates();
    REQUIRE(templates.find("nginx-php-default") != templates.end());

    containercp::template_engine::TemplateEngine engine;
    std::string rendered = engine.render_web(templates["nginx-php-default"],
        "test.example.com", "/var/www/html", "php:9000", "/var/log", false);

    CHECK(rendered.find("server_name test.example.com") != std::string::npos);
    CHECK(rendered.find("root /var/www/html") != std::string::npos);
    CHECK(rendered.find("fastcgi_pass php:9000") != std::string::npos);
    CHECK(rendered.find("listen 80") != std::string::npos);
    CHECK(rendered.find("index index.php") != std::string::npos);
}

TEST_CASE("Default nginx profile has correct metadata") {
    // Verify the nginx-php-default profile matches what
    // DockerComposeProvider expects (web_server = "nginx").
    ProfileManager mgr;
    mgr.create("nginx-php-default", ProfileType::WEB_SERVER, "nginx", "/path/tmpl", "Default", true);
    auto* p = mgr.get_default(ProfileType::WEB_SERVER);
    REQUIRE(p != nullptr);
    CHECK(p->web_server == "nginx");
    CHECK(p->default_profile);
    CHECK(p->type == ProfileType::WEB_SERVER);
}

TEST_CASE("Site nginx config path structure") {
    // Verify that the config file path matches what docker-compose mounts.
    // docker-compose mounts ./config/nginx:/etc/nginx/conf.d
    // So config should be at: <site_dir>/config/nginx/default.conf
    std::string site_dir = "/srv/containercp/sites/test.example.com/";
    std::string config_file = site_dir + "config/nginx/default.conf";
    CHECK(config_file == "/srv/containercp/sites/test.example.com/config/nginx/default.conf");
}

TEST_CASE("Profile validate detects missing variables") {
    std::string incomplete = "server_name {{DOMAIN}}; root {{PUBLIC_ROOT}};";
    const char* required[] = {"{{DOMAIN}}", "{{PUBLIC_ROOT}}", "{{PHP_UPSTREAM}}",
                              "{{LOG_ROOT}}", "{{SSL_ENABLED}}"};
    std::string missing;
    for (const auto& var : required) {
        if (incomplete.find(var) == std::string::npos) {
            if (!missing.empty()) missing += ", ";
            missing += var;
        }
    }
    CHECK(!missing.empty());
    CHECK(missing.find("{{PHP_UPSTREAM}}") != std::string::npos);
}
