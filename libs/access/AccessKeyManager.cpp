#include "access/AccessKeyManager.h"

#include <algorithm>

namespace containercp::access {

uint64_t AccessKeyManager::create(const AccessKey& key) {
    // Check for duplicate fingerprint for the same user
    for (const auto& existing : keys_) {
        if (existing.access_user_id == key.access_user_id &&
            existing.fingerprint == key.fingerprint) {
            return 0;
        }
    }

    AccessKey copy = key;
    copy.id = next_id_++;
    keys_.push_back(std::move(copy));
    return keys_.back().id;
}

const AccessKey* AccessKeyManager::find(uint64_t id) const {
    for (const auto& k : keys_) {
        if (k.id == id) return &k;
    }
    return nullptr;
}

std::vector<const AccessKey*> AccessKeyManager::list_by_user(uint64_t access_user_id) const {
    std::vector<const AccessKey*> result;
    for (const auto& k : keys_) {
        if (k.access_user_id == access_user_id) result.push_back(&k);
    }
    return result;
}

const std::vector<AccessKey>& AccessKeyManager::list() const {
    return keys_;
}

bool AccessKeyManager::set_enabled(uint64_t id, bool enabled) {
    for (auto& k : keys_) {
        if (k.id == id) {
            k.enabled = enabled;
            return true;
        }
    }
    return false;
}

bool AccessKeyManager::remove(uint64_t id) {
    auto it = std::find_if(keys_.begin(), keys_.end(),
                           [id](const AccessKey& k) { return k.id == id; });
    if (it == keys_.end()) return false;
    keys_.erase(it);
    return true;
}

void AccessKeyManager::set_keys(const std::vector<AccessKey>& keys) {
    keys_ = keys;
    next_id_ = 1;
    for (const auto& k : keys_) {
        if (k.id >= next_id_) next_id_ = k.id + 1;
    }
}

} // namespace containercp::access
