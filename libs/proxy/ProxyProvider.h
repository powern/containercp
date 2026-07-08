#ifndef CONTAINERCP_PROXY_PROXY_PROVIDER_H
#define CONTAINERCP_PROXY_PROXY_PROVIDER_H

#include "core/OperationResult.h"
#include "proxy/ReverseProxy.h"

#include <string>

namespace containercp::proxy {

class ProxyProvider {
public:
    virtual ~ProxyProvider() = default;

    virtual core::OperationResult create_proxy(const ReverseProxy& proxy) = 0;
    virtual core::OperationResult remove_proxy(const std::string& domain) = 0;
    virtual core::OperationResult enable_proxy(const std::string& domain) = 0;
    virtual core::OperationResult disable_proxy(const std::string& domain) = 0;
    virtual core::OperationResult reload() = 0;
    virtual core::OperationResult status(const std::string& domain) = 0;
    virtual core::OperationResult ensure_central_proxy() { return {true, ""}; }
    virtual core::OperationResult remove_central_proxy() { return {true, ""}; }
};

} // namespace containercp::proxy

#endif // CONTAINERCP_PROXY_PROXY_PROVIDER_H
