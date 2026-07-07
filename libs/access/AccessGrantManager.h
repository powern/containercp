#ifndef CONTAINERCP_ACCESS_ACCESS_GRANT_MANAGER_H
#define CONTAINERCP_ACCESS_ACCESS_GRANT_MANAGER_H

#include "access/AccessGrant.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::access {

class AccessGrantManager {
public:
    uint64_t create(uint64_t access_user_id, uint64_t site_id, Permission permission);
    bool remove(uint64_t id);
    AccessGrant* find(uint64_t id);
    std::vector<AccessGrant*> find_by_user(uint64_t access_user_id);
    std::vector<AccessGrant*> find_by_site(uint64_t site_id);
    const std::vector<AccessGrant>& list() const;

    void set_grants(const std::vector<AccessGrant>& grants);

private:
    std::vector<AccessGrant> grants_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_ACCESS_GRANT_MANAGER_H
