#include "MailDomainManager.h"
#include "utils/Validator.h"

namespace containercp::mail {

uint64_t MailDomainManager::create(const std::string& domain_name,
                                    MailDomainMode mode,
                                    uint64_t owner_id) {
    // A mail domain name must be globally unique per ContainerCP instance
    // because email routing is global — one domain cannot have two different
    // mail configurations on the same server.
    for (const auto& existing : domains_) {
        if (existing.domain_name == domain_name) {
            return 0;  // duplicate — caller should check
        }
    }
    MailDomain m;
    m.id = next_id_++;
    m.name = domain_name;
    m.domain_name = domain_name;
    m.mode = mode;
    m.owner_id = owner_id;
    m.enabled = true;
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

MailDomain* MailDomainManager::find_by_domain(const std::string& domain_name) {
    for (auto& m : domains_) {
        if (m.domain_name == domain_name) {
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
