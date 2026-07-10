#ifndef CONTAINERCP_MAIL_MAIL_ALIAS_H
#define CONTAINERCP_MAIL_MAIL_ALIAS_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::mail {

struct MailAlias : core::Resource {
    uint64_t domain_id = 0;         // FK to MailDomain
    std::string source_local_part;   // e.g. "info" (local part of source address)
    std::string destination;         // full destination address (local or external)
    bool enabled = true;
    std::string created_at;
    std::string updated_at;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_MAIL_ALIAS_H
