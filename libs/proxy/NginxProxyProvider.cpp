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

    std::string upstream = proxy.upstream.empty() ? "site-0-web:80" : proxy.upstream;

    auto* cert = ssl_mgr_.find_by_domain(proxy.domain);
    bool has_ssl = (cert != nullptr && cert->https_enabled && cert->status == "active");

    ProxyConfigBuilder::Params cfg_params;
    cfg_params.domain = proxy.domain;
    cfg_params.upstream = upstream;
    cfg_params.https = has_ssl;
    cfg_params.redirect = has_ssl;
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

// Extract upstream from existing config file, normalize to host:port
static std::string extract_upstream(const std::string& filepath, logger::Logger& logger) {
    std::ifstream in(filepath);
    if (!in.is_open()) return "";

    std::string content((std::istreambuf_iterator<char>(in)), {});
    in.close();

    const std::string marker = "proxy_pass ";
    auto pos = content.find(marker);
    if (pos == std::string::npos) return "";

    pos += marker.size(); // skip "proxy_pass "

    // Find semicolon end
    auto end = content.find(";", pos);
    if (end == std::string::npos) return "";

    std::string normalized = ProxyConfigBuilder::normalize_upstream(content.substr(pos, end - pos));

    logger.info("PROXY", "extract_upstream: normalized='" + normalized + "'");
    return normalized;
}

core::OperationResult NginxProxyProvider::attach_certificate(const std::string& domain,
                                                               const std::string& cert_path,
                                                               const std::string& key_path) {
    std::string path = config_path(domain);
    if (!fs_.exists(path)) {
        return {false, "Proxy config not found for " + domain};
    }

    // Extract upstream from existing config (always normalized to host:port)
    std::string upstream = extract_upstream(path, logger_);
    if (upstream.empty()) {
        upstream = "site-0-web:80";
        logger_.warning("PROXY", domain + ": upstream not found, using default: " + upstream);
    }
    logger_.info("PROXY", domain + ": resolved upstream=" + upstream);

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

    // Write to a backup copy first, then rename to config_path.
    // nginx -t validates ALL configs in /etc/nginx/conf.d/ so we must
    // ensure the new file is in place (via atomic rename) before validating.
    //
    // Transactional flow:
    //   1. Write new config to path.new (on host filesystem, visible to container)
    //   2. Rename new → path (atomic, replaces old broken config)
    //   3. Run nginx -t inside container
    //   4. If OK, reload nginx
    //   5. If FAIL, restore old config from path.bak (saved before rename)
    std::string new_path = path + ".new";
    std::string bak_path = path + ".bak";

    // Save current config as backup
    std::filesystem::copy(path, bak_path, std::filesystem::copy_options::overwrite_existing);

    // Write new config
    fs_.create_file(new_path, config);

    // Atomic rename: replace old config with new one
    std::filesystem::rename(new_path, path);

    // Validate ALL nginx configs inside the container
    if (!validate_nginx_config(path)) {
        // Restore backup
        std::filesystem::rename(bak_path, path);
        logger_.error("PROXY", domain + ": nginx config validation failed, config restored");
        return {false, domain + ": nginx config validation failed"};
    }

    // Remove backup
    std::filesystem::remove(bak_path);

    // Reload nginx
    auto reload_result = reload();
    if (!reload_result.success) {
        return {false, domain + ": Config written but nginx reload failed: " + reload_result.message};
    }

    logger_.info("PROXY", "HTTPS enabled for " + domain);
    return {true, ""};
}

core::OperationResult NginxProxyProvider::detach_certificate(const std::string& domain) {
    std::string path = config_path(domain);
    if (!fs_.exists(path)) {
        return {true, ""};
    }

    std::string upstream = extract_upstream(path, logger_);
    if (upstream.empty()) {
        upstream = "site-0-web:80";
    }
    logger_.info("PROXY", domain + ": detaching certificate, upstream=" + upstream);

    // Build pure HTTP config
    std::string config = config_builder_.build_http_block(domain, upstream);

    // Transactional replace
    std::string new_path = path + ".detach_new";
    std::string bak_path = path + ".detach_bak";
    std::filesystem::copy(path, bak_path, std::filesystem::copy_options::overwrite_existing);
    fs_.create_file(new_path, config);
    std::filesystem::rename(new_path, path);

    if (!validate_nginx_config(path)) {
        std::filesystem::rename(bak_path, path);
        logger_.error("PROXY", domain + ": nginx config validation failed after detach, restored");
        return {false, domain + ": nginx config validation failed"};
    }
    std::filesystem::remove(bak_path);

    auto reload_result = reload();
    if (!reload_result.success) {
        return {false, "Config updated but nginx reload failed: " + reload_result.message};
    }

    logger_.info("PROXY", "Detached certificate for " + domain);
    return {true, ""};
}

bool NginxProxyProvider::validate_nginx_config(const std::string& config_path) const {
    (void)config_path; // All configs in /etc/nginx/conf.d/ are validated together
    std::string cmd = "docker exec " + proxy_name() + " nginx -t 2>&1";
    std::string out_file = "/tmp/containercp-nginx-check.txt";
    std::system((cmd + " > " + out_file + " 2>&1").c_str());
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
    std::string cmd = "docker exec " + proxy_name() + " nginx -s reload 2>&1";
    std::string out_file = "/tmp/containercp-nginx-reload.txt";
    std::system((cmd + " > " + out_file + " 2>&1").c_str());
    std::ifstream out_in(out_file);
    bool ok = true;
    std::string error_msg;
    if (out_in.is_open()) {
        std::string line;
        while (std::getline(out_in, line)) {
            if (line.find("emerg") != std::string::npos || line.find("failed") != std::string::npos) {
                error_msg = line;
                ok = false;
            }
        }
    }
    std::remove(out_file.c_str());
    if (!ok) {
        logger_.error("PROXY", "Reload failed: " + error_msg);
        return {false, "Reload failed: " + error_msg};
    }
    logger_.info("PROXY", "Reloaded");
    return {true, ""};
}

core::OperationResult NginxProxyProvider::ensure_central_proxy() {
    if (central_proxy_running()) {
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

    std::string net_cmd = "docker network inspect containercp-public > /dev/null 2>&1"
        " || docker network create containercp-public > /dev/null 2>&1";
    std::system(net_cmd.c_str());

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
