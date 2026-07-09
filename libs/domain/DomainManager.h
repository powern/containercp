#ifndef CONTAINERCP_DOMAIN_DOMAIN_MANAGER_H
#define CONTAINERCP_DOMAIN_DOMAIN_MANAGER_H

#include "domain/Domain.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::domain {

class DomainManager {
public:
    uint64_t create(const std::string& fqdn, uint64_t owner_id, uint64_t site_id,
                    const std::string& type = "primary",
                    const std::string& target = "");
    bool remove(uint64_t id);
    Domain* find(const std::string& fqdn);
    Domain* find(uint64_t id);
    const std::vector<Domain>& list() const;

    void set_domains(const std::vector<Domain>& domains);

private:
    std::vector<Domain> domains_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::domain

#endif // CONTAINERCP_DOMAIN_DOMAIN_MANAGER_H
