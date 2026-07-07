#include "SiteCreateOperation.h"

namespace containercp::operations {

SiteCreateOperation::SiteCreateOperation(site::SiteManager& sites, provider::HostingProvider& provider)
    : sites_(sites)
    , provider_(provider)
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

    site::Site site;
    site.name = domain;
    site.domain = domain;
    site.owner = owner;
    site.node_id = node.id;

    sites_.create(domain, owner, node.id);

    return provider_.create_site(site);
}

} // namespace containercp::operations
