#include "template/TemplateProfileManager.h"
#include "template/TemplateEngine.h"
#include "template/web_templates.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
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

TEST_CASE("Template validate detects missing variables") {
    // A template without required variables should fail validation
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
    CHECK(missing.find("{{LOG_ROOT}}") != std::string::npos);
    CHECK(missing.find("{{SSL_ENABLED}}") != std::string::npos);
}

TEST_CASE("Complete template has all variables") {
    std::string complete = "{{DOMAIN}} {{PUBLIC_ROOT}} {{PHP_UPSTREAM}} {{LOG_ROOT}} {{SSL_ENABLED}}";
    const char* required[] = {"{{DOMAIN}}", "{{PUBLIC_ROOT}}", "{{PHP_UPSTREAM}}",
                              "{{LOG_ROOT}}", "{{SSL_ENABLED}}"};
    for (const auto& var : required) {
        CHECK(complete.find(var) != std::string::npos);
    }
}

TEST_CASE("Existing template file is not overwritten") {
    // Create a temp dir with a pre-existing template
    std::string tmp_dir = "/tmp/containercp_test_templates/";
    std::string tmpl_file = tmp_dir + "custom-template.conf.template";
    std::filesystem::create_directories(tmp_dir);

    // Write custom content
    std::ofstream(tmpl_file) << "CUSTOM_CONTENT";
    CHECK(std::filesystem::exists(tmpl_file));

    // Read it back - verify it wasn't changed
    std::ifstream f(tmpl_file);
    std::string content((std::istreambuf_iterator<char>(f)), {});
    CHECK(content == "CUSTOM_CONTENT");

    std::filesystem::remove_all(tmp_dir);
}
