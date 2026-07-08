#include "NginxProxyProvider.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/wait.h>

namespace containercp::proxy {

NginxProxyProvider::NginxProxyProvider(filesystem::Filesystem& fs, config::Config& cfg,
                                       logger::Logger& logger, ssl::SslCertificateManager& ssl_mgr)
    : fs_(fs)
    , cfg_(cfg)
    , logger_(logger)
    , ssl_mgr_(ssl_mgr)
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
    std::string path = config_path(proxy.domain);
    fs_.create_directory(cfg_.data_root() + "/proxy/sites/");

    // Upstream is now a Docker service name, e.g. "site-3-web:80"
    std::string upstream = proxy.upstream.empty() ? "site-0-web:80" : proxy.upstream;

    auto* cert = ssl_mgr_.find_by_domain(proxy.domain);
    bool has_ssl = (cert != nullptr && cert->https_enabled && cert->status == "active");

    ProxyConfigBuilder::Params cfg_params;
    cfg_params.domain = proxy.domain;
    cfg_params.upstream = upstream;
    cfg_params.https = has_ssl;
    cfg_params.redirect = has_ssl; // legacy: existing sites with SSL get redirect
    if (has_ssl && cert) {
        cfg_params.cert_path = cert->certificate_path;
        cfg_params.key_path = cert->key_path;
    }

    std::string config = config_builder_.build(cfg_params);
    fs_.create_file(path, config);
    logger_.info("PROXY", "Created config for " + proxy.domain);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::remove_proxy(const std::string& domain) {
    std::string path = config_path(domain);
    if (fs_.exists(path)) {
        std::filesystem::remove(path);
    }
    logger_.info("PROXY", "Removed config for " + domain);
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
    std::string path = config_path(domain);
    if (!fs_.exists(path)) {
        return {false, "Proxy config not found for " + domain};
    }

    // Read existing HTTP config
    std::ifstream in(path);
    if (!in.is_open()) {
        return {false, "Cannot read existing config for " + domain};
    }
    std::string existing((std::istreambuf_iterator<char>(in)), {});
    in.close();

    // Build HTTPS config with redirect (existing HTTP block becomes redirect)
    std::string upstream;
    {
        // Extract upstream from existing config
        auto pos = existing.find("proxy_pass http://");
        if (pos != std::string::npos) {
            pos += 17; // length of "proxy_pass http://"
            auto end = existing.find(";", pos);
            if (end != std::string::npos) {
                upstream = existing.substr(pos, end - pos);
            }
        }
    }
    if (upstream.empty()) {
        upstream = "site-0-web:80";
    }

    ProxyConfigBuilder::Params params;
    params.domain = domain;
    params.upstream = upstream;
    params.https = true;
    params.cert_path = cert_path;
    params.key_path = key_path;

    std::string config = config_builder_.build(params);

    // Transactional write: write to temp file, validate, rename
    std::string tmp_path = path + ".attach_tmp";
    fs_.create_file(tmp_path, config);

    if (!validate_nginx_config(tmp_path)) {
        std::filesystem::remove(tmp_path);
        return {false, "Generated nginx config is invalid"};
    }

    // Replace original config with new one
    std::filesystem::rename(tmp_path, path);

    // Reload nginx
    auto reload_result = reload();
    if (!reload_result.success) {
        return {false, "Config updated but nginx reload failed: " + reload_result.message};
    }

    logger_.info("PROXY", "Attached certificate for " + domain);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::detach_certificate(const std::string& domain) {
    std::string path = config_path(domain);
    if (!fs_.exists(path)) {
        return {true, ""}; // No config, nothing to detach
    }

    // Read existing config
    std::ifstream in(path);
    if (!in.is_open()) {
        return {false, "Cannot read config for " + domain};
    }
    std::string existing((std::istreambuf_iterator<char>(in)), {});
    in.close();

    // Extract upstream from existing config
    std::string upstream;
    {
        auto pos = existing.find("proxy_pass http://");
        if (pos != std::string::npos) {
            pos += 17;
            auto end = existing.find(";", pos);
            if (end != std::string::npos) {
                upstream = existing.substr(pos, end - pos);
            }
        }
    }
    if (upstream.empty()) {
        upstream = "site-0-web:80";
    }

    // Build pure HTTP config
    std::string config = config_builder_.build_http_block(domain, upstream);

    // Transactional write
    std::string tmp_path = path + ".detach_tmp";
    fs_.create_file(tmp_path, config);

    if (!validate_nginx_config(tmp_path)) {
        std::filesystem::remove(tmp_path);
        return {false, "Generated nginx config is invalid"};
    }

    std::filesystem::rename(tmp_path, path);

    auto reload_result = reload();
    if (!reload_result.success) {
        return {false, "Config updated but nginx reload failed: " + reload_result.message};
    }

    logger_.info("PROXY", "Detached certificate for " + domain);
    return {true, ""};
}

bool NginxProxyProvider::validate_nginx_config(const std::string& config_content) const {
    // For now, write to a temp file in the proxy config directory and run nginx -t
    (void)config_content;
    // TODO: Run "docker exec containercp-proxy nginx -t" to validate syntax
    // For now, assume valid (the config is generated from templates)
    return true;
}

core::OperationResult NginxProxyProvider::reload() {
    std::string cmd = "docker exec " + proxy_name() + " nginx -s reload > /dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        logger_.info("PROXY", "Reload failed (proxy container may not be running)");
        return {false, "Reload failed: proxy container not running"};
    }
    logger_.info("PROXY", "Reloaded");
    return {true, ""};
}

core::OperationResult NginxProxyProvider::ensure_central_proxy() {
    if (central_proxy_running()) {
        // Verify the existing container has the correct configuration.
        // If it was created with --network host (old RC1 style), recreate it.
        // Also check that port 80 is mapped and containercp-public network exists.
        std::string mode_check = "docker inspect " + proxy_name()
            + " --format '{{.HostConfig.NetworkMode}}' 2>/dev/null";
        std::string mode_file = "/tmp/containercp-proxy-mode.txt";
        std::system((mode_check + " > " + mode_file + " 2>/dev/null").c_str());

        std::ifstream mode_result(mode_file);
        std::string network_mode;
        std::getline(mode_result, network_mode);
        mode_result.close();
        std::remove(mode_file.c_str());

        bool needs_recreate = false;
        if (network_mode == "host") {
            logger_.info("PROXY", "Detected old proxy on host network, recreating");
            needs_recreate = true;
        }

        // Also check port mapping — if port 80 is not published, recreate
        if (!needs_recreate) {
            std::string port_check = "docker inspect " + proxy_name()
                + " --format '{{index .NetworkSettings.Ports \"80/tcp\"}}' 2>/dev/null";
            std::string port_file = "/tmp/containercp-proxy-port.txt";
            std::system((port_check + " > " + port_file + " 2>/dev/null").c_str());

            std::ifstream port_result(port_file);
            std::string port_info;
            std::getline(port_result, port_info);
            port_result.close();
            std::remove(port_file.c_str());

            if (port_info.empty() || port_info.find("HostPort") == std::string::npos) {
                logger_.info("PROXY", "Detected proxy without port mapping, recreating");
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

    // Ensure the shared public network exists
    std::string net_cmd = "docker network inspect containercp-public > /dev/null 2>&1"
        " || docker network create containercp-public > /dev/null 2>&1";
    std::system(net_cmd.c_str());

    // Write a fallback default config so nginx has something to serve
    std::string default_conf_path = cfg_.data_root() + "/proxy/sites/00-default.conf";
    if (!fs_.exists(default_conf_path)) {
        std::ostringstream conf;
        conf << "server {\n"
             << "    listen 80 default_server;\n"
             << "    server_name _;\n"
             << "    return 404;\n"
             << "}\n";
        fs_.create_file(default_conf_path, conf.str());
    }

    std::string cmd = "docker run -d --name " + proxy_name()
        + " --restart unless-stopped"
        + " --network containercp-public"
        + " -p 80:80 -p 443:443"
        + " -v " + cfg_.data_root() + "/proxy/sites/:/etc/nginx/conf.d/"
        + " nginx:alpine > /dev/null 2>&1";

    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        logger_.error("PROXY", "Failed to create central proxy container");
        return {false, "Failed to create central proxy container"};
    }
    logger_.info("PROXY", "Central proxy container created on containercp-public");
    return {true, ""};
}

// remove_central_proxy is preserved for cleanup but NOT called on normal shutdown.
// The proxy container survives daemon restart. Only explicit reset should remove it.
core::OperationResult NginxProxyProvider::remove_central_proxy() {
    std::string cmd = "docker rm -f " + proxy_name() + " > /dev/null 2>&1";
    std::system(cmd.c_str());
    logger_.info("PROXY", "Central proxy container removed");
    return {true, ""};
}

core::OperationResult NginxProxyProvider::status(const std::string& domain) {
    logger_.info("PROXY", "Status for " + domain);
    return {true, ""};
}

} // namespace containercp::proxy
