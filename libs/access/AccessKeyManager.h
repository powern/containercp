#ifndef CONTAINERCP_ACCESS_ACCESS_KEY_MANAGER_H
#define CONTAINERCP_ACCESS_ACCESS_KEY_MANAGER_H

#include "access/AccessKey.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::access {

class AccessKeyManager {
public:
    AccessKeyManager() = default;

    // Create a key. Returns the assigned id, or 0 on duplicate fingerprint.
    uint64_t create(const AccessKey& key);

    // Find by id. Returns nullptr if not found.
    const AccessKey* find(uint64_t id) const;

    // List all keys for a given access user.
    std::vector<const AccessKey*> list_by_user(uint64_t access_user_id) const;

    // List all keys.
    const std::vector<AccessKey>& list() const;

    // Set enabled state. Returns true if the key was found.
    bool set_enabled(uint64_t id, bool enabled);

    // Remove a key by id. Returns true if found and removed.
    bool remove(uint64_t id);

    // Replace all keys (used for loading from storage).
    void set_keys(const std::vector<AccessKey>& keys);

private:
    std::vector<AccessKey> keys_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_ACCESS_KEY_MANAGER_H
