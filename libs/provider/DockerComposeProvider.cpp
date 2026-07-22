#include "DockerComposeProvider.h"
#include "docker/EnvGenerator.h"
#include "filesystem/SiteLayout.h"
#include "runtime/CommandExecutor.h"
#include "template/TemplateEngine.h"

namespace containercp::provider {

static profile::Profile* select_web_profile(profile::ProfileManager& profiles,
                                            const std::string& web_server_type) {
    profile::Profile* fallback = nullptr;
    auto web_profiles = profiles.list_by_type(profile::ProfileType::WEB_SERVER);
    for (auto* candidate : web_profiles) {
        if (candidate == nullptr || candidate->web_server != web_server_type) continue;
        if (candidate->default_profile) return candidate;
        if (fallback == nullptr) fallback = candidate;
    }
    return fallback;
}

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

core::OperationResult DockerComposeProvider::create_site(site::Site& site, core::ProgressCallback progress) {
    progress(5, "Checking Docker Compose...");
    auto check = rt_.check_compose();
    if (!check.success) {
        return check;
    }

    std::string site_dir = cfg_.sites_dir() + site.domain + "/";

    progress(10, "Creating site directories...");
    filesystem::SiteLayout layout(fs_, site_dir);
    layout.create();

    progress(15, "Generating environment configuration...");
    docker::EnvGenerator env(fs_, site_dir);
    if (site.db_name.empty()) {
        env.generate(site.domain, site.owner);
    } else {
        env.generate(site.domain, site.owner, site.db_name, site.db_user, site.db_password);
    }

    fs_.create_directory(site_dir + "config/mariadb/initdb/");
    fs_.create_file(site_dir + "config/mariadb/initdb/10-containercp-service-account.sh",
        "#!/bin/sh\n"
        "set -eu\n"
        "if [ -z \"${CONTAINERCP_DB_SERVICE_USER:-}\" ] || [ -z \"${CONTAINERCP_DB_SERVICE_PASSWORD:-}\" ]; then\n"
        "  echo \"ContainerCP MariaDB service account environment is missing\" >&2\n"
        "  exit 1\n"
        "fi\n"
        "mariadb -uroot -p\"$MYSQL_ROOT_PASSWORD\" <<SQL\n"
        "CREATE USER IF NOT EXISTS '$CONTAINERCP_DB_SERVICE_USER'@'%' IDENTIFIED BY '$CONTAINERCP_DB_SERVICE_PASSWORD';\n"
        "GRANT CREATE ON *.* TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        "GRANT CREATE USER ON *.* TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        "GRANT SELECT ON mysql.user TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        "GRANT SELECT ON mysql.db TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        "GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, INDEX, ALTER, CREATE TEMPORARY TABLES, LOCK TABLES, GRANT OPTION ON \\`$MYSQL_DATABASE\\`.* TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        "FLUSH PRIVILEGES;\n"
        "SQL\n");

    auto* php_version = php_.get_default();
    std::string php_image = php_version ? php_version->image : "ghcr.io/powern/containercp-php:8.4";

    std::string site_id = std::to_string(site.id);

    progress(25, "Determining web server configuration...");
    std::string web_server_type = site.web_server.empty() ? "apache" : site.web_server;
    auto* profile = select_web_profile(prof_, web_server_type);
    if (!site.template_profile.empty()) {
        auto* requested = prof_.find(site.template_profile);
        if (requested != nullptr && requested->type == profile::ProfileType::WEB_SERVER
            && requested->web_server == web_server_type) {
            profile = requested;
        } else {
            return {false, "Template profile not found or backend mismatch: " + site.template_profile};
        }
    }
    if (profile != nullptr) {
        site.web_template_profile = profile->profile_name;
    }
    std::string web_server_image = "nginx:alpine";
    std::string web_config_dir = "/etc/nginx/conf.d";
    std::string web_log_dir = "/var/log/nginx";
    std::string web_doc_root = "/var/www/html";
    std::string web_local_config = "config/nginx";
    std::string web_local_log = "logs/nginx";
    std::string web_server_cmd = "";
    if (web_server_type == "apache") {
        web_server_image = "httpd:alpine";
        web_config_dir = "/usr/local/apache2/conf/extra";
        web_log_dir = "/usr/local/apache2/logs";
        web_doc_root = "/usr/local/apache2/htdocs";
        web_local_config = "config/apache";
        web_local_log = "logs/apache";
        // httpd:alpine does not include conf/extra/* by default — inject the directive
        web_server_cmd = "[\"httpd-foreground\", \"-c\", \"IncludeOptional conf/extra/*.conf\"]";
    }

    progress(30, "Generating Docker Compose configuration...");
    docker::ComposeGenerator gen(fs_, cfg_.templates_dir());
    gen.generate(site.domain, site.owner, php_image, site_dir + "docker-compose.yml",
                 site_id, web_server_image, web_config_dir, web_log_dir, web_doc_root,
                 web_local_config, web_local_log, web_server_cmd);

    progress(40, "Generating web server configuration...");
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
                "    DirectoryIndex index.php index.html\n"
                "    <Directory " + web_doc_root + ">\n"
                "        Options FollowSymLinks\n"
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

    // For Apache httpd:alpine, enable required modules (mod_proxy, mod_proxy_fcgi)
    // by writing a module-load config that gets included via IncludeOptional.
    if (web_server_type == "apache") {
        std::string modules_path = site_dir + "config/apache/00-load-modules.conf";
        if (!fs_.exists(modules_path)) {
            std::string modules =
                "# Enable proxy, remoteip, and rewrite modules for PHP-FPM + real IP + WordPress permalinks\n"
                "LoadModule proxy_module modules/mod_proxy.so\n"
                "LoadModule proxy_fcgi_module modules/mod_proxy_fcgi.so\n"
                "LoadModule remoteip_module modules/mod_remoteip.so\n"
                "LoadModule rewrite_module modules/mod_rewrite.so\n";
            fs_.create_file(modules_path, modules);
        }
    }

    // Validate that the web server config file was actually written
    if (!fs_.exists(config_path)) {
        return {false, "Failed to generate web server config at " + config_path};
    }

    progress(55, "Creating mail configuration directory...");
    fs_.create_directory(site_dir + "config/php/");

    progress(60, "Creating default index file...");
    fs_.create_file(site_dir + "public/index.php", "<?php\nphpinfo();\n");

    progress(70, "Starting Docker stack...");
    auto result = rt_.create_site_stack(site.domain);

    if (result.success) {
        progress(100, "Deployment completed.");
    }
    return result;
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

core::OperationResult DockerComposeProvider::apply_web_template(site::Site& site, const std::string& template_path) {
    std::string site_dir = cfg_.sites_dir() + site.domain + "/";
    std::string web_server_type = site.web_server.empty() ? "apache" : site.web_server;
    std::string config_dir = site_dir + "config/" + web_server_type + "/";
    std::string config_path = config_dir + "default.conf";

    if (!fs_.exists(template_path)) {
        return {false, "Template file not found: " + template_path};
    }
    if (!fs_.exists(config_dir)) {
        return {false, "Web server config directory not found for " + site.domain};
    }

    std::string template_content = fs_.read_file(template_path);
    template_engine::TemplateEngine engine;

    std::string doc_root = (web_server_type == "apache")
        ? "/usr/local/apache2/htdocs"
        : "/var/www/html";
    std::string log_dir = (web_server_type == "apache")
        ? "/usr/local/apache2/logs"
        : "/var/log/nginx";

    std::string rendered = engine.render_web(template_content, site.domain,
        doc_root, "php:9000", log_dir, false);

    fs_.create_file(config_path, rendered);

    runtime::CommandExecutor exec;
    auto restart = exec.run({"docker", "compose", "-f", site_dir + "docker-compose.yml", "restart", "web"});
    if (restart.exit_code != 0) {
        return {false, "Template applied but web container restart failed: " + restart.err};
    }

    return {true, "Template applied and web container restarted for " + site.domain};
}

} // namespace containercp::provider
