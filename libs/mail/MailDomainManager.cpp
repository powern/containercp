#include "MailDomainManager.h"
#include "utils/Validator.h"

namespace containercp::mail {

std::string MailDomainManager::validate_mode_relay(
    MailDomainMode mode, const std::string& relay_host) {
    if ((mode == MailDomainMode::ExternalRelay ||
         mode == MailDomainMode::SplitM365) && relay_host.empty()) {
        return "relay_host is required for " +
               mail_domain_mode_to_string(mode) + " mode";
    }
    return "";
}

uint64_t MailDomainManager::create(const std::string& domain_name,
                                    MailDomainMode mode,
                                    uint64_t owner_id,
                                    const std::string& relay_host) {
    // Validate mode+relay before creating
    std::string vr = validate_mode_relay(mode, relay_host);
    if (!vr.empty()) return 0;

    for (const auto& existing : domains_) {
        if (existing.domain_name == domain_name) {
            return 0;
        }
    }
    MailDomain m;
    m.id = next_id_++;
    m.name = domain_name;
    m.domain_name = domain_name;
    m.mode = mode;
    m.owner_id = owner_id;
    m.relay_host = relay_host;
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
