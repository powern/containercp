#include "ProxyConfigBuilder.h"
#include "ssl/CertificateProvider.h"

#include <sstream>

namespace containercp::proxy {

// Generate an nginx location block that proxies /api/* directly to
// the API server, bypassing the Web UI server.  This avoids a
// double-hop through the WebServer and makes API calls more reliable
// through the reverse proxy.
static std::string api_location(const std::string& api_upstream) {
    std::ostringstream conf;
    conf << "    location /api/ {\n"
         << "        set $backend \"http://" << api_upstream << "\";\n"
         << "        proxy_pass $backend;\n"
         << "        proxy_set_header Host $host;\n"
         << "        proxy_set_header X-Real-IP $remote_addr;\n"
         << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
         << "        proxy_set_header X-Forwarded-Proto $scheme;\n"
         << "    }\n";
    return conf.str();
}

std::string ProxyConfigBuilder::build(const Params& params) const {
    std::string api_loc = params.api_upstream.empty() ? "" : api_location(params.api_upstream);

    if (params.https && params.redirect) {
        return build_redirect_block(params.domain)
             + build_https_block(params.domain, params.upstream,
                                   params.cert_path, params.key_path, api_loc);
    }
    if (params.https) {
        return build_http_block(params.domain, params.upstream, api_loc)
             + build_https_block(params.domain, params.upstream,
                                   params.cert_path, params.key_path, api_loc);
    }
    // HTTP only
    std::ostringstream conf;
    conf << "server {\n"
         << "    listen 80;\n"
         << "    server_name " << params.domain << ";\n"
         << "    resolver 127.0.0.11 valid=30s;\n"
         << "\n";
    if (!params.acme_challenge_root.empty()) {
        // ACME challenge is served by Web UI server, not nginx.
    }
    conf << api_loc
         << "    location / {\n"
         << "        set $backend \"http://" << params.upstream << "\";\n"
         << "        proxy_pass $backend;\n"
         << "        proxy_set_header Host $host;\n"
         << "        proxy_set_header X-Real-IP $remote_addr;\n"
         << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
         << "        proxy_set_header X-Forwarded-Proto $scheme;\n"
         << "    }\n"
         << "}\n";
    return conf.str();
}

std::string ProxyConfigBuilder::build_http_block(const std::string& domain,
                                                   const std::string& upstream,
                                                   const std::string& api_loc) const {
    std::ostringstream conf;
    conf << "server {\n"
         << "    listen 80;\n"
         << "    server_name " << domain << ";\n"
         << "    resolver 127.0.0.11 valid=30s;\n"
         << "\n"
         << api_loc
         << "    location / {\n"
         << "        set $backend \"http://" << upstream << "\";\n"
         << "        proxy_pass $backend;\n"
         << "        proxy_set_header Host $host;\n"
         << "        proxy_set_header X-Real-IP $remote_addr;\n"
         << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
         << "        proxy_set_header X-Forwarded-Proto $scheme;\n"
         << "    }\n"
         << "}\n";
    return conf.str();
}

std::string ProxyConfigBuilder::build_https_block(const std::string& domain,
                                                    const std::string& upstream,
                                                    const std::string& cert_path,
                                                    const std::string& key_path,
                                                    const std::string& api_loc) const {
    std::ostringstream conf;
    conf << "server {\n"
         << "    listen 443 ssl;\n"
         << "    server_name " << domain << ";\n"
         << "    resolver 127.0.0.11 valid=30s;\n"
         << "    ssl_certificate " << cert_path << ";\n"
         << "    ssl_certificate_key " << key_path << ";\n"
         << "\n"
         << api_loc
         << "    location / {\n"
         << "        set $backend \"http://" << upstream << "\";\n"
         << "        proxy_pass $backend;\n"
         << "        proxy_set_header Host $host;\n"
         << "        proxy_set_header X-Real-IP $remote_addr;\n"
         << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
         << "        proxy_set_header X-Forwarded-Proto https;\n"
         << "    }\n"
         << "}\n";
    return conf.str();
}

std::string ProxyConfigBuilder::build_redirect_block(const std::string& domain) const {
    std::ostringstream conf;
    conf << "server {\n"
         << "    listen 80;\n"
         << "    server_name " << domain << ";\n"
         << "    return 301 https://$host$request_uri;\n"
         << "}\n";
    return conf.str();
}

std::string ProxyConfigBuilder::acme_challenge_location(const std::string& challenge_root) {
    if (challenge_root.empty()) {
        return "location /.well-known/acme-challenge/ {\n"
               "    root /srv/containercp/ssl;\n"
               "}\n";
    }
    // For admin panel: serve challenge files from a custom path
    auto parent = challenge_root.substr(0, challenge_root.rfind('/'));
    return "location ^~ /.well-known/acme-challenge/ {\n"
           "    root " + parent + ";\n"
           "    try_files $uri =404;\n"
           "}\n";
}

std::string ProxyConfigBuilder::normalize_upstream(const std::string& raw) {
    std::string result = raw;
    auto scheme = result.find("://");
    if (scheme != std::string::npos) {
        result = result.substr(scheme + 3);
    }
    while (!result.empty() && result[0] == '/') {
        result = result.substr(1);
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t' || result.back() == ';')) {
        result.pop_back();
    }
    return result;
}

} // namespace containercp::proxy
