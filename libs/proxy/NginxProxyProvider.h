#ifndef CONTAINERCP_PROXY_NGINX_PROXY_PROVIDER_H
#define CONTAINERCP_PROXY_NGINX_PROXY_PROVIDER_H

#include "proxy/ProxyProvider.h"
#include "proxy/ProxyConfigBuilder.h"
#include "proxy/ReverseProxyManager.h"
#include "config/Config.h"
#include "filesystem/Filesystem.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"
#include "ssl/CertificateStore.h"
#include "ssl/SslCertificateManager.h"

#include <cstdint>
#include <mutex>
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
    core::OperationResult sync_all_proxies(const std::vector<ReverseProxy>& all_proxies,
                                            ssl::CertificateStore& cert_store);
    core::OperationResult test_config();
    core::OperationResult last_test_result() const;

    void set_webmail_upstream(const std::string& upstream);
    core::OperationResult upsert_sql_console_route(const std::string& domain,
                                                   const std::string& launch_id,
                                                   uint64_t database_id,
                                                   const std::string& adminer_upstream,
                                                   const std::string& auth_upstream,
                                                   const std::string& site_network);
    core::OperationResult remove_sql_console_route(const std::string& domain,
                                                   const std::string& launch_id);

private:
    std::string config_path(const std::string& domain) const;
    std::string proxy_name() const;
    bool validate_nginx_config(const std::string& config_content) const;
    bool container_config_valid() const;

    mutable std::mutex operation_mutex_;
    mutable std::mutex config_cache_mutex_;
    mutable core::OperationResult cached_test_{false, "Not tested since daemon start"};
    runtime::CommandExecutor executor_;

    filesystem::Filesystem& fs_;
    config::Config& cfg_;
    logger::Logger& logger_;
    ssl::SslCertificateManager& ssl_mgr_;
    proxy::ReverseProxyManager& proxy_mgr_;
    ProxyConfigBuilder config_builder_;
    std::string webmail_upstream_;
};

} // namespace containercp::proxy

#endif // CONTAINERCP_PROXY_NGINX_PROXY_PROVIDER_H
