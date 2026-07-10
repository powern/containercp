#include "MailboxManager.h"

namespace containercp::mail {

uint64_t MailboxManager::create(uint64_t domain_id, const std::string& local_part,
                                 const std::string& password_hash) {
    // Local part + domain_id must be unique
    for (const auto& existing : mailboxes_) {
        if (existing.domain_id == domain_id && existing.local_part == local_part) {
            return 0;
        }
    }
    Mailbox mb;
    mb.id = next_id_++;
    mb.name = local_part;
    mb.domain_id = domain_id;
    mb.local_part = local_part;
    mb.password_hash = password_hash;
    mb.enabled = true;
    mb.spam_enabled = true;
    mailboxes_.push_back(std::move(mb));
    return mb.id;
}

bool MailboxManager::remove(uint64_t id) {
    for (auto it = mailboxes_.begin(); it != mailboxes_.end(); ++it) {
        if (it->id == id) {
            mailboxes_.erase(it);
            return true;
        }
    }
    return false;
}

Mailbox* MailboxManager::find(uint64_t id) {
    for (auto& mb : mailboxes_) {
        if (mb.id == id) {
            return &mb;
        }
    }
    return nullptr;
}

Mailbox* MailboxManager::find_by_address(const std::string& local_part,
                                          uint64_t domain_id) {
    for (auto& mb : mailboxes_) {
        if (mb.domain_id == domain_id && mb.local_part == local_part) {
            return &mb;
        }
    }
    return nullptr;
}

std::vector<Mailbox*> MailboxManager::find_by_domain(uint64_t domain_id) {
    std::vector<Mailbox*> result;
    for (auto& mb : mailboxes_) {
        if (mb.domain_id == domain_id) {
            result.push_back(&mb);
        }
    }
    return result;
}

const std::vector<Mailbox>& MailboxManager::list() const {
    return mailboxes_;
}

void MailboxManager::set_mailboxes(const std::vector<Mailbox>& mailboxes) {
    mailboxes_ = mailboxes;
    next_id_ = 1;
    for (const auto& mb : mailboxes_) {
        if (mb.id >= next_id_) {
            next_id_ = mb.id + 1;
        }
    }
}

} // namespace containercp::mail
