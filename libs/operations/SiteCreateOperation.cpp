#include "SiteCreateOperation.h"

#include <random>

namespace containercp::operations {

SiteCreateOperation::SiteCreateOperation(site::SiteManager& sites, domain::DomainManager& domains, database::DatabaseManager& databases, provider::HostingProvider& provider)
    : sites_(sites)
    , domains_(domains)
    , databases_(databases)
    , provider_(provider)
{
}

std::string SiteCreateOperation::sanitize(const std::string& domain) {
    std::string result = domain;
    for (auto& c : result) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    return result;
}

std::string SiteCreateOperation::generate_password(int length) {
    static constexpr char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    std::string password;
    password.reserve(length);
    for (int i = 0; i < length; ++i) {
        password += chars[dist(gen)];
    }
    return password;
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

    domains_.create(domain, 0, site.id);

    std::string safe = sanitize(domain);
    databases_.create(safe + "_db", safe + "_user", generate_password(32), 0, site.id);

    return provider_.create_site(site);
}

} // namespace containercp::operations
