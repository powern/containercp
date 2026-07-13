#include "NginxProxyProvider.h"

#include <set>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <thread>

namespace containercp::proxy {

NginxProxyProvider::NginxProxyProvider(filesystem::Filesystem& fs, config::Config& cfg,
                                       logger::Logger& logger, ssl::SslCertificateManager& ssl_mgr,
                                       proxy::ReverseProxyManager& proxy_mgr)
    : fs_(fs)
    , cfg_(cfg)
    , logger_(logger)
    , ssl_mgr_(ssl_mgr)
    , proxy_mgr_(proxy_mgr)
{
}

std::string NginxProxyProvider::config_path(const std::string& domain) const {
    return cfg_.data_root() + "/proxy/sites/" + domain + ".conf";
}

std::string NginxProxyProvider::proxy_name() const {
    return "containercp-proxy";
}

// Detect Docker bridge gateway IP for host access from inside containers.
// Tries host.docker.internal first, then falls back to gateway detection.
static std::string detect_host_gateway() {
    // Try host.docker.internal via getent (only works with --add-host flag)
    {
        std::string out_file = "/tmp/containercp-gateway-check.txt";
        std::string cmd1 = "getent hosts host.docker.internal 2>/dev/null > " + out_file;
        std::system(cmd1.c_str());
        std::ifstream in(out_file);
        std::string host_entry;
        std::getline(in, host_entry);
        std::remove(out_file.c_str());
        if (!host_entry.empty()) {
            auto space = host_entry.find(' ');
            if (space != std::string::npos) {
                return host_entry.substr(0, space);
            }
        }
    }
    // Fallback: parse docker bridge gateway from network inspect
    {
        std::string out_file = "/tmp/containercp-gateway-ip.txt";
        std::string cmd2 = "docker network inspect bridge --format '{{(index .IPAM.Config 0).Gateway}}' 2>/dev/null > " + out_file;
        std::system(cmd2.c_str());
        std::ifstream in(out_file);
        std::string gw;
        std::getline(in, gw);
        std::remove(out_file.c_str());
        if (!gw.empty()) return gw;
    }
    // Last resort: common Docker bridge gateway
    return "172.17.0.1";
}

// Replace this entire function with upstream resolution in a future refactor.
// Currently unused but kept for reference (Docker gateway resolved inline in
// ServiceRegistry::start()).

bool NginxProxyProvider::central_proxy_running() const {
    // Check if the container exists AND is actually in "running" state.
    // docker inspect with >/dev/null returns 0 even for Exited containers,
    // so we must check .State.Running explicitly.
    std::string out_file = "/tmp/containercp-proxy-status.txt";
    std::string cmd = "docker inspect --format '{{.State.Running}}' " + proxy_name()
                    + " 2>/dev/null > " + out_file;
    std::system(cmd.c_str());
    std::ifstream in(out_file);
    std::string state;
    std::getline(in, state);
    std::remove(out_file.c_str());
    return state == "true";
}

core::OperationResult NginxProxyProvider::create_proxy(const ReverseProxy& proxy) {
    if (proxy.upstream.empty()) {
        return {false, "upstream is required for proxy creation"};
    }
    std::string path = config_path(proxy.domain);
    fs_.create_directory(cfg_.data_root() + "/proxy/sites/");

    ProxyConfigBuilder::Params cfg_params;
    cfg_params.domain = proxy.domain;
    cfg_params.upstream = proxy.upstream;
    cfg_params.https = false;

    std::string config = config_builder_.build(cfg_params);
    fs_.create_file(path, config);
    logger_.info("PROXY", "Created config for " + proxy.domain + " upstream=" + proxy.upstream);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::remove_proxy(const std::string& domain) {
    std::string path = config_path(domain);
    if (fs_.exists(path)) {
        std::filesystem::remove(path);
        logger_.info("PROXY", "Removed config for " + domain);
    }
    return {true, ""};
}

core::OperationResult NginxProxyProvider::enable_proxy(const std::string& domain) {
    logger_.info("PROXY", "Enabled proxy for " + domain);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::disable_proxy(const std::string& domain) {
    logger_.info("PROXY", "Disabled proxy for " + domain);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::attach_certificate(const std::string& domain,
                                                               const std::string& cert_path,
                                                               const std::string& key_path,
                                                               bool redirect) {
    std::string cfg_path = config_path(domain);
    if (!fs_.exists(cfg_path)) {
        return {false, "Proxy config not found for " + domain};
    }

    // Resolve upstream from ReverseProxyManager (canonical source of truth)
    auto* rp = proxy_mgr_.find_by_domain(domain);
    if (!rp || rp->upstream.empty()) {
        return {false, domain + ": cannot attach certificate: upstream not found in ReverseProxyManager"};
    }
    std::string upstream = rp->upstream;
    logger_.info("PROXY", domain + ": canonical upstream=" + upstream);

    // Verify certificate files exist
    if (!fs_.exists(cert_path)) {
        return {false, domain + ": Certificate file not found: " + cert_path};
    }
    if (!fs_.exists(key_path)) {
        return {false, domain + ": Private key file not found: " + key_path};
    }

    // Build complete new config (HTTP + optionally HTTPS + optionally redirect)
    ProxyConfigBuilder::Params params;
    params.domain = domain;
    params.upstream = upstream;
    params.https = true;
    params.redirect = redirect;
    params.cert_path = cert_path;
    params.key_path = key_path;
    params.webmail_upstream = webmail_upstream_;

    std::string config = config_builder_.build(params);
    logger_.info("PROXY", domain + ": generated config (" + std::to_string(config.size()) + " bytes)");

    // Transactional write with nginx -t validation before reload
    std::string new_path = cfg_path + ".new";
    std::string bak_path = cfg_path + ".bak";

    std::filesystem::copy(cfg_path, bak_path, std::filesystem::copy_options::overwrite_existing);
    fs_.create_file(new_path, config);
    std::filesystem::rename(new_path, cfg_path);

    if (!validate_nginx_config(cfg_path)) {
        std::filesystem::rename(bak_path, cfg_path);
        logger_.error("PROXY", domain + ": nginx config validation failed, restored backup");
        return {false, domain + ": nginx config validation failed, rolled back"};
    }
    std::filesystem::remove(bak_path);

    auto reload_result = reload();
    if (!reload_result.success) {
        return {false, domain + ": Config updated but reload failed: " + reload_result.message};
    }

    logger_.info("PROXY", "HTTPS enabled for " + domain);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::detach_certificate(const std::string& domain) {
    std::string cfg_path = config_path(domain);
    if (!fs_.exists(cfg_path)) {
        return {true, ""};
    }

    // Resolve upstream from ReverseProxyManager
    auto* rp = proxy_mgr_.find_by_domain(domain);
    if (!rp || rp->upstream.empty()) {
        return {false, domain + ": cannot detach certificate: upstream not found"};
    }
    std::string upstream = rp->upstream;

    std::string config = config_builder_.build_http_block(domain, upstream);

    std::string new_path = cfg_path + ".detach_new";
    std::string bak_path = cfg_path + ".detach_bak";
    std::filesystem::copy(cfg_path, bak_path, std::filesystem::copy_options::overwrite_existing);
    fs_.create_file(new_path, config);
    std::filesystem::rename(new_path, cfg_path);

    if (!validate_nginx_config(cfg_path)) {
        std::filesystem::rename(bak_path, cfg_path);
        logger_.error("PROXY", domain + ": nginx config validation failed after detach, restored");
        return {false, domain + ": nginx config validation failed, rolled back"};
    }
    std::filesystem::remove(bak_path);

    auto reload_result = reload();
    if (!reload_result.success) {
        return {false, domain + ": Config updated but reload failed: " + reload_result.message};
    }

    logger_.info("PROXY", "HTTPS disabled for " + domain);
    return {true, ""};
}

bool NginxProxyProvider::validate_nginx_config(const std::string& cfg_path) const {
    (void)cfg_path;
    std::string out_file = "/tmp/containercp-nginx-check.txt";
    std::system(("docker exec " + proxy_name() + " nginx -t > " + out_file + " 2>&1").c_str());
    std::ifstream out_in(out_file);
    bool ok = true;
    if (out_in.is_open()) {
        std::string line;
        while (std::getline(out_in, line)) {
            if (line.find("test failed") != std::string::npos || line.find("emerg") != std::string::npos) {
                logger_.error("PROXY", "nginx: " + line);
                ok = false;
            } else if (line.find("test is successful") != std::string::npos) {
                logger_.info("PROXY", "nginx config valid");
            }
        }
    }
    std::remove(out_file.c_str());
    return ok;
}

core::OperationResult NginxProxyProvider::reload() {
    // Wait for container to be ready (especially after recreate)
    for (int i = 0; i < 30; ++i) {
        std::string status_file = "/tmp/containercp-proxy-status.txt";
        std::system(("docker inspect " + proxy_name() + " --format '{{.State.Status}}' > " + status_file + " 2>/dev/null").c_str());
        std::ifstream status_in(status_file);
        std::string status;
        std::getline(status_in, status);
        std::remove(status_file.c_str());
        if (status == "running") break;
        if (status == "exited" || status == "dead") {
            logger_.error("PROXY", "Container " + proxy_name() + " is " + status);
            // Log container logs for debugging
            std::system(("docker logs " + proxy_name() + " --tail 20 2>/dev/null || true").c_str());
            return {false, "Container " + proxy_name() + " is " + status};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::string out_file = "/tmp/containercp-nginx-reload.txt";
    std::string cmd = "docker exec " + proxy_name() + " nginx -s reload";
    int rc = std::system((cmd + " > " + out_file + " 2>&1").c_str());

    std::ifstream out_in(out_file);
    std::string error_msg;
    if (out_in.is_open()) {
        std::string line;
        while (std::getline(out_in, line)) {
            if (line.find("emerg") != std::string::npos || line.find("failed") != std::string::npos) {
                error_msg = line;
            }
        }
    }
    std::remove(out_file.c_str());

    if (rc != 0 || !error_msg.empty()) {
        logger_.error("PROXY", "Reload failed: " + (error_msg.empty() ? "exit code " + std::to_string(rc) : error_msg));
        return {false, "nginx reload failed: " + (error_msg.empty() ? "exit code " + std::to_string(rc) : error_msg)};
    }
    logger_.info("PROXY", "Reloaded");
    return {true, ""};
}

bool NginxProxyProvider::container_config_valid() const {
    // Check network mode
    {
        std::string f = "/tmp/containercp-proxy-mode.txt";
        std::system(("docker inspect " + proxy_name() + " --format '{{.HostConfig.NetworkMode}}' > " + f + " 2>/dev/null").c_str());
        std::ifstream in(f);
        std::string val;
        std::getline(in, val);
        std::remove(f.c_str());
        if (val == "host") {
            logger_.info("PROXY", "Validation failed: network mode is 'host', expected bridge");
            return false;
        }
    }
    // Check SSL mount
    {
        std::string f = "/tmp/containercp-proxy-ssl.txt";
        std::system(("docker inspect " + proxy_name() + " --format '{{range .Mounts}}{{.Source}}{{end}}' > " + f + " 2>/dev/null").c_str());
        std::ifstream in(f);
        std::string val;
        std::getline(in, val);
        std::remove(f.c_str());
        if (val.find(cfg_.data_root() + "/ssl") == std::string::npos) {
            logger_.info("PROXY", "Validation failed: SSL mount missing");
            return false;
        }
    }
    // Check port mapping for 80
    {
        std::string f = "/tmp/containercp-proxy-port.txt";
        std::system(("docker inspect " + proxy_name() + " --format '{{index .NetworkSettings.Ports \"80/tcp\"}}' > " + f + " 2>/dev/null").c_str());
        std::ifstream in(f);
        std::string val;
        std::getline(in, val);
        std::remove(f.c_str());
        if (val.empty() || val.find("HostPort") == std::string::npos) {
            logger_.info("PROXY", "Validation failed: port 80 mapping missing");
            return false;
        }
    }
    logger_.info("PROXY", "Container configuration is valid");
    return true;
}

core::OperationResult NginxProxyProvider::ensure_central_proxy() {
    // Step 1: check if the container exists at all (running or not)
    bool exists = false;
    {
        std::string check_file = "/tmp/containercp-proxy-exists.txt";
        std::system(("docker inspect " + proxy_name() + " > /dev/null 2>&1; echo $? > " + check_file).c_str());
        std::ifstream in(check_file);
        int ec = 1;
        in >> ec;
        std::remove(check_file.c_str());
        exists = (ec == 0);
    }

    if (exists) {
        logger_.info("PROXY", "Container " + proxy_name() + " exists");

        // Determine container state
        bool running = central_proxy_running();
        logger_.info("PROXY", "Container state: " + std::string(running ? "Running" : "Exited/stopped"));

        // Validate configuration regardless of state (docker inspect works on stopped containers)
        logger_.info("PROXY", "Running configuration validation...");
        bool config_ok = container_config_valid();

        if (running && config_ok) {
            logger_.info("PROXY", "Central proxy already running and valid");
            return {true, ""};
        }

        if (config_ok && !running) {
            // Configuration is valid, container just needs to be started
            logger_.info("PROXY", "Validation OK. Starting existing container...");
            int start_rc = std::system(("docker start " + proxy_name() + " > /dev/null 2>&1").c_str());
            if (start_rc == 0) {
                for (int i = 0; i < 30; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    if (central_proxy_running()) {
                        logger_.info("PROXY", "Container started successfully");
                        return {true, "Container started"};
                    }
                }
                logger_.warning("PROXY", "docker start timed out waiting for running state");
            } else {
                logger_.warning("PROXY", "docker start command failed");
            }
        }

        // If we reach here, either config is invalid or start failed — recreate
        logger_.info("PROXY", "Recreating container...");
        std::system(("docker rm -f " + proxy_name() + " > /dev/null 2>&1").c_str());
    }

    // Container doesn't exist or was removed — create fresh
    logger_.info("PROXY", "Creating central proxy container");
    fs_.create_directory(cfg_.data_root() + "/proxy/");
    fs_.create_directory(cfg_.data_root() + "/proxy/sites/");

    std::string net_cmd = "docker network inspect containercp-public > /dev/null 2>&1"
        " || docker network create containercp-public > /dev/null 2>&1";
    std::system(net_cmd.c_str());

    std::string default_conf = cfg_.data_root() + "/proxy/sites/00-default.conf";
    if (!fs_.exists(default_conf)) {
        std::ostringstream conf;
        conf << "server {\n"
             << "    listen 80 default_server;\n"
             << "    server_name _;\n"
             << "    return 404;\n"
             << "}\n";
        fs_.create_file(default_conf, conf.str());
    }

    std::string cmd = "docker run -d --name " + proxy_name()
        + " --restart unless-stopped"
        + " --network containercp-public"
        + " --add-host host.docker.internal:host-gateway"
        + " -p 80:80 -p 443:443"
        + " -v " + cfg_.data_root() + "/proxy/sites/:/etc/nginx/conf.d/"
        + " -v " + cfg_.data_root() + "/ssl/:/srv/containercp/ssl/:ro"
        + " nginx:alpine > /dev/null 2>&1";

    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        logger_.error("PROXY", "Failed to create central proxy container");
        return {false, "Failed to create central proxy container"};
    }

    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (central_proxy_running()) {
            logger_.info("PROXY", "Container ready after " + std::to_string((i+1)*500) + "ms");
            return {true, "Central proxy created and running"};
        }
    }
    logger_.warning("PROXY", "Container created but not yet running. Will retry on reload.");
    return {true, "Central proxy created (waiting for container)"};
}

core::OperationResult NginxProxyProvider::sync_all_proxies(
    const std::vector<ReverseProxy>& all_proxies,
    ssl::CertificateStore& cert_store) {

    logger_.info("PROXY", "Starting declarative proxy sync (" + std::to_string(all_proxies.size()) + " entries)");

    // Phase 1: collect expected config files
    std::set<std::string> expected_configs;
    for (const auto& p : all_proxies) {
        expected_configs.insert(config_path(p.domain));
    }

    // Phase 2: remove orphan config files
    std::string sites_dir = cfg_.data_root() + "/proxy/sites/";
    if (fs_.exists(sites_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(sites_dir)) {
            std::string path = entry.path().string();
            if (path.find(".conf") == std::string::npos) continue;
            if (expected_configs.find(path) == expected_configs.end()) {
                // Orphan config — remove it
                std::filesystem::remove(path);
                logger_.info("PROXY", "Removed orphan proxy config: " + path);
            }
        }
    }

    // Phase 3: generate/regenerate each proxy config with correct HTTPS if cert exists
    int generated = 0, errors = 0;
    for (const auto& p : all_proxies) {
        ProxyConfigBuilder::Params params;
        params.domain = p.domain;
        params.upstream = p.upstream;
        params.webmail_upstream = webmail_upstream_;

        // Check if SSL certificate exists for this site
        if (p.site_id > 0) {
            auto load_result = cert_store.load_metadata(p.site_id);
            if (load_result.success && load_result.metadata.status == "active" && load_result.metadata.https_enabled) {
                params.https = true;
                params.redirect = load_result.metadata.redirect_enabled;
                params.cert_path = cert_store.fullchain_path(p.site_id);
                params.key_path = cert_store.privkey_path(p.site_id);

                // Verify certificate files exist
                if (!fs_.exists(params.cert_path) || !fs_.exists(params.key_path)) {
                    logger_.warning("PROXY", p.domain + ": cert files missing, falling back to HTTP");
                    params.https = false;
                }
            }
        }

        std::string config = config_builder_.build(params);
        std::string path = config_path(p.domain);
        fs_.create_directory(sites_dir);
        fs_.create_file(path, config);
        generated++;
    }

    // Phase 4: validate all configs
    bool config_valid = true;
    for (const auto& p : all_proxies) {
        if (!validate_nginx_config(config_path(p.domain))) {
            logger_.error("PROXY", "Config validation failed for " + p.domain);
            config_valid = false;
            errors++;
        }
        // Verify upstream container exists (best-effort)
        if (p.upstream.find("site-") == 0) {
            std::string container_name = p.upstream.substr(0, p.upstream.find(':'));
            auto check = executor_.run({"docker", "inspect", container_name, "--format", "{{.State.Running}}"});
            if (check.exit_code != 0) {
                logger_.warning("PROXY", p.domain + ": upstream container " + container_name + " not found");
            }
        }
    }

    if (!config_valid) {
        logger_.error("PROXY", "Declarative sync completed with " + std::to_string(errors) + " config errors");
        return {false, std::to_string(errors) + " proxy config(s) have validation errors"};
    }

    // Phase 5: reload nginx
    auto reload_result = reload();
    if (!reload_result.success) {
        return {false, "Proxy reload failed after sync: " + reload_result.message};
    }

    logger_.info("PROXY", "Declarative sync completed: " + std::to_string(generated) + " configs generated, 0 orphans");
    return {true, "Proxy sync completed"};
}

void NginxProxyProvider::set_webmail_upstream(const std::string& upstream) {
    webmail_upstream_ = upstream;
}

core::OperationResult NginxProxyProvider::test_config() {
    std::lock_guard<std::mutex> lock(config_cache_mutex_);
    if (!central_proxy_running()) {
        cached_test_ = {false, "Proxy container is not running"};
        return cached_test_;
    }
    auto result = executor_.run({
        "docker", "exec", proxy_name(), "nginx", "-t"
    });
    bool ok = (result.exit_code == 0);
    if (ok) {
        logger_.info("PROXY", "nginx config test passed");
        cached_test_ = {true, "nginx configuration is valid"};
    } else {
        // Sanitize: use the last line of stderr as the error, avoid leaking paths
        std::string err = result.err;
        // Strip file paths from error message (e.g. "/etc/nginx/nginx.conf:" -> "nginx.conf:")
        std::string sanitized;
        size_t pos = 0;
        while (pos < err.size()) {
            auto slash = err.find('/', pos);
            if (slash == std::string::npos || slash > err.size() - 2) break;
            auto space = err.find(' ', slash);
            auto colon = err.find(':', slash);
            size_t end = (space < colon) ? space : colon;
            if (end != std::string::npos && end > slash) {
                sanitized += err.substr(pos, slash - pos) + err.substr(end);
                pos = err.find('\n', end);
                if (pos == std::string::npos) break;
                ++pos;
            } else {
                sanitized += err[pos++];
            }
        }
        logger_.warning("PROXY", "nginx config test failed");
        cached_test_ = {false, "nginx configuration test failed"};
    }
    return cached_test_;
}

core::OperationResult NginxProxyProvider::last_test_result() const {
    std::lock_guard<std::mutex> lock(config_cache_mutex_);
    return cached_test_;
}

core::OperationResult NginxProxyProvider::remove_central_proxy() {
    std::string cmd = "docker rm -f " + proxy_name() + " > /dev/null 2>&1";
    std::system(cmd.c_str());
    logger_.info("PROXY", "Central proxy container removed");
    return {true, ""};
}

core::OperationResult NginxProxyProvider::status(const std::string& domain) {
    (void)domain;
    return {true, ""};
}

} // namespace containercp::proxy
