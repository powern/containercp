#include "SiteCreateOperation.h"
#include "utils/PasswordGenerator.h"
#include "utils/StringUtils.h"
#include "utils/Validator.h"

namespace containercp::operations {

SiteCreateOperation::SiteCreateOperation(site::SiteManager& sites, domain::DomainManager& domains, database::DatabaseManager& databases, provider::HostingProvider& provider)
    : sites_(sites)
    , domains_(domains)
    , databases_(databases)
    , provider_(provider)
{
}

core::OperationResult SiteCreateOperation::execute(const std::string& owner, const std::string& domain, const node::Node& node) {
    if (!utils::Validator::is_valid_username(owner)) {
        return {false, "Invalid username."};
    }

    if (!utils::Validator::is_valid_hostname(domain)) {
        return {false, "Invalid domain."};
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

    domains_.create(domain, 0, site.id);

    std::string safe = utils::StringUtils::sanitize(domain);
    std::string db_name = safe + "_db";
    std::string db_user = safe + "_user";
    std::string db_password = utils::PasswordGenerator::generate();
    databases_.create(db_name, db_user, db_password, 0, site.id);

    site.db_name = db_name;
    site.db_user = db_user;
    site.db_password = db_password;

    return provider_.create_site(site);
}

} // namespace containercp::operations
