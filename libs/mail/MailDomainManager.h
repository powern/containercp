#ifndef CONTAINERCP_MAIL_MAIL_DOMAIN_MANAGER_H
#define CONTAINERCP_MAIL_MAIL_DOMAIN_MANAGER_H

#include "mail/MailDomain.h"
#include "mail/MailModuleState.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::mail {

class MailDomainManager {
public:
    uint64_t create(const std::string& domain_name, MailDomainMode mode,
                    uint64_t domain_id, uint64_t site_id,
                    const std::string& relay_host = "");

    // Validate mode+relay_host combination. Returns empty string on success,
    // error message on failure.
    static std::string validate_mode_relay(MailDomainMode mode,
                                            const std::string& relay_host);

    // Smarthost (external SMTP relay) for all outbound mail
    struct SmarthostConfig {
        std::string host;
        int port = 587;
        std::string username;
        std::string password;
        bool enabled = false;
    };

    void set_smarthost(const SmarthostConfig& cfg);
    const SmarthostConfig& smarthost() const { return smarthost_; }
    std::string smarthost_to_string() const;
    void smarthost_from_string(const std::string& s);

    bool remove(uint64_t id);
    MailDomain* find(uint64_t id);
    MailDomain* find_by_domain(const std::string& domain_name);
    const std::vector<MailDomain>& list() const;

    // Enable mail for a site: find existing or create, set mode=LocalPrimary
    // Returns the MailDomain ID. Idempotent.
    uint64_t enable_for_site(uint64_t site_id, const std::string& domain_name);

    void set_domains(const std::vector<MailDomain>& domains);

    // Mail module lifecycle state
    MailModuleState module_state() const { return module_state_; }
    void set_module_state(MailModuleState state) { module_state_ = state; }

private:
    std::vector<MailDomain> domains_;
    uint64_t next_id_ = 1;
    MailModuleState module_state_ = MailModuleState::Inactive;
    SmarthostConfig smarthost_;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_MAIL_DOMAIN_MANAGER_H
