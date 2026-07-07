#ifndef CONTAINERCP_API_JSON_FORMATTER_H
#define CONTAINERCP_API_JSON_FORMATTER_H

#include "site/Site.h"
#include "user/User.h"
#include "domain/Domain.h"
#include "proxy/ReverseProxy.h"
#include "ssl/SslCertificate.h"
#include "node/Node.h"

#include <string>
#include <vector>

namespace containercp::api {

class JsonFormatter {
public:
    static std::string success(const std::string& data_json);
    static std::string error(const std::string& message);

    static std::string version(const std::string& version_str);
    static std::string health(bool ok);

    static std::string sites(const std::vector<site::Site>& sites);
    static std::string site(const site::Site& site);

    static std::string users(const std::vector<user::User>& users);
    static std::string domains(const std::vector<domain::Domain>& domains);
    static std::string proxies(const std::vector<proxy::ReverseProxy>& proxies);
    static std::string ssl_certificates(const std::vector<ssl::SslCertificate>& certs);
    static std::string nodes(const std::vector<node::Node>& nodes);

private:
    static std::string escape(const std::string& s);
};

} // namespace containercp::api

#endif // CONTAINERCP_API_JSON_FORMATTER_H
