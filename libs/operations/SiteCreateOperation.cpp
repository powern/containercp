#include "SiteCreateOperation.h"
#include "logger/Logger.h"
#include "operations/SiteDatabaseVolumeGuard.h"
#include "utils/PasswordGenerator.h"
#include "utils/StringUtils.h"
#include "utils/Validator.h"
#include "runtime/CommandExecutor.h"

#include <iostream>
#include <sstream>

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

struct CreateState {
    bool site_record_created = false;
    bool domain_record_created = false;
    bool database_record_created = false;
    uint64_t site_id = 0;
    std::string site_dir;
    std::string domain;
};

static void do_rollback(CreateState& st,
                        site::SiteManager& sites_,
                        domain::DomainManager& domains_,
                        database::DatabaseManager& databases_,
                        proxy::ReverseProxyManager& proxies_,
                        proxy::ProxyProvider& proxy_provider_,
                        filesystem::Filesystem& fs_,
                        config::Config& cfg_) {
    (void)cfg_;

    // 1. Remove proxy config if it was created
    proxy_provider_.remove_proxy(st.domain);
    auto* rp = proxies_.find_by_domain(st.domain);
    if (rp != nullptr) proxies_.remove(rp->id);

    // 2. Stop Docker stack while docker-compose.yml still exists
    if (!st.site_dir.empty()) {
        runtime::CommandExecutor exec;
        // docker compose down — ignore errors, best effort
        exec.run({"docker", "compose", "-f", st.site_dir + "docker-compose.yml",
                  "down", "--volumes", "--remove-orphans"});
    }

    // 3. Remove containers by label (belt and suspenders)
    if (st.site_id > 0) {
        runtime::CommandExecutor exec;
        auto container_list = exec.run({
            "docker", "ps", "-a", "--filter",
            "label=containercp.site.id=" + std::to_string(st.site_id),
            "--format", "{{.ID}}"
        });
        if (container_list.exit_code == 0 && !container_list.out.empty()) {
            std::istringstream ss(container_list.out);
            std::string cid;
            while (std::getline(ss, cid)) {
                if (!cid.empty()) exec.run({"docker", "rm", "-f", cid});
            }
        }
    }

    // 4. Remove Docker network by project label
    if (st.site_id > 0) {
        runtime::CommandExecutor exec;
        auto net_list = exec.run({
            "docker", "network", "ls", "--filter",
            "label=com.docker.compose.project=containercp-site-" + std::to_string(st.site_id),
            "--format", "{{.ID}}"
        });
        if (net_list.exit_code == 0 && !net_list.out.empty()) {
            std::istringstream ss(net_list.out);
            std::string nid;
            while (std::getline(ss, nid)) {
                if (!nid.empty()) exec.run({"docker", "network", "rm", nid});
            }
        }
        // Fallback: remove by name pattern
        auto nets_by_name = exec.run({
            "docker", "network", "ls", "--filter",
            "name=containercp-site-" + std::to_string(st.site_id),
            "--format", "{{.ID}}"
        });
        if (nets_by_name.exit_code == 0 && !nets_by_name.out.empty()) {
            std::istringstream ns(nets_by_name.out);
            std::string nid;
            while (std::getline(ns, nid)) {
                if (!nid.empty()) exec.run({"docker", "network", "rm", nid});
            }
        }
    }

    // 5. Remove Docker volumes by compose project name
    if (st.site_id > 0) {
        runtime::CommandExecutor exec;
        auto vol_list = exec.run({
            "docker", "volume", "ls", "--filter",
            "label=com.docker.compose.project=containercp-site-" + std::to_string(st.site_id),
            "--format", "{{.Name}}"
        });
        if (vol_list.exit_code == 0 && !vol_list.out.empty()) {
            std::istringstream ss(vol_list.out);
            std::string vname;
            while (std::getline(ss, vname)) {
                if (!vname.empty()) exec.run({"docker", "volume", "rm", vname});
            }
        }
    }

    // 6. Remove site directory (only after Docker cleanup)
    if (!st.site_dir.empty()) {
        fs_.remove_directory(st.site_dir);
    }

    // 7. Remove in-memory records (reverse order)
    if (st.database_record_created && st.site_id > 0) {
        for (const auto& d : databases_.list()) {
            if (d.site_id == st.site_id) {
                databases_.remove(d.id);
                break;
            }
        }
    }
    if (st.domain_record_created && st.site_id > 0) {
        for (const auto& d : domains_.list()) {
            if (d.site_id == st.site_id) {
                domains_.remove(d.id);
                break;
            }
        }
    }
    if (st.site_record_created && st.site_id > 0) {
        sites_.remove(st.site_id);
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

    CreateState st;
    st.domain = domain;

    update_job_and_progress(jobs, job_id, 10, "Creating site record...");

    site::Site site;
    site.name = domain;
    site.domain = domain;
    site.owner = owner;
    site.node_id = node.id;
    site.web_server = web_server;

    site.id = sites_.create(domain, owner, node.id, web_server);
    st.site_id = site.id;
    st.site_record_created = true;

    CommandExecutorDockerRunner docker_runner;
    auto volume_check = ensure_database_volume_absent_for_create(docker_runner, domain, site.id);
    if (!volume_check.success) {
        logger::Logger::instance().warning("SITE_CREATE", "database volume collision refused domain=" + domain + " site_id=" + std::to_string(site.id));
        do_rollback(st, sites_, domains_, databases_, proxies_,
                    proxy_provider_, fs_, cfg_);
        update_job_and_progress(jobs, job_id, 0, "Refused stale database volume reuse");
        return volume_check;
    }

    update_job_and_progress(jobs, job_id, 15, "Creating domain record...");
    domains_.create(domain, 0, site.id);
    st.domain_record_created = true;

    update_job_and_progress(jobs, job_id, 20, "Creating database...");
    std::string safe = utils::StringUtils::sanitize(domain);
    std::string db_name = safe + "_db";
    std::string db_user = safe + "_user";
    std::string db_password = utils::PasswordGenerator::generate();
    databases_.create(db_name, db_user, db_password, 0, site.id);
    st.database_record_created = true;

    site.db_name = db_name;
    site.db_user = db_user;
    site.db_password = db_password;

    st.site_dir = cfg_.sites_dir() + domain + "/";

    auto progress = [jobs, job_id](int percent, const std::string& step) {
        update_job_and_progress(jobs, job_id, percent, step);
    };

    auto result = provider_.create_site(site, progress);

    if (!result.success) {
        do_rollback(st, sites_, domains_, databases_, proxies_,
                    proxy_provider_, fs_, cfg_);
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

    auto create_result = proxy_provider_.create_proxy(rp);
    if (!create_result.success) {
        do_rollback(st, sites_, domains_, databases_, proxies_,
                    proxy_provider_, fs_, cfg_);
        update_job_and_progress(jobs, job_id, 0, "Rolled back: " + create_result.message);
        return {false, create_result.message + " Created resources have been rolled back."};
    }

    update_job_and_progress(jobs, job_id, 95, "Reloading proxy...");
    auto reload_result = proxy_provider_.reload();
    if (!reload_result.success) {
        do_rollback(st, sites_, domains_, databases_, proxies_,
                    proxy_provider_, fs_, cfg_);
        update_job_and_progress(jobs, job_id, 0, "Rolled back: " + reload_result.message);
        return {false, reload_result.message + " Created resources have been rolled back."};
    }

    proxies_.create(domain, site.id, cfg_.data_root() + "/proxy/sites/" + domain + ".conf", upstream);

    update_job_and_progress(jobs, job_id, 100, "Site created successfully.");
    return {true, ""};
}

} // namespace containercp::operations
