#include "DomainManager.h"

namespace containercp::domain {

uint64_t DomainManager::create(const std::string& fqdn, uint64_t owner_id, uint64_t site_id) {
    Domain d;
    d.id = next_id_++;
    d.name = fqdn;
    d.fqdn = fqdn;
    d.owner_id = owner_id;
    d.site_id = site_id;
    d.php_version = "8.4";
    d.ssl_enabled = false;
    d.enabled = true;
    domains_.push_back(std::move(d));
    return d.id;
}

bool DomainManager::remove(uint64_t id) {
    for (auto it = domains_.begin(); it != domains_.end(); ++it) {
        if (it->id == id) {
            domains_.erase(it);
            return true;
        }
    }
    return false;
}

Domain* DomainManager::find(const std::string& fqdn) {
    for (auto& d : domains_) {
        if (d.fqdn == fqdn) {
            return &d;
        }
    }
    return nullptr;
}

Domain* DomainManager::find(uint64_t id) {
    for (auto& d : domains_) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

const std::vector<Domain>& DomainManager::list() const {
    return domains_;
}

void DomainManager::set_domains(const std::vector<Domain>& domains) {
    domains_ = domains;
    next_id_ = 1;
    for (const auto& d : domains_) {
        if (d.id >= next_id_) {
            next_id_ = d.id + 1;
        }
    }
}

} // namespace containercp::domain
