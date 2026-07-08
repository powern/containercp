#include "JsonFormatter.h"

#include <sstream>

namespace containercp::api {

std::string JsonFormatter::escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string JsonFormatter::success(const std::string& data_json) {
    return "{\"success\":true,\"data\":" + data_json + "}";
}

std::string JsonFormatter::error(const std::string& message) {
    return "{\"success\":false,\"error\":\"" + escape(message) + "\"}";
}

std::string JsonFormatter::version(const std::string& version_str) {
    return "{\"version\":\"" + escape(version_str) + "\"}";
}

std::string JsonFormatter::health(bool ok) {
    return "{\"status\":\"" + std::string(ok ? "ok" : "degraded") + "\"}";
}

std::string JsonFormatter::sites(const std::vector<site::Site>& sites) {
    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& s : sites) {
        if (!first) json << ",";
        first = false;
        json << "{\"id\":" << s.id
             << ",\"name\":\"" << escape(s.name)
             << "\",\"domain\":\"" << escape(s.domain)
             << "\",\"owner\":\"" << escape(s.owner)
             << "\",\"node_id\":" << s.node_id
             << ",\"web_server\":\"" << escape(s.web_server)
             << "\"}";
    }
    json << "]";
    return json.str();
}

std::string JsonFormatter::site(const site::Site& site) {
    std::ostringstream json;
    json << "{\"id\":" << site.id
         << ",\"name\":\"" << escape(site.name)
         << "\",\"domain\":\"" << escape(site.domain)
         << "\",\"owner\":\"" << escape(site.owner)
         << "\",\"node_id\":" << site.node_id
         << "}";
    return json.str();
}

std::string JsonFormatter::users(const std::vector<user::User>& users) {
    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& u : users) {
        if (!first) json << ",";
        first = false;
        json << "{\"id\":" << u.id
             << ",\"username\":\"" << escape(u.username)
             << "\",\"uid\":" << u.uid
             << ",\"enabled\":" << (u.enabled ? "true" : "false")
             << "}";
    }
    json << "]";
    return json.str();
}

std::string JsonFormatter::domains(const std::vector<domain::Domain>& domains) {
    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& d : domains) {
        if (!first) json << ",";
        first = false;
        json << "{\"id\":" << d.id
             << ",\"domain\":\"" << escape(d.fqdn)
             << "\",\"site_id\":" << d.site_id
             << ",\"php_version\":\"" << escape(d.php_version)
             << "\",\"ssl_enabled\":" << (d.ssl_enabled ? "true" : "false")
             << ",\"enabled\":" << (d.enabled ? "true" : "false")
             << "}";
    }
    json << "]";
    return json.str();
}

std::string JsonFormatter::proxies(const std::vector<proxy::ReverseProxy>& proxies) {
    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& p : proxies) {
        if (!first) json << ",";
        first = false;
        json << "{\"id\":" << p.id
             << ",\"domain\":\"" << escape(p.domain)
             << "\",\"site_id\":" << p.site_id
             << ",\"provider\":\"" << escape(p.provider)
             << "\",\"enabled\":" << (p.enabled ? "true" : "false")
             << ",\"status\":\"" << escape(p.status)
             << "\"}";
    }
    json << "]";
    return json.str();
}

std::string JsonFormatter::ssl_certificates(const std::vector<ssl::SslCertificate>& certs) {
    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& c : certs) {
        if (!first) json << ",";
        first = false;
        json << "{\"id\":" << c.id
             << ",\"domain\":\"" << escape(c.domain)
             << "\",\"provider\":\"" << escape(c.provider)
             << "\",\"status\":\"" << escape(c.status)
             << "\",\"expires_at\":\"" << escape(c.expires_at)
             << "\",\"auto_renew\":" << (c.auto_renew ? "true" : "false")
             << ",\"enabled\":" << (c.enabled ? "true" : "false")
             << "}";
    }
    json << "]";
    return json.str();
}

std::string JsonFormatter::nodes(const std::vector<node::Node>& nodes) {
    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& n : nodes) {
        if (!first) json << ",";
        first = false;
        json << "{\"id\":" << n.id
             << ",\"name\":\"" << escape(n.name)
             << "\",\"type\":\"" << escape(n.type)
             << "\"}";
    }
    json << "]";
    return json.str();
}

std::string JsonFormatter::databases(const std::vector<database::Database>& databases) {
    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& d : databases) {
        if (!first) json << ",";
        first = false;
        json << "{\"id\":" << d.id
             << ",\"name\":\"" << escape(d.db_name)
             << "\",\"user\":\"" << escape(d.db_user)
             << "\",\"engine\":\"" << escape(d.engine)
             << "\",\"site_id\":" << d.site_id
             << ",\"enabled\":" << (d.enabled ? "true" : "false")
             << "}";
    }
    json << "]";
    return json.str();
}

} // namespace containercp::api
