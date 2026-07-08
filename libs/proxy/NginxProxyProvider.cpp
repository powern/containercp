#include "NginxProxyProvider.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <sys/wait.h>

namespace containercp::proxy {

NginxProxyProvider::NginxProxyProvider(filesystem::Filesystem& fs, config::Config& cfg,
                                       logger::Logger& logger, ssl::SslCertificateManager& ssl_mgr)
    : fs_(fs)
    , cfg_(cfg)
    , logger_(logger)
    , ssl_mgr_(ssl_mgr)
{
}

std::string NginxProxyProvider::config_path(const std::string& domain) const {
    return cfg_.data_root() + "/proxy/sites/" + domain + ".conf";
}

std::string NginxProxyProvider::proxy_name() const {
    return "containercp-proxy";
}

bool NginxProxyProvider::central_proxy_running() const {
    std::string cmd = "docker inspect " + proxy_name() + " > /dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc == -1) return false;
    int exit_code = WEXITSTATUS(rc);
    return exit_code == 0;
}

core::OperationResult NginxProxyProvider::create_proxy(const ReverseProxy& proxy) {
    std::string path = config_path(proxy.domain);
    fs_.create_directory(cfg_.data_root() + "/proxy/sites/");

    // Upstream is now a Docker service name, e.g. "site-3-web:80"
    std::string upstream = proxy.upstream.empty() ? "site-0-web:80" : proxy.upstream;

    auto* cert = ssl_mgr_.find_by_domain(proxy.domain);
    bool has_ssl = (cert != nullptr && cert->enabled && cert->status == "active");

    std::ostringstream conf;
    if (has_ssl) {
        conf << "server {\n"
             << "    listen 443 ssl;\n"
             << "    server_name " << proxy.domain << ";\n"
             << "    ssl_certificate " << cert->certificate_path << ";\n"
             << "    ssl_certificate_key " << cert->key_path << ";\n"
             << "\n"
             << "    location / {\n"
             << "        proxy_pass http://" << upstream << ";\n"
             << "        proxy_set_header Host $host;\n"
             << "        proxy_set_header X-Real-IP $remote_addr;\n"
             << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
             << "        proxy_set_header X-Forwarded-Proto https;\n"
             << "    }\n"
             << "}\n"
             << "\n"
             << "server {\n"
             << "    listen 80;\n"
             << "    server_name " << proxy.domain << ";\n"
             << "    return 301 https://$host$request_uri;\n"
             << "}\n";
    } else {
        conf << "server {\n"
             << "    listen 80;\n"
             << "    server_name " << proxy.domain << ";\n"
             << "\n"
             << "    location / {\n"
             << "        proxy_pass http://" << upstream << ";\n"
             << "        proxy_set_header Host $host;\n"
             << "        proxy_set_header X-Real-IP $remote_addr;\n"
             << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
             << "        proxy_set_header X-Forwarded-Proto $scheme;\n"
             << "    }\n"
             << "}\n";
    }

    fs_.create_file(path, conf.str());
    logger_.info("NginxProxyProvider: Created config for " + proxy.domain);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::remove_proxy(const std::string& domain) {
    std::string path = config_path(domain);
    if (fs_.exists(path)) {
        std::filesystem::remove(path);
    }
    logger_.info("NginxProxyProvider: Removed config for " + domain);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::enable_proxy(const std::string& domain) {
    logger_.info("NginxProxyProvider: Enabled proxy for " + domain);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::disable_proxy(const std::string& domain) {
    logger_.info("NginxProxyProvider: Disabled proxy for " + domain);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::reload() {
    std::string cmd = "docker exec " + proxy_name() + " nginx -s reload > /dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        logger_.info("NginxProxyProvider: Reload failed (proxy container may not be running)");
        return {false, "Reload failed: proxy container not running"};
    }
    logger_.info("NginxProxyProvider: Reloaded");
    return {true, ""};
}

core::OperationResult NginxProxyProvider::ensure_central_proxy() {
    if (central_proxy_running()) {
        logger_.info("NginxProxyProvider: Central proxy already running");
        return {true, ""};
    }

    fs_.create_directory(cfg_.data_root() + "/proxy/");
    fs_.create_directory(cfg_.data_root() + "/proxy/sites/");

    // Ensure the shared public network exists
    std::string net_cmd = "docker network inspect containercp-public > /dev/null 2>&1"
        " || docker network create containercp-public > /dev/null 2>&1";
    std::system(net_cmd.c_str());

    // Write a fallback default config so nginx has something to serve
    std::string default_conf_path = cfg_.data_root() + "/proxy/sites/00-default.conf";
    if (!fs_.exists(default_conf_path)) {
        std::ostringstream conf;
        conf << "server {\n"
             << "    listen 80 default_server;\n"
             << "    server_name _;\n"
             << "    return 404;\n"
             << "}\n";
        fs_.create_file(default_conf_path, conf.str());
    }

    std::string cmd = "docker run -d --name " + proxy_name()
        + " --restart unless-stopped"
        + " --network containercp-public"
        + " -p 80:80 -p 443:443"
        + " -v " + cfg_.data_root() + "/proxy/sites/:/etc/nginx/conf.d/"
        + " nginx:alpine > /dev/null 2>&1";

    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        logger_.error("NginxProxyProvider: Failed to create central proxy container");
        return {false, "Failed to create central proxy container"};
    }
    logger_.info("NginxProxyProvider: Central proxy container created on containercp-public");
    return {true, ""};
}

// remove_central_proxy is preserved for cleanup but NOT called on normal shutdown.
// The proxy container survives daemon restart. Only explicit reset should remove it.
core::OperationResult NginxProxyProvider::remove_central_proxy() {
    std::string cmd = "docker rm -f " + proxy_name() + " > /dev/null 2>&1";
    std::system(cmd.c_str());
    logger_.info("NginxProxyProvider: Central proxy container removed");
    return {true, ""};
}

core::OperationResult NginxProxyProvider::status(const std::string& domain) {
    logger_.info("NginxProxyProvider: Status for " + domain);
    return {true, ""};
}

} // namespace containercp::proxy
