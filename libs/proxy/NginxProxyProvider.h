#ifndef CONTAINERCP_PROXY_NGINX_PROXY_PROVIDER_H
#define CONTAINERCP_PROXY_NGINX_PROXY_PROVIDER_H

#include "proxy/ProxyProvider.h"
#include "config/Config.h"
#include "filesystem/Filesystem.h"
#include "logger/Logger.h"
#include "ssl/SslCertificateManager.h"

#include <string>

namespace containercp::proxy {

class NginxProxyProvider : public ProxyProvider {
public:
    NginxProxyProvider(filesystem::Filesystem& fs, config::Config& cfg,
                       logger::Logger& logger, ssl::SslCertificateManager& ssl_mgr);

    core::OperationResult create_proxy(const ReverseProxy& proxy) override;
    core::OperationResult remove_proxy(const std::string& domain) override;
    core::OperationResult enable_proxy(const std::string& domain) override;
    core::OperationResult disable_proxy(const std::string& domain) override;
    core::OperationResult reload() override;
    core::OperationResult status(const std::string& domain) override;
    core::OperationResult ensure_central_proxy();
    core::OperationResult remove_central_proxy();

private:
    std::string config_path(const std::string& domain) const;
    std::string proxy_name() const;
    bool central_proxy_running() const;

    filesystem::Filesystem& fs_;
    config::Config& cfg_;
    logger::Logger& logger_;
    ssl::SslCertificateManager& ssl_mgr_;
};

} // namespace containercp::proxy

#endif // CONTAINERCP_PROXY_NGINX_PROXY_PROVIDER_H
