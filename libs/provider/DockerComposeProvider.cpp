#include "DockerComposeProvider.h"
#include "docker/EnvGenerator.h"
#include "filesystem/SiteLayout.h"
#include "template/TemplateEngine.h"

namespace containercp::provider {

DockerComposeProvider::DockerComposeProvider(filesystem::Filesystem& fs, config::Config& cfg,
                                             php::PhpVersionManager& php, runtime::Runtime& rt,
                                             profile::ProfileManager& prof)
    : fs_(fs)
    , cfg_(cfg)
    , php_(php)
    , rt_(rt)
    , prof_(prof)
{
}

core::OperationResult DockerComposeProvider::create_site(site::Site& site, uint16_t nginx_port) {
    auto check = rt_.check_compose();
    if (!check.success) {
        return check;
    }

    std::string site_dir = cfg_.sites_dir() + site.domain + "/";

    filesystem::SiteLayout layout(fs_, site_dir);
    layout.create();

    docker::EnvGenerator env(fs_, site_dir);
    if (site.db_name.empty()) {
        env.generate(site.domain, site.owner, nginx_port);
    } else {
        env.generate(site.domain, site.owner, site.db_name, site.db_user, site.db_password, nginx_port);
    }

    auto* php_version = php_.get_default();
    std::string php_image = php_version ? php_version->image : "php:8.4-fpm";

    docker::ComposeGenerator gen(fs_, cfg_.templates_dir());
    gen.generate(site.domain, site.owner, php_image, site_dir + "docker-compose.yml");

    // Generate web server config from default WEB_SERVER profile
    std::string nginx_config_path = site_dir + "config/nginx/default.conf";
    {
        auto* profile = prof_.get_default(profile::ProfileType::WEB_SERVER);
        bool config_generated = false;
        if (profile != nullptr && fs_.exists(profile->template_path)) {
            std::string template_content = fs_.read_file(profile->template_path);
            template_engine::TemplateEngine engine;
            std::string rendered = engine.render_web(template_content, site.domain,
                "/var/www/html", "php:9000", "/var/log", false);
            std::string config_dir = site_dir + "config/" + profile->web_server + "/";
            fs_.create_directory(config_dir);
            fs_.create_file(config_dir + "default.conf", rendered);
            config_generated = true;
        }

        // Fallback: generate a basic nginx config if profile/template was missing
        if (!config_generated || !fs_.exists(nginx_config_path)) {
            std::string fallback =
                "server {\n"
                "    listen 80;\n"
                "    server_name " + site.domain + ";\n"
                "    root /var/www/html;\n"
                "    index index.php index.html;\n"
                "\n"
                "    location / {\n"
                "        try_files $uri $uri/ /index.php?$query_string;\n"
                "    }\n"
                "\n"
                "    location ~ \\.php$ {\n"
                "        fastcgi_pass php:9000;\n"
                "        fastcgi_index index.php;\n"
                "        fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;\n"
                "        include fastcgi_params;\n"
                "    }\n"
                "}\n";
            fs_.create_directory(site_dir + "config/nginx/");
            fs_.create_file(nginx_config_path, fallback);
        }
    }

    // Validate that the nginx config file was actually written
    if (!fs_.exists(nginx_config_path)) {
        return {false, "Failed to generate nginx config at " + nginx_config_path};
    }

    fs_.create_file(site_dir + "public/index.php", "<?php\nphpinfo();\n");

    return rt_.create_site_stack(site.domain);
}

core::OperationResult DockerComposeProvider::remove_site(site::Site& site) {
    return rt_.remove_site(site.domain);
}

core::OperationResult DockerComposeProvider::start_site(site::Site& site) {
    return rt_.start_site(site.domain);
}

core::OperationResult DockerComposeProvider::stop_site(site::Site& site) {
    return rt_.stop_site(site.domain);
}

core::OperationResult DockerComposeProvider::status(site::Site& site) {
    return rt_.status(site.domain);
}

} // namespace containercp::provider
