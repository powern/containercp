#include "ProxyConfigBuilder.h"
#include "ssl/CertificateProvider.h"

#include <sstream>

namespace containercp::proxy {

std::string ProxyConfigBuilder::build(const Params& params) const {
    std::ostringstream conf;

    if (params.https && params.redirect) {
        // HTTP → HTTPS redirect, then HTTPS block
        conf << build_redirect_block(params.domain);
        conf << build_https_block(params.domain, params.upstream,
                                   params.cert_path, params.key_path);
    } else if (params.https) {
        // Both HTTP and HTTPS, no redirect
        conf << build_http_block(params.domain, params.upstream);
        conf << build_https_block(params.domain, params.upstream,
                                   params.cert_path, params.key_path);
    } else {
        // HTTP only
        conf << build_http_block(params.domain, params.upstream);
    }

    return conf.str();
}

std::string ProxyConfigBuilder::build_http_block(const std::string& domain,
                                                   const std::string& upstream) const {
    std::ostringstream conf;
    conf << "server {\n"
         << "    listen 80;\n"
         << "    server_name " << domain << ";\n"
         << "\n"
         << "    location / {\n"
         << "        proxy_pass http://" << upstream << ";\n"
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
                                                    const std::string& key_path) const {
    std::ostringstream conf;
    conf << "server {\n"
         << "    listen 443 ssl;\n"
         << "    server_name " << domain << ";\n"
         << "    ssl_certificate " << cert_path << ";\n"
         << "    ssl_certificate_key " << key_path << ";\n"
         << "\n"
         << "    location / {\n"
         << "        proxy_pass http://" << upstream << ";\n"
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

std::string ProxyConfigBuilder::acme_challenge_location() const {
    return "location /.well-known/acme-challenge/ {\n"
           "    root /srv/containercp/ssl;\n"
           "}\n";
}

} // namespace containercp::proxy
