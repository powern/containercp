#ifndef CONTAINERCP_MAIL_MAILBOX_H
#define CONTAINERCP_MAIL_MAILBOX_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::mail {

struct Mailbox : core::Resource {
    uint64_t domain_id = 0;         // FK to MailDomain
    std::string local_part;          // e.g. "user" for user@example.com
    std::string password_hash;       // Dovecot-compatible SHA-512-CRYPT hash
    uint64_t quota_bytes = 0;        // 0 = unlimited
    uint64_t quota_messages = 0;     // 0 = unlimited
    bool enabled = true;
    std::string display_name;
    std::string forward_to;          // forwarding address (empty = local delivery)
    bool spam_enabled = true;
    std::string last_login;
    std::string created_at;
    std::string updated_at;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_MAILBOX_H
