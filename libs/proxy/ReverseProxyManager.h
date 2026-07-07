#ifndef CONTAINERCP_PROXY_REVERSE_PROXY_MANAGER_H
#define CONTAINERCP_PROXY_REVERSE_PROXY_MANAGER_H

#include "proxy/ReverseProxy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::proxy {

class ReverseProxyManager {
public:
    uint64_t create(const std::string& domain, uint64_t site_id, const std::string& config_path, const std::string& upstream);
    bool remove(uint64_t id);
    ReverseProxy* find(uint64_t id);
    ReverseProxy* find_by_domain(const std::string& domain);
    const std::vector<ReverseProxy>& list() const;

    void set_proxies(const std::vector<ReverseProxy>& proxies);

private:
    std::vector<ReverseProxy> proxies_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::proxy

#endif // CONTAINERCP_PROXY_REVERSE_PROXY_MANAGER_H
