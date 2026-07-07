#include "ResourceManager.h"

namespace containercp::core {

uint64_t ResourceManager::add(const Resource& resource) {
    node::Node n;
    n.id = next_id_++;
    n.name = resource.name;
    nodes_.push_back(std::move(n));
    return n.id;
}

bool ResourceManager::remove(uint64_t id) {
    for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
        if (it->id == id) {
            nodes_.erase(it);
            return true;
        }
    }
    return false;
}

node::Node* ResourceManager::find(uint64_t id) {
    for (auto& n : nodes_) {
        if (n.id == id) {
            return &n;
        }
    }
    return nullptr;
}

node::Node* ResourceManager::find(const std::string& name) {
    for (auto& n : nodes_) {
        if (n.name == name) {
            return &n;
        }
    }
    return nullptr;
}

const std::vector<node::Node>& ResourceManager::list() const {
    return nodes_;
}

} // namespace containercp::core
