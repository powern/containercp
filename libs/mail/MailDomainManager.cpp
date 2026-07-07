#include "MailDomainManager.h"

namespace containercp::mail {

uint64_t MailDomainManager::create(uint64_t domain_id, const std::string& domain, uint64_t owner_id) {
    MailDomain m;
    m.id = next_id_++;
    m.name = domain;
    m.domain_id = domain_id;
    m.domain = domain;
    m.owner_id = owner_id;
    m.enabled = true;
    m.status = "placeholder";
    domains_.push_back(std::move(m));
    return m.id;
}

bool MailDomainManager::remove(uint64_t id) {
    for (auto it = domains_.begin(); it != domains_.end(); ++it) {
        if (it->id == id) {
            domains_.erase(it);
            return true;
        }
    }
    return false;
}

MailDomain* MailDomainManager::find(uint64_t id) {
    for (auto& m : domains_) {
        if (m.id == id) {
            return &m;
        }
    }
    return nullptr;
}

MailDomain* MailDomainManager::find_by_domain(const std::string& domain) {
    for (auto& m : domains_) {
        if (m.domain == domain) {
            return &m;
        }
    }
    return nullptr;
}

const std::vector<MailDomain>& MailDomainManager::list() const {
    return domains_;
}

void MailDomainManager::set_domains(const std::vector<MailDomain>& domains) {
    domains_ = domains;
    next_id_ = 1;
    for (const auto& m : domains_) {
        if (m.id >= next_id_) {
            next_id_ = m.id + 1;
        }
    }
}

} // namespace containercp::mail
