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

static void update_job_and_progress(jobs::JobManager* jobs, uint64_t job_id,
                                     int percent, const std::string& step) {
    if (jobs != nullptr && job_id > 0) {
        jobs->update(job_id, "running", percent, step);
    }
}

core::OperationResult SiteCreateOperation::execute(const std::string& owner,
    const std::string& domain, const node::Node& node, bool dry_run,
    const std::string& profile, jobs::JobManager* jobs, uint64_t job_id) {

    update_job_and_progress(jobs, job_id, 5, "Validating parameters...");

    {
        std::string msg = utils::Validator::validate_username(owner);
        if (!msg.empty()) {
            update_job_and_progress(jobs, job_id, 0, "Validation failed: " + msg);
            return {false, msg};
        }
    }

    {
        std::string msg = utils::Validator::validate_hostname(domain);
        if (!msg.empty()) {
            update_job_and_progress(jobs, job_id, 0, "Validation failed: " + msg);
            return {false, msg};
        }
    }

    if (sites_.find(domain) != nullptr) {
        update_job_and_progress(jobs, job_id, 0, "Site already exists");
        return {false, "Site already exists."};
    }

    std::string web_server = "apache";
    if (!profile.empty()) {
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
        update_job_and_progress(jobs, job_id, 100, "Dry run completed");
        return {true, ""};
    }

    update_job_and_progress(jobs, job_id, 10, "Creating site record...");

    site::Site site;
    site.name = domain;
    site.domain = domain;
    site.owner = owner;
    site.node_id = node.id;
    site.web_server = web_server;

    site.id = sites_.create(domain, owner, node.id, web_server);

    update_job_and_progress(jobs, job_id, 15, "Creating domain record...");
    domains_.create(domain, 0, site.id);

    update_job_and_progress(jobs, job_id, 20, "Creating database...");
    std::string safe = utils::StringUtils::sanitize(domain);
    std::string db_name = safe + "_db";
    std::string db_user = safe + "_user";
    std::string db_password = utils::PasswordGenerator::generate();
    databases_.create(db_name, db_user, db_password, 0, site.id);

    site.db_name = db_name;
    site.db_user = db_user;
    site.db_password = db_password;

    // Create progress callback that wraps the job system
    auto progress = [jobs, job_id](int percent, const std::string& step) {
        update_job_and_progress(jobs, job_id, percent, step);
    };

    auto result = provider_.create_site(site, progress);

    if (!result.success) {
        // Rollback: remove filesystem
        fs_.remove_directory(cfg_.sites_dir() + domain + "/");
        // Rollback: remove proxy config if it exists
        proxy_provider_.remove_proxy(domain);
        auto* rp = proxies_.find_by_domain(domain);
        if (rp != nullptr) proxies_.remove(rp->id);
        // Rollback: remove Docker containers/networks if created
        std::string cleanup_cmd = "cd " + cfg_.sites_dir() + domain
            + " && docker compose down --volumes --remove-orphans 2>/dev/null || true";
        std::system(cleanup_cmd.c_str());
        // Remove the private Docker network by filter
        std::string net_cleanup = "docker network ls --filter name=containercp-site-"
            + std::to_string(site.id) + " -q | xargs -r docker network rm 2>/dev/null || true";
        std::system(net_cleanup.c_str());
        // Remove the site directory with all artifacts
        fs_.remove_directory(cfg_.sites_dir() + domain + "/");
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
        update_job_and_progress(jobs, job_id, 0, "Rolled back: " + result.message);
        return {false, result.message + " Created resources have been rolled back."};
    }

    // Create proxy config pointing to site web container via Docker network
    update_job_and_progress(jobs, job_id, 85, "Creating proxy configuration...");
    std::string upstream = "site-" + std::to_string(site.id) + "-web:80";
    proxy::ReverseProxy rp;
    rp.domain = domain;
    rp.site_id = site.id;
    rp.provider = "nginx";
    rp.upstream = upstream;
    rp.enabled = true;
    rp.status = "active";
    proxy_provider_.create_proxy(rp);

    update_job_and_progress(jobs, job_id, 95, "Reloading proxy...");
    proxy_provider_.reload();

    proxies_.create(domain, site.id, cfg_.data_root() + "/proxy/sites/" + domain + ".conf", upstream);

    update_job_and_progress(jobs, job_id, 100, "Site created successfully.");
    return {true, ""};
}

} // namespace containercp::operations
