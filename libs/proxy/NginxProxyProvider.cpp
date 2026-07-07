#include "NginxProxyProvider.h"

#include <filesystem>
#include <sstream>

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

core::OperationResult NginxProxyProvider::create_proxy(const ReverseProxy& proxy) {
    std::string path = config_path(proxy.domain);
    fs_.create_directory(cfg_.data_root() + "/proxy/sites/");

    std::string port = "80";
    auto pos = proxy.upstream.find_last_of(':');
    if (pos != std::string::npos) {
        port = proxy.upstream.substr(pos + 1);
    }

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
             << "        proxy_pass http://127.0.0.1:" << port << ";\n"
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
             << "        proxy_pass http://127.0.0.1:" << port << ";\n"
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
    logger_.info("NginxProxyProvider: Reload requested (no-op)");
    return {true, ""};
}

core::OperationResult NginxProxyProvider::status(const std::string& domain) {
    logger_.info("NginxProxyProvider: Status for " + domain);
    return {true, ""};
}

} // namespace containercp::proxy
