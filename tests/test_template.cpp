#include "template/TemplateProfileManager.h"
#include "template/TemplateEngine.h"
#include "template/web_templates.h"

#include <string>

#include "doctest/doctest.h"

TEST_CASE("TemplateProfileManager create/find/list") {
    containercp::template_engine::TemplateProfileManager mgr;

    uint64_t id = mgr.create("nginx-php-default", "nginx", "/path/tmpl", "Default PHP", true);
    CHECK(id == 1);

    auto* p = mgr.find("nginx-php-default");
    REQUIRE(p != nullptr);
    CHECK(p->web_server == "nginx");
    CHECK(p->default_profile);
    CHECK(p->enabled);

    CHECK(mgr.list().size() == 1);
}

TEST_CASE("TemplateProfileManager get_default") {
    containercp::template_engine::TemplateProfileManager mgr;
    mgr.create("nginx-php-default", "nginx", "/a", "desc", true);
    mgr.create("apache-php-default", "apache", "/b", "desc", false);

    auto* def = mgr.get_default();
    REQUIRE(def != nullptr);
    CHECK(def->profile_name == "nginx-php-default");
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

TEST_CASE("Default web templates exist") {
    auto tmpl = containercp::template_engine::default_web_templates();
    CHECK(tmpl.find("nginx-php-default") != tmpl.end());
    CHECK(tmpl.find("nginx-wordpress") != tmpl.end());
    CHECK(tmpl.find("nginx-laravel") != tmpl.end());
    CHECK(tmpl.find("apache-php-default") != tmpl.end());
    CHECK(tmpl.find("apache-wordpress") != tmpl.end());
    CHECK(tmpl.size() == 5);
}
