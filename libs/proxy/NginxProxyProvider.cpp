#include "NginxProxyProvider.h"

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

bool NginxProxyProvider::central_proxy_running() const {
    std::string cmd = "docker inspect " + proxy_name() + " > /dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc == -1) return false;
    int exit_code = WEXITSTATUS(rc);
    return exit_code == 0;
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
                                                               const std::string& key_path) {
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

    // Build complete new config (HTTP + HTTPS blocks)
    ProxyConfigBuilder::Params params;
    params.domain = domain;
    params.upstream = upstream;
    params.https = true;
    params.cert_path = cert_path;
    params.key_path = key_path;

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

core::OperationResult NginxProxyProvider::ensure_central_proxy() {
    if (central_proxy_running()) {
        // Check network mode (host mode is old RC1 style)
        std::string mode_file = "/tmp/containercp-proxy-mode.txt";
        std::system(("docker inspect " + proxy_name() + " --format '{{.HostConfig.NetworkMode}}' > " + mode_file + " 2>/dev/null").c_str());
        std::ifstream mode_in(mode_file);
        std::string network_mode;
        std::getline(mode_in, network_mode);
        std::remove(mode_file.c_str());

        bool needs_recreate = false;
        if (network_mode == "host") {
            logger_.info("PROXY", "Old proxy on host network, recreating");
            needs_recreate = true;
        }

        // Check SSL mount
        if (!needs_recreate) {
            std::string ssl_check = "docker inspect " + proxy_name()
                + " --format '{{range .Mounts}}{{.Source}}{{end}}' 2>/dev/null";
            std::string ssl_file = "/tmp/containercp-proxy-ssl.txt";
            std::system((ssl_check + " > " + ssl_file + " 2>/dev/null").c_str());
            std::ifstream ssl_in(ssl_file);
            std::string mounts;
            std::getline(ssl_in, mounts);
            std::remove(ssl_file.c_str());

            if (mounts.find(cfg_.data_root() + "/ssl") == std::string::npos) {
                logger_.info("PROXY", "SSL mount missing, recreating proxy container");
                needs_recreate = true;
            }
        }

        // Check port mapping
        if (!needs_recreate) {
            std::string port_file = "/tmp/containercp-proxy-port.txt";
            std::system(("docker inspect " + proxy_name() + " --format '{{index .NetworkSettings.Ports \"80/tcp\"}}' > " + port_file + " 2>/dev/null").c_str());
            std::ifstream port_in(port_file);
            std::string port_info;
            std::getline(port_in, port_info);
            std::remove(port_file.c_str());

            if (port_info.empty() || port_info.find("HostPort") == std::string::npos) {
                logger_.info("PROXY", "Port mapping missing, recreating proxy container");
                needs_recreate = true;
            }
        }

        if (needs_recreate) {
            std::system(("docker rm -f " + proxy_name() + " > /dev/null 2>&1").c_str());
        } else {
            logger_.info("PROXY", "Central proxy already running");
            return {true, ""};
        }
    }

    fs_.create_directory(cfg_.data_root() + "/proxy/");
    fs_.create_directory(cfg_.data_root() + "/proxy/sites/");

    // Ensure shared public network
    std::string net_cmd = "docker network inspect containercp-public > /dev/null 2>&1"
        " || docker network create containercp-public > /dev/null 2>&1";
    std::system(net_cmd.c_str());

    // Default 404 config
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

    // Mount SSL directory so nginx can read certificate files
    // Add host.docker.internal for admin panel access to host's port 8081
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

    // Wait for container to be running
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::string status_file = "/tmp/containercp-proxy-status.txt";
        std::system(("docker inspect " + proxy_name() + " --format '{{.State.Status}}' > " + status_file + " 2>/dev/null").c_str());
        std::ifstream status_in(status_file);
        std::string status;
        std::getline(status_in, status);
        std::remove(status_file.c_str());
        if (status == "running") {
            logger_.info("PROXY", "Container ready after " + std::to_string((i+1)*500) + "ms");
            return {true, "Central proxy created and running"};
        }
    }
    logger_.warning("PROXY", "Container created but not yet running. Will retry on reload.");
    return {true, "Central proxy created (waiting for container)"};
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
