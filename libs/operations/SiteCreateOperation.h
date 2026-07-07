#ifndef CONTAINERCP_OPERATIONS_SITE_CREATE_OPERATION_H
#define CONTAINERCP_OPERATIONS_SITE_CREATE_OPERATION_H

#include "config/Config.h"
#include "core/OperationResult.h"
#include "core/ResourceManager.h"
#include "docker/ComposeGenerator.h"
#include "filesystem/Filesystem.h"
#include "node/Node.h"
#include "runtime/Runtime.h"
#include "site/SiteManager.h"

#include <string>

namespace containercp::operations {

class SiteCreateOperation {
public:
    SiteCreateOperation(site::SiteManager& sites, core::ResourceManager& nodes, filesystem::Filesystem& fs, config::Config& cfg, runtime::Runtime& rt);

    core::OperationResult execute(const std::string& owner, const std::string& domain, const node::Node& node);

private:
    site::SiteManager& sites_;
    core::ResourceManager& nodes_;
    filesystem::Filesystem& fs_;
    config::Config& cfg_;
    runtime::Runtime& rt_;
};

} // namespace containercp::operations

#endif // CONTAINERCP_OPERATIONS_SITE_CREATE_OPERATION_H
