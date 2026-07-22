#include "ProxyConfigBuilder.h"
#include "ssl/CertificateProvider.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace containercp::proxy {

std::string ProxyConfigBuilder::build(const Params& params) const {
    std::string webmail_loc;
    if (!params.webmail_upstream.empty()) {
        webmail_loc = "    location /webmail/ {\n"
            "        proxy_pass http://" + params.webmail_upstream + "/;\n"
            "        proxy_set_header Host $host;\n"
            "        proxy_set_header X-Real-IP $remote_addr;\n"
            "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
            "        proxy_set_header X-Forwarded-Proto $scheme;\n"
            "    }\n"
            "    location /snappymail/ {\n"
            "        proxy_pass http://" + params.webmail_upstream + ";\n"
            "        proxy_set_header Host $host;\n"
            "        proxy_set_header X-Real-IP $remote_addr;\n"
            "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
            "        proxy_set_header X-Forwarded-Proto $scheme;\n"
            "    }\n";
    }

    if (params.https && params.redirect) {
        return build_redirect_block(params.domain)
             + build_https_block(params.domain, params.upstream,
                                  params.cert_path, params.key_path, webmail_loc + params.sql_console_locations);
    }
    if (params.https) {
        return build_http_block(params.domain, params.upstream)
             + build_https_block(params.domain, params.upstream,
                                  params.cert_path, params.key_path, webmail_loc + params.sql_console_locations);
    }
    // HTTP only — build server block with optional ACME location INSIDE
    std::ostringstream conf;
    conf << "server {\n"
         << "    listen 80;\n"
         << "    server_name " << params.domain << ";\n"
         << "    resolver 127.0.0.11 valid=30s;\n"
         << "\n";
    // ACME challenge location (inside server block, before proxy_pass)
    if (!params.acme_challenge_root.empty()) {
        // ACME challenge is served by Web UI server, not nginx.
        // location ^~ is not needed — proxy_pass handles all.
    }
    conf << "    location / {\n"
         << "        set $backend \"http://" << params.upstream << "\";\n"
         << "        proxy_pass $backend;\n"
         << "        proxy_set_header Host $host;\n"
         << "        proxy_set_header X-Real-IP $remote_addr;\n"
         << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
         << "        proxy_set_header X-Forwarded-Proto $scheme;\n"
         << "    }\n"
         << webmail_loc
         << params.sql_console_locations
         << "}\n";
    return conf.str();
}

std::string ProxyConfigBuilder::build_http_block(const std::string& domain,
                                                   const std::string& upstream) const {
    std::ostringstream conf;
    conf << "server {\n"
         << "    listen 80;\n"
         << "    server_name " << domain << ";\n"
         << "    resolver 127.0.0.11 valid=30s;\n"
         << "\n"
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
                                                    const std::string& webmail_loc) const {
    std::ostringstream conf;
    conf << "server {\n"
         << "    listen 443 ssl;\n"
         << "    server_name " << domain << ";\n"
         << "    resolver 127.0.0.11 valid=30s;\n"
         << "    ssl_certificate " << cert_path << ";\n"
         << "    ssl_certificate_key " << key_path << ";\n"
         << "\n"
         << "    location / {\n"
         << "        set $backend \"http://" << upstream << "\";\n"
         << "        proxy_pass $backend;\n"
         << "        proxy_set_header Host $host;\n"
         << "        proxy_set_header X-Real-IP $remote_addr;\n"
         << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
         << "        proxy_set_header X-Forwarded-Proto https;\n"
         << "        proxy_set_header X-Forwarded-Port 443;\n"
         << "        proxy_set_header X-Forwarded-Ssl on;\n"
         << "    }\n"
         << webmail_loc
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

std::string ProxyConfigBuilder::sql_console_route_locations(const std::string& launch_id,
                                                            uint64_t database_id,
                                                            const std::string& adminer_upstream,
                                                            const std::string& auth_upstream) {
    const bool valid_launch_id = launch_id.size() == 32 &&
        std::all_of(launch_id.begin(), launch_id.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
    if (!valid_launch_id || database_id == 0) return {};
    const auto adminer = normalize_upstream(adminer_upstream);
    const auto auth = normalize_upstream(auth_upstream);
    if (adminer.empty() || auth.empty()) return {};

    std::ostringstream conf;
    conf << "    # containercp-sql-console-route " << launch_id << " begin\n"
         << "    location = /sql-console/internal/redeem {\n"
         << "        return 404;\n"
         << "    }\n"
         << "    location = /sql-console/internal/logout {\n"
         << "        return 404;\n"
         << "    }\n"
         << "    location = /sql-console/internal/auth/" << launch_id << " {\n"
         << "        internal;\n"
         << "        proxy_pass http://" << auth << "/sql-console/internal/auth/" << launch_id << ";\n"
         << "        proxy_pass_request_body off;\n"
         << "        proxy_set_header Content-Length \"\";\n"
         << "        proxy_set_header Cookie $http_cookie;\n"
         << "        proxy_set_header X-Real-IP $remote_addr;\n"
         << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
         << "        proxy_set_header X-Forwarded-Proto $scheme;\n"
         << "    }\n"
         << "    location ^~ /sql-console/" << launch_id << "/ {\n"
         << "        auth_request /sql-console/internal/auth/" << launch_id << ";\n"
         << "        set $sql_console_backend \"http://" << adminer << "\";\n"
         << "        rewrite ^/sql-console/" << launch_id << "/?(.*)$ /$1 break;\n"
         << "        proxy_pass $sql_console_backend;\n"
         << "        proxy_set_header Host $host;\n"
         << "        proxy_set_header X-ContainerCP-SqlConsole-Launch-Id " << launch_id << ";\n"
         << "        proxy_set_header X-ContainerCP-SqlConsole-Database-Id " << database_id << ";\n"
         << "        proxy_set_header X-Real-IP $remote_addr;\n"
         << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
         << "        proxy_set_header X-Forwarded-Proto $scheme;\n"
         << "    }\n"
         << "    # containercp-sql-console-route " << launch_id << " end\n";
    return conf.str();
}

} // namespace containercp::proxy
