#include "SiteManager.h"

namespace containercp::site {

uint64_t SiteManager::create(const std::string& domain, const std::string& owner, uint64_t node_id,
                              const std::string& web_server) {
    Site s;
    s.id = next_id_++;
    s.name = domain;
    s.domain = domain;
    s.owner = owner;
    s.node_id = node_id;
    s.web_server = web_server.empty() ? "apache" : web_server;
    sites_.push_back(std::move(s));
    return s.id;
}

bool SiteManager::remove(uint64_t id) {
    for (auto it = sites_.begin(); it != sites_.end(); ++it) {
        if (it->id == id) {
            sites_.erase(it);
            return true;
        }
    }
    return false;
}

Site* SiteManager::find(const std::string& domain) {
    for (auto& s : sites_) {
        if (s.domain == domain) {
            return &s;
        }
    }
    return nullptr;
}

const std::vector<Site>& SiteManager::list() const {
    return sites_;
}

void SiteManager::set_sites(const std::vector<Site>& sites) {
    sites_ = sites;
    next_id_ = 1;
    for (const auto& s : sites_) {
        if (s.id >= next_id_) {
            next_id_ = s.id + 1;
        }
    }
}

} // namespace containercp::site
