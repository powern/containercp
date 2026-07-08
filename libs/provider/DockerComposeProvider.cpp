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

core::OperationResult DockerComposeProvider::create_site(site::Site& site) {
    auto check = rt_.check_compose();
    if (!check.success) {
        return check;
    }

    std::string site_dir = cfg_.sites_dir() + site.domain + "/";

    filesystem::SiteLayout layout(fs_, site_dir);
    layout.create();

    docker::EnvGenerator env(fs_, site_dir);
    if (site.db_name.empty()) {
        env.generate(site.domain, site.owner);
    } else {
        env.generate(site.domain, site.owner, site.db_name, site.db_user, site.db_password);
    }

    auto* php_version = php_.get_default();
    std::string php_image = php_version ? php_version->image : "php:8.4-fpm";

    std::string site_id = std::to_string(site.id);

    // Determine web server image and paths from selected profile
    auto* profile = prof_.get_default(profile::ProfileType::WEB_SERVER);
    std::string web_server_type = (profile != nullptr) ? profile->web_server : "nginx";
    std::string web_server_image = "nginx:alpine";
    std::string web_config_dir = "/etc/nginx/conf.d";
    std::string web_log_dir = "/var/log/nginx";
    std::string web_doc_root = "/var/www/html";
    std::string web_local_config = "config/nginx";
    std::string web_local_log = "logs/nginx";
    if (web_server_type == "apache") {
        web_server_image = "httpd:alpine";
        web_config_dir = "/usr/local/apache2/conf/extra";
        web_log_dir = "/usr/local/apache2/logs";
        web_doc_root = "/usr/local/apache2/htdocs";
        web_local_config = "config/apache";
        web_local_log = "logs/apache";
    }

    docker::ComposeGenerator gen(fs_, cfg_.templates_dir());
    gen.generate(site.domain, site.owner, php_image, site_dir + "docker-compose.yml",
                 site_id, web_server_image, web_config_dir, web_log_dir, web_doc_root,
                 web_local_config, web_local_log);

    // Generate web server config from default WEB_SERVER profile
    std::string config_dir = site_dir + "config/" + web_server_type + "/";
    std::string config_path = config_dir + "default.conf";
    fs_.create_directory(config_dir);

    bool config_generated = false;
    if (profile != nullptr && fs_.exists(profile->template_path)) {
        std::string template_content = fs_.read_file(profile->template_path);
        template_engine::TemplateEngine engine;
        std::string rendered = engine.render_web(template_content, site.domain,
            web_doc_root, "php:9000", web_log_dir, false);
        fs_.create_file(config_path, rendered);
        config_generated = true;
    }

    // Fallback: generate a basic web server config if no profile template
    if (!config_generated || !fs_.exists(config_path)) {
        std::string fallback;
        if (web_server_type == "apache") {
            fallback =
                "<VirtualHost *:80>\n"
                "    ServerName " + site.domain + "\n"
                "    DocumentRoot " + web_doc_root + "\n"
                "    <Directory " + web_doc_root + ">\n"
                "        Options Indexes FollowSymLinks\n"
                "        AllowOverride All\n"
                "        Require all granted\n"
                "    </Directory>\n"
                "    <FilesMatch \\.php$>\n"
                "        SetHandler \"proxy:fcgi://php:9000\"\n"
                "    </FilesMatch>\n"
                "    ErrorLog " + web_log_dir + "/error.log\n"
                "    CustomLog " + web_log_dir + "/access.log combined\n"
                "</VirtualHost>\n";
        } else {
            fallback =
                "server {\n"
                "    listen 80;\n"
                "    server_name " + site.domain + ";\n"
                "    root " + web_doc_root + ";\n"
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
        }
        fs_.create_file(config_path, fallback);
    }

    // Validate that the web server config file was actually written
    if (!fs_.exists(config_path)) {
        return {false, "Failed to generate web server config at " + config_path};
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
