#ifndef CONTAINERCP_PROXY_PROXY_CONFIG_BUILDER_H
#define CONTAINERCP_PROXY_PROXY_CONFIG_BUILDER_H

#include <cstdint>
#include <string>

namespace containercp::proxy {

// Generates nginx server block configurations.
// Keeps string-building logic separate from NginxProxyProvider.
// Future: HTTP/2, HTTP/3, HSTS, OCSP, mTLS.

class ProxyConfigBuilder {
public:
    struct Params {
        std::string domain;
        std::string upstream;
        bool https = false;
        bool redirect = false;
        std::string cert_path;
        std::string key_path;
        std::string acme_challenge_root;
        std::string webmail_upstream;
        std::string sql_console_locations;
    };

    std::string build(const Params& params) const;

    std::string build_http_block(const std::string& domain,
                                  const std::string& upstream) const;

    std::string build_https_block(const std::string& domain,
                                    const std::string& upstream,
                                    const std::string& cert_path,
                                    const std::string& key_path,
                                    const std::string& webmail_loc = "") const;

    std::string build_redirect_block(const std::string& domain) const;

    static std::string acme_challenge_location(const std::string& challenge_root);

    static std::string normalize_upstream(const std::string& raw);
    static std::string sql_console_route_locations(const std::string& launch_id,
                                                   uint64_t database_id,
                                                   const std::string& adminer_upstream,
                                                   const std::string& auth_upstream);
};

} // namespace containercp::proxy

#endif // CONTAINERCP_PROXY_PROXY_CONFIG_BUILDER_H
