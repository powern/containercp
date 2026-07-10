#ifndef CONTAINERCP_PROXY_NGINX_PROXY_PROVIDER_H
#define CONTAINERCP_PROXY_NGINX_PROXY_PROVIDER_H

#include "proxy/ProxyProvider.h"
#include "proxy/ProxyConfigBuilder.h"
#include "proxy/ReverseProxyManager.h"
#include "config/Config.h"
#include "filesystem/Filesystem.h"
#include "logger/Logger.h"
#include "ssl/SslCertificateManager.h"

#include <string>

namespace containercp::proxy {

class NginxProxyProvider : public ProxyProvider {
public:
    NginxProxyProvider(filesystem::Filesystem& fs, config::Config& cfg,
                       logger::Logger& logger, ssl::SslCertificateManager& ssl_mgr,
                       proxy::ReverseProxyManager& proxy_mgr);

    core::OperationResult create_proxy(const ReverseProxy& proxy) override;
    core::OperationResult remove_proxy(const std::string& domain) override;
    core::OperationResult enable_proxy(const std::string& domain) override;
    core::OperationResult disable_proxy(const std::string& domain) override;
    core::OperationResult reload() override;
    core::OperationResult status(const std::string& domain) override;
    core::OperationResult attach_certificate(const std::string& domain,
                                               const std::string& cert_path,
                                               const std::string& key_path,
                                               bool redirect = false) override;
    core::OperationResult detach_certificate(const std::string& domain) override;
    core::OperationResult ensure_central_proxy();
    core::OperationResult remove_central_proxy();
    bool central_proxy_running() const;

private:
    std::string config_path(const std::string& domain) const;
    std::string proxy_name() const;
    bool validate_nginx_config(const std::string& config_content) const;
    bool container_config_valid() const;

    filesystem::Filesystem& fs_;
    config::Config& cfg_;
    logger::Logger& logger_;
    ssl::SslCertificateManager& ssl_mgr_;
    proxy::ReverseProxyManager& proxy_mgr_;
    ProxyConfigBuilder config_builder_;
};

} // namespace containercp::proxy

#endif // CONTAINERCP_PROXY_NGINX_PROXY_PROVIDER_H
