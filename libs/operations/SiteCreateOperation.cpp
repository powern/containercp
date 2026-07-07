#include "SiteCreateOperation.h"

namespace containercp::operations {

SiteCreateOperation::SiteCreateOperation(site::SiteManager& sites, core::ResourceManager& nodes, filesystem::Filesystem& fs, config::Config& cfg)
    : sites_(sites)
    , nodes_(nodes)
    , fs_(fs)
    , cfg_(cfg)
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

    std::string site_dir = cfg_.data_root() + "/sites/" + domain + "/";
    fs_.create_directory(site_dir);
    fs_.create_file(site_dir + "README.txt", "This site is managed by ContainerCP.\n");

    return {true, ""};
}

} // namespace containercp::operations
