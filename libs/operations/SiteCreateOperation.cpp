#include "SiteCreateOperation.h"

namespace containercp::operations {

SiteCreateOperation::SiteCreateOperation(site::SiteManager& sites, core::ResourceManager& nodes)
    : sites_(sites)
    , nodes_(nodes)
{
}

core::OperationResult SiteCreateOperation::execute(const std::string& owner, const std::string& domain, const node::Node& node) {
    if (domain.empty()) {
        return {false, "Domain cannot be empty."};
    }

    if (owner.empty()) {
        return {false, "Owner cannot be empty."};
    }

    if (sites_.find(domain) != nullptr) {
        return {false, "Site already exists."};
    }

    sites_.create(domain, owner, node.id);
    return {true, ""};
}

} // namespace containercp::operations
