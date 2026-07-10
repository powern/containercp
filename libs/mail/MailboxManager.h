#ifndef CONTAINERCP_MAIL_MAILBOX_MANAGER_H
#define CONTAINERCP_MAIL_MAILBOX_MANAGER_H

#include "mail/Mailbox.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::mail {

class MailboxManager {
public:
    uint64_t create(uint64_t domain_id, const std::string& local_part,
                    const std::string& password_hash);
    bool remove(uint64_t id);
    Mailbox* find(uint64_t id);
    Mailbox* find_by_address(const std::string& local_part, uint64_t domain_id);
    std::vector<Mailbox*> find_by_domain(uint64_t domain_id);
    const std::vector<Mailbox>& list() const;

    void set_mailboxes(const std::vector<Mailbox>& mailboxes);

private:
    std::vector<Mailbox> mailboxes_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_MAILBOX_MANAGER_H
