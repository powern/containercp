#include "SiteCreateOperation.h"
#include "utils/PasswordGenerator.h"
#include "utils/StringUtils.h"
#include "utils/Validator.h"

#include <iostream>

namespace containercp::operations {

SiteCreateOperation::SiteCreateOperation(site::SiteManager& sites, domain::DomainManager& domains, database::DatabaseManager& databases, provider::HostingProvider& provider)
    : sites_(sites)
    , domains_(domains)
    , databases_(databases)
    , provider_(provider)
{
}

core::OperationResult SiteCreateOperation::execute(const std::string& owner, const std::string& domain, const node::Node& node, bool dry_run) {
    if (!utils::Validator::is_valid_username(owner)) {
        return {false, "Invalid username."};
    }

    if (!utils::Validator::is_valid_hostname(domain)) {
        return {false, "Invalid domain."};
    }

    if (sites_.find(domain) != nullptr) {
        return {false, "Site already exists."};
    }

    if (dry_run) {
        std::cout << "[Dry Run] Would create site: " << domain << "\n";
        std::cout << "[Dry Run] Would create domain: " << domain << "\n";
        std::cout << "[Dry Run] Would create database: " << utils::StringUtils::sanitize(domain) << "_db\n";
        std::cout << "[Dry Run] Would generate docker-compose.yml\n";
        std::cout << "[Dry Run] Would create directory: /srv/containercp/sites/" << domain << "/\n";
        std::cout << "[Dry Run] Would start Docker stack\n";
        return {true, ""};
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

    auto result = provider_.create_site(site);

    if (!result.success) {
        databases_.remove(site.id);
        domains_.remove(site.id);
        sites_.remove(site.id);
        return {false, result.message + " Created resources have been rolled back."};
    }

    return {true, ""};
}

} // namespace containercp::operations
