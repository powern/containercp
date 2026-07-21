// Integration test: SiteCreateOperation rollback
//
// Tests what resources remain when SiteCreateOperation fails.
// Uses real components: DockerComposeProvider, DockerRuntime, etc.
// No mocks — tests actual system behavior.

#include "site/SiteManager.h"
#include "domain/DomainManager.h"
#include "database/DatabaseManager.h"
#include "operations/SiteCreateOperation.h"
#include "provider/DockerComposeProvider.h"
#include "runtime/DockerRuntime.h"
#include "config/Config.h"
#include "filesystem/Filesystem.h"
#include "proxy/ReverseProxyManager.h"
#include "proxy/ProxyProvider.h"
#include "node/Node.h"
#include "php/PhpVersionManager.h"
#include "profile/ProfileManager.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "doctest/doctest.h"

using namespace containercp;

TEST_CASE("SiteCreateOperation rollback — what remains after failure") {
    auto& log = logger::Logger::instance();
    config::Config& cfg = config::Config::instance();
    filesystem::Filesystem fs;
    runtime::CommandExecutor exec;
    php::PhpVersionManager php_mgr;
    profile::ProfileManager prof_mgr;

    runtime::DockerRuntime rt(log, cfg.sites_dir());
    provider::DockerComposeProvider provider(fs, cfg, php_mgr, rt, prof_mgr);

    site::SiteManager sites;
    domain::DomainManager domains;
    database::DatabaseManager databases;
    proxy::ReverseProxyManager proxies;

    site::Site high_water;
    high_water.id = 100000 + static_cast<uint64_t>(::getpid() % 100000);
    high_water.domain = "reserved-rollback-id.local";
    high_water.name = high_water.domain;
    high_water.owner = "system";
    high_water.node_id = 1;
    sites.set_sites({high_water});

    // Proxy provider that returns error — simulates failure after site creation
    class ErrorProxyProvider : public proxy::ProxyProvider {
    public:
        core::OperationResult create_proxy(const proxy::ReverseProxy&) override {
            return {false, "Intentional proxy failure for rollback test"};
        }
        core::OperationResult remove_proxy(const std::string&) override { return {true, ""}; }
        core::OperationResult enable_proxy(const std::string&) override { return {false, ""}; }
        core::OperationResult disable_proxy(const std::string&) override { return {false, ""}; }
        core::OperationResult reload() override { return {true, ""}; }
        core::OperationResult status(const std::string&) override { return {true, ""}; }
    };
    ErrorProxyProvider error_proxy;

    operations::SiteCreateOperation op(sites, domains, databases,
                                        proxies, error_proxy, fs, cfg, provider);

    std::string test_domain = "rollback-test-" + std::to_string(::getpid()) + ".local";
    node::Node local_node;
    local_node.id = 1;
    local_node.name = "local";

    std::string site_dir = cfg.sites_dir() + test_domain + "/";

    // Docker containers before
    auto docker_ps_before = exec.run({"docker", "ps", "-a", "--filter", "name=" + test_domain, "--format", "{{.Names}}"});
    int containers_before = 0;
    if (docker_ps_before.exit_code == 0) {
        std::istringstream ss(docker_ps_before.out);
        std::string line;
        while (std::getline(ss, line)) { if (!line.empty()) containers_before++; }
    }

    // Docker network before
    auto net_before = exec.run({"docker", "network", "ls", "--filter", "name=containercp-site-", "--format", "{{.Name}}"});
    int nets_before = 0;
    if (net_before.exit_code == 0) {
        std::istringstream ss(net_before.out);
        std::string line;
        while (std::getline(ss, line)) { if (!line.empty()) nets_before++; }
    }

    // ── Execute ──
    auto result = op.execute("admin", test_domain, local_node, false, "", nullptr, 0);

    // ── Check after ──
    bool site_after = sites.find(test_domain) != nullptr;
    bool domain_after = domains.find(test_domain) != nullptr;
    bool dir_after = fs.exists(site_dir);

    // Docker containers
    auto docker_ps = exec.run({"docker", "ps", "-a", "--filter", "name=" + test_domain, "--format", "{{.Names}}"});
    int containers_after = 0;
    std::string containers_names;
    if (docker_ps.exit_code == 0) {
        std::istringstream ss(docker_ps.out);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty()) { containers_after++; containers_names += line + " "; }
        }
    }

    // Docker volumes
    auto vols = exec.run({"docker", "volume", "ls", "--filter", "name=" + test_domain, "--format", "{{.Name}}"});
    int vols_after = 0;
    std::string vols_names;
    if (vols.exit_code == 0) {
        std::istringstream ss(vols.out);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty()) { vols_after++; vols_names += line + " "; }
        }
    }

    // Docker network — check for the specific site ID (the test site-xxx may have site_id)
    // After rollback, site_id was the last auto-incremented ID. We don't know the exact value,
    // so check all containercp-site-N networks but only flag those that had containers
    auto nets_check = exec.run({"docker", "network", "ls", "--filter", "name=containercp-site-", "--format", "{{.Name}}"});
    std::string net_names_after;
    std::string our_net_name = "containercp-site-"; // partial match
    if (nets_check.exit_code == 0) {
        std::istringstream ss(nets_check.out);
        std::string line;
        while (std::getline(ss, line)) { if (!line.empty()) net_names_after += line + " "; }
    }
    // Only report as leaked if there are containers to go with it (means recent leak)
    bool any_net_after = !net_names_after.empty() && containers_after > 0;

    // nginx config
    std::string nginx_cfg = cfg.data_root() + "/proxy/sites/" + test_domain + ".conf";
    bool nginx_after = fs.exists(nginx_cfg);

    // compose file
    bool compose_after = fs.exists(site_dir + "docker-compose.yml");
    bool env_after = fs.exists(site_dir + ".env");

    // ═══════════════════════════════════════════════════════════
    // REPORT
    // ═══════════════════════════════════════════════════════════
    log.info("ROLLBACK_TEST", "=============================================");
    log.info("ROLLBACK_TEST", "SiteCreateOperation rollback test");
    log.info("ROLLBACK_TEST", "Domain: " + test_domain);
    log.info("ROLLBACK_TEST", "Result: " + std::string(result.success ? "SUCCESS" : "FAILED: " + result.message));
    log.info("ROLLBACK_TEST", "---------------------------------------------");

    bool has_docker = (exec.run({"docker", "info", "--format", "{{.ServerVersion}}"}).exit_code == 0);
    if (!has_docker) {
        log.info("ROLLBACK_TEST", "Docker unavailable — testing rollback path only");
    }

    int leaked = 0;
    auto check_leak = [&](const std::string& name, bool leaked_flag) {
        log.info("ROLLBACK_TEST", std::string(leaked_flag ? "  ❌ LEAKED" : "  ✅ CLEAN") + "  " + name);
        if (leaked_flag) leaked++;
    };

    check_leak("Site record (in-memory)", site_after);
    check_leak("Domain record (in-memory)", domain_after);
    check_leak("Site directory: " + site_dir, dir_after);
    check_leak("docker-compose.yml", compose_after);
    check_leak(".env file", env_after);
    check_leak("nginx config: " + nginx_cfg, nginx_after);
    check_leak("Docker containers: " + containers_names, containers_after > 0);
    check_leak("Docker volumes: " + vols_names, vols_after > 0);
    check_leak("Docker networks: " + net_names_after, any_net_after);

    log.info("ROLLBACK_TEST", "---------------------------------------------");
    if (leaked == 0) {
        log.info("ROLLBACK_TEST", "✅ Rollback complete — no resources leaked");
    } else {
        log.info("ROLLBACK_TEST", "❌ " + std::to_string(leaked) + " resource(s) leaked");
    }
    log.info("ROLLBACK_TEST", "=============================================");

    // ═══════════════════════════════════════════════════════════
    // CLEANUP
    // ═══════════════════════════════════════════════════════════
    if (containers_after > 0 || dir_after) {
        exec.run({"docker", "compose", "-p", test_domain, "down", "--volumes", "--remove-orphans"});
        exec.run({"sh", "-c", "docker ps -a --filter name=" + test_domain + " -q | xargs -r docker rm -f 2>/dev/null"});
        exec.run({"sh", "-c", "docker network ls --filter name=containercp-site- -q | xargs -r docker network rm 2>/dev/null"});
        fs.remove_directory(site_dir);
    }

    auto* leaked_site = sites.find(test_domain);
    if (leaked_site) {
        for (const auto& d : databases.list()) {
            if (d.site_id == leaked_site->id) databases.remove(d.id);
        }
        for (const auto& d : domains.list()) {
            if (d.site_id == leaked_site->id) domains.remove(d.id);
        }
        sites.remove(leaked_site->id);
    }

    if (nginx_after) std::remove(nginx_cfg.c_str());
}
