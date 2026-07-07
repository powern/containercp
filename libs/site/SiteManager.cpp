#include "SiteManager.h"

namespace containercp::site {

uint64_t SiteManager::create(const std::string& domain, const std::string& owner, uint64_t node_id) {
    Site s;
    s.id = next_id_++;
    s.name = domain;
    s.domain = domain;
    s.owner = owner;
    s.node_id = node_id;
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

} // namespace containercp::site
