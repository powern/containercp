#ifndef CONTAINERCP_PROXY_PROXY_CONFIG_BUILDER_H
#define CONTAINERCP_PROXY_PROXY_CONFIG_BUILDER_H

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
    };

    std::string build(const Params& params) const;

    std::string build_http_block(const std::string& domain,
                                  const std::string& upstream) const;

    std::string build_https_block(const std::string& domain,
                                   const std::string& upstream,
                                   const std::string& cert_path,
                                   const std::string& key_path) const;

    std::string build_redirect_block(const std::string& domain) const;

    std::string acme_challenge_location() const;
};

} // namespace containercp::proxy

#endif // CONTAINERCP_PROXY_PROXY_CONFIG_BUILDER_H
