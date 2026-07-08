#include "SiteCreateOperation.h"
#include "utils/PasswordGenerator.h"
#include "utils/StringUtils.h"
#include "utils/Validator.h"

#include <iostream>

namespace containercp::operations {

SiteCreateOperation::SiteCreateOperation(site::SiteManager& sites, domain::DomainManager& domains,
                                         database::DatabaseManager& databases,
                                         proxy::ReverseProxyManager& proxies,
                                         proxy::ProxyProvider& proxy_provider,
                                         filesystem::Filesystem& fs, config::Config& cfg,
                                         provider::HostingProvider& provider)
    : sites_(sites)
    , domains_(domains)
    , databases_(databases)
    , proxies_(proxies)
    , proxy_provider_(proxy_provider)
    , fs_(fs)
    , cfg_(cfg)
    , provider_(provider)
{
}

core::OperationResult SiteCreateOperation::execute(const std::string& owner, const std::string& domain, const node::Node& node, bool dry_run, const std::string& profile) {
    {
        std::string msg = utils::Validator::validate_username(owner);
        if (!msg.empty()) return {false, msg};
    }

    {
        std::string msg = utils::Validator::validate_hostname(domain);
        if (!msg.empty()) return {false, msg};
    }

    if (sites_.find(domain) != nullptr) {
        return {false, "Site already exists."};
    }

    std::string web_server = "apache";
    if (!profile.empty()) {
        // Map profile name to web server type
        if (profile.find("nginx") != std::string::npos) {
            web_server = "nginx";
        }
    }

    if (dry_run) {
        std::cout << "[Dry Run] Would create site: " << domain << "\n";
        std::cout << "[Dry Run] Would create domain: " << domain << "\n";
        std::cout << "[Dry Run] Would create database: " << utils::StringUtils::sanitize(domain) << "_db\n";
        std::cout << "[Dry Run] Would generate docker-compose.yml with Docker network routing\n";
        std::cout << "[Dry Run] Would create directory: /srv/containercp/sites/" << domain << "/\n";
        std::cout << "[Dry Run] Would start Docker stack (no host ports)\n";
        std::cout << "[Dry Run] Selected backend: " << web_server << "\n";
        return {true, ""};
    }

    site::Site site;
    site.name = domain;
    site.domain = domain;
    site.owner = owner;
    site.node_id = node.id;
    site.web_server = web_server;

    site.id = sites_.create(domain, owner, node.id, web_server);

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
        // Rollback: remove filesystem
        fs_.remove_directory(cfg_.sites_dir() + domain + "/");
        // Rollback: remove proxy config if it exists
        proxy_provider_.remove_proxy(domain);
        auto* rp = proxies_.find_by_domain(domain);
        if (rp != nullptr) proxies_.remove(rp->id);
        // Rollback: remove in-memory records
        for (const auto& d : databases_.list()) {
            if (d.site_id == site.id) {
                databases_.remove(d.id);
            }
        }
        for (const auto& d : domains_.list()) {
            if (d.site_id == site.id) {
                domains_.remove(d.id);
            }
        }
        sites_.remove(site.id);
        return {false, result.message + " Created resources have been rolled back."};
    }

    // Create proxy config pointing to site web container via Docker network
    std::string upstream = "site-" + std::to_string(site.id) + "-web:80";
    proxy::ReverseProxy rp;
    rp.domain = domain;
    rp.site_id = site.id;
    rp.provider = "nginx";
    rp.upstream = upstream;
    rp.enabled = true;
    rp.status = "active";
    proxy_provider_.create_proxy(rp);
    proxy_provider_.reload();

    proxies_.create(domain, site.id, cfg_.data_root() + "/proxy/sites/" + domain + ".conf", upstream);
    return {true, ""};
}

} // namespace containercp::operations
