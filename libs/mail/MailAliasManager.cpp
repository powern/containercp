#include "MailAliasManager.h"

namespace containercp::mail {

uint64_t MailAliasManager::create(uint64_t domain_id,
                                    const std::string& source_local_part,
                                    const std::string& destination) {
    // Source + domain + destination must be unique
    for (const auto& existing : aliases_) {
        if (existing.domain_id == domain_id &&
            existing.source_local_part == source_local_part &&
            existing.destination == destination) {
            return 0;
        }
    }
    MailAlias a;
    a.id = next_id_++;
    a.name = source_local_part + "@" + std::to_string(domain_id);
    a.domain_id = domain_id;
    a.source_local_part = source_local_part;
    a.destination = destination;
    a.enabled = true;
    aliases_.push_back(std::move(a));
    return a.id;
}

bool MailAliasManager::remove(uint64_t id) {
    for (auto it = aliases_.begin(); it != aliases_.end(); ++it) {
        if (it->id == id) {
            aliases_.erase(it);
            return true;
        }
    }
    return false;
}

MailAlias* MailAliasManager::find(uint64_t id) {
    for (auto& a : aliases_) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

std::vector<MailAlias*> MailAliasManager::find_by_domain(uint64_t domain_id) {
    std::vector<MailAlias*> result;
    for (auto& a : aliases_) {
        if (a.domain_id == domain_id) {
            result.push_back(&a);
        }
    }
    return result;
}

std::vector<MailAlias*> MailAliasManager::find_by_source(
    const std::string& source_local_part, uint64_t domain_id) {
    std::vector<MailAlias*> result;
    for (auto& a : aliases_) {
        if (a.domain_id == domain_id && a.source_local_part == source_local_part) {
            result.push_back(&a);
        }
    }
    return result;
}

const std::vector<MailAlias>& MailAliasManager::list() const {
    return aliases_;
}

void MailAliasManager::set_aliases(const std::vector<MailAlias>& aliases) {
    aliases_ = aliases;
    next_id_ = 1;
    for (const auto& a : aliases_) {
        if (a.id >= next_id_) {
            next_id_ = a.id + 1;
        }
    }
}

} // namespace containercp::mail
