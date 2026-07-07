#ifndef CONTAINERCP_CORE_RESOURCE_MANAGER_H
#define CONTAINERCP_CORE_RESOURCE_MANAGER_H

#include "core/Resource.h"
#include "node/Node.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::core {

class ResourceManager {
public:
    uint64_t add(const Resource& resource);
    bool remove(uint64_t id);
    node::Node* find(uint64_t id);
    node::Node* find(const std::string& name);
    const std::vector<node::Node>& list() const;

private:
    std::vector<node::Node> nodes_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_RESOURCE_MANAGER_H
