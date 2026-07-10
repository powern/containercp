#ifndef CONTAINERCP_MAIL_MAIL_DOMAIN_MANAGER_H
#define CONTAINERCP_MAIL_MAIL_DOMAIN_MANAGER_H

#include "mail/MailDomain.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::mail {

class MailDomainManager {
public:
    uint64_t create(const std::string& domain_name, MailDomainMode mode,
                    uint64_t owner_id);
    bool remove(uint64_t id);
    MailDomain* find(uint64_t id);
    MailDomain* find_by_domain(const std::string& domain_name);
    const std::vector<MailDomain>& list() const;

    void set_domains(const std::vector<MailDomain>& domains);

private:
    std::vector<MailDomain> domains_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_MAIL_DOMAIN_MANAGER_H
