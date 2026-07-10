#ifndef CONTAINERCP_MAIL_MAIL_ALIAS_MANAGER_H
#define CONTAINERCP_MAIL_MAIL_ALIAS_MANAGER_H

#include "mail/MailAlias.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::mail {

class MailAliasManager {
public:
    uint64_t create(uint64_t domain_id, const std::string& source_local_part,
                    const std::string& destination);
    bool remove(uint64_t id);
    MailAlias* find(uint64_t id);
    std::vector<MailAlias*> find_by_domain(uint64_t domain_id);
    std::vector<MailAlias*> find_by_source(const std::string& source_local_part,
                                            uint64_t domain_id);
    const std::vector<MailAlias>& list() const;
    void set_aliases(const std::vector<MailAlias>& aliases);

private:
    std::vector<MailAlias> aliases_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_MAIL_ALIAS_MANAGER_H
