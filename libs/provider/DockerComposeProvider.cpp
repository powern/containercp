#include "DockerComposeProvider.h"
#include "docker/EnvGenerator.h"
#include "filesystem/SiteLayout.h"

namespace containercp::provider {

DockerComposeProvider::DockerComposeProvider(filesystem::Filesystem& fs, config::Config& cfg, runtime::Runtime& rt)
    : fs_(fs)
    , cfg_(cfg)
    , rt_(rt)
{
}

core::OperationResult DockerComposeProvider::create_site(site::Site& site) {
    std::string site_dir = cfg_.sites_dir() + site.domain + "/";

    filesystem::SiteLayout layout(fs_, site_dir);
    layout.create();

    docker::EnvGenerator env(fs_, site_dir);
    env.generate(site.domain, site.owner);

    docker::ComposeGenerator gen(fs_, cfg_.templates_dir());
    gen.generate(site.domain, site.owner, site_dir + "docker-compose.yml");

    std::string nginx_cfg =
        "server {\n"
        "    listen 80;\n"
        "    server_name _;\n"
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
        "\n"
        "    location ~ /\\.ht {\n"
        "        deny all;\n"
        "    }\n"
        "}\n";

    fs_.create_file(site_dir + "config/nginx/default.conf", nginx_cfg);
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
