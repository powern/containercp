#include "AccessGrantManager.h"

namespace containercp::access {

uint64_t AccessGrantManager::create(uint64_t access_user_id, uint64_t site_id, Permission permission) {
    AccessGrant g;
    g.id = next_id_++;
    g.name = std::to_string(access_user_id) + "-" + std::to_string(site_id);
    g.access_user_id = access_user_id;
    g.site_id = site_id;
    g.permission = permission;
    grants_.push_back(std::move(g));
    return g.id;
}

bool AccessGrantManager::remove(uint64_t id) {
    for (auto it = grants_.begin(); it != grants_.end(); ++it) {
        if (it->id == id) {
            grants_.erase(it);
            return true;
        }
    }
    return false;
}

AccessGrant* AccessGrantManager::find(uint64_t id) {
    for (auto& g : grants_) {
        if (g.id == id) {
            return &g;
        }
    }
    return nullptr;
}

std::vector<AccessGrant*> AccessGrantManager::find_by_user(uint64_t access_user_id) {
    std::vector<AccessGrant*> result;
    for (auto& g : grants_) {
        if (g.access_user_id == access_user_id) {
            result.push_back(&g);
        }
    }
    return result;
}

std::vector<AccessGrant*> AccessGrantManager::find_by_site(uint64_t site_id) {
    std::vector<AccessGrant*> result;
    for (auto& g : grants_) {
        if (g.site_id == site_id) {
            result.push_back(&g);
        }
    }
    return result;
}

const std::vector<AccessGrant>& AccessGrantManager::list() const {
    return grants_;
}

void AccessGrantManager::set_grants(const std::vector<AccessGrant>& grants) {
    grants_ = grants;
    next_id_ = 1;
    for (const auto& g : grants_) {
        if (g.id >= next_id_) {
            next_id_ = g.id + 1;
        }
    }
}

} // namespace containercp::access
