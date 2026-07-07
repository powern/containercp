#include "ReverseProxyManager.h"

namespace containercp::proxy {

uint64_t ReverseProxyManager::create(const std::string& domain, uint64_t site_id, const std::string& config_path, const std::string& upstream) {
    ReverseProxy p;
    p.id = next_id_++;
    p.name = domain;
    p.domain = domain;
    p.site_id = site_id;
    p.provider = "nginx";
    p.config_path = config_path;
    p.upstream = upstream;
    p.enabled = true;
    p.status = "active";
    proxies_.push_back(std::move(p));
    return p.id;
}

bool ReverseProxyManager::remove(uint64_t id) {
    for (auto it = proxies_.begin(); it != proxies_.end(); ++it) {
        if (it->id == id) {
            proxies_.erase(it);
            return true;
        }
    }
    return false;
}

ReverseProxy* ReverseProxyManager::find(uint64_t id) {
    for (auto& p : proxies_) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

ReverseProxy* ReverseProxyManager::find_by_domain(const std::string& domain) {
    for (auto& p : proxies_) {
        if (p.domain == domain) {
            return &p;
        }
    }
    return nullptr;
}

const std::vector<ReverseProxy>& ReverseProxyManager::list() const {
    return proxies_;
}

void ReverseProxyManager::set_proxies(const std::vector<ReverseProxy>& proxies) {
    proxies_ = proxies;
    next_id_ = 1;
    for (const auto& p : proxies_) {
        if (p.id >= next_id_) {
            next_id_ = p.id + 1;
        }
    }
}

} // namespace containercp::proxy
