#ifndef CONTAINERCP_MAIL_MAIL_DOMAIN_H
#define CONTAINERCP_MAIL_MAIL_DOMAIN_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::mail {

struct MailDomain : core::Resource {
    uint64_t domain_id = 0;
    std::string domain;
    uint64_t owner_id = 0;
    bool enabled = true;
    std::string status = "placeholder";
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_MAIL_DOMAIN_H
