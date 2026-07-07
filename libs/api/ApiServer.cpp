#include "ApiServer.h"
#include "api/JsonFormatter.h"

#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace containercp::api {

ApiServer::ApiServer(core::ServiceRegistry& services, int port)
    : port_(port)
    , services_(services)
    , auth_(std::make_unique<AllowAllAuth>())
{
}

void ApiServer::handle_client(int client_fd, ApiServer* server) {
    Request req = server->parse_request(client_fd);

    Response resp;
    if (!server->auth_->authenticate(req, resp)) {
        std::string resp_str = resp.to_string();
        ::write(client_fd, resp_str.data(), resp_str.size());
        ::close(client_fd);
        return;
    }

    // Serve static files for non-API paths
    if (req.path.find("/api/") != 0) {
        resp = server->serve_static(req.path);
    } else {
        resp = server->router_.dispatch(req);
    }

    std::string resp_str = resp.to_string();
    ::write(client_fd, resp_str.data(), resp_str.size());
    ::close(client_fd);
}

Response ApiServer::serve_static(const std::string& path) const {
    std::string file_path = services_.config().source_root() + "/web";
    if (path == "/" || path.empty()) {
        file_path += "/index.html";
    } else {
        // Remove leading slash and prevent directory traversal
        std::string clean = path.substr(1);
        if (clean.find("..") != std::string::npos) {
            Response r;
            r.status_code = 403;
            r.body = "Forbidden";
            return r;
        }
        file_path += "/" + clean;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        Response r;
        r.status_code = 404;
        r.body = "Not found";
        return r;
    }

    std::string content((std::istreambuf_iterator<char>(file)), {});

    // Determine content type
    std::string ext;
    auto dot = file_path.find_last_of('.');
    if (dot != std::string::npos) ext = file_path.substr(dot);

    Response r;
    r.status_code = 200;
    r.body = content;
    if (ext == ".css") r.content_type = "text/css";
    else if (ext == ".js") r.content_type = "application/javascript";
    else if (ext == ".png") r.content_type = "image/png";
    else if (ext == ".ico") r.content_type = "image/x-icon";
    else r.content_type = "text/html";

    return r;
}

Request ApiServer::parse_request(int client_fd) const {
    Request req;

    char buf[8192];
    ssize_t n = ::read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return req;
    buf[n] = '\0';

    std::istringstream stream(buf);
    std::string line;

    if (!std::getline(stream, line)) return req;
    {
        std::istringstream line_stream(line);
        line_stream >> req.method >> req.path;
    }

    while (std::getline(stream, line)) {
        if (line.empty() || line == "\r") break;
        if (line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            if (!val.empty() && val[0] == ' ') val = val.substr(1);
            req.headers[key] = val;
        }
    }

    auto qpos = req.path.find('?');
    if (qpos != std::string::npos) {
        std::string qs = req.path.substr(qpos + 1);
        req.path = req.path.substr(0, qpos);
        std::istringstream qs_stream(qs);
        while (std::getline(qs_stream, line, '&')) {
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                req.query[line.substr(0, eq)] = line.substr(eq + 1);
            }
        }
    }

    auto it = req.headers.find("Content-Length");
    if (it != req.headers.end()) {
        int body_len = std::stoi(it->second);
        if (body_len > 0 && body_len < 8192) {
            req.body = std::string(buf + n - body_len, body_len);
        }
    }

    return req;
}

bool ApiServer::start() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        services_.logger().error("ApiServer: Failed to create socket");
        return false;
    }

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = ::htons(port_);

    if (::bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        services_.logger().error("ApiServer: Failed to bind to port " + std::to_string(port_));
        ::close(server_fd_);
        return false;
    }

    if (::listen(server_fd_, 5) < 0) {
        services_.logger().error("ApiServer: Failed to listen");
        ::close(server_fd_);
        return false;
    }

    running_ = true;
    services_.logger().info("ApiServer: Listening on 127.0.0.1:" + std::to_string(port_));

    // Setup routes
    auto& s = services_;

    router_.add("GET", "/api/version", [](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::version("0.1.0"));
        return r;
    });

    router_.add("GET", "/api/health", [](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::health(true));
        return r;
    });

    router_.add("GET", "/api/sites", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::sites(s.sites().list()));
        return r;
    });

    router_.add("GET", "/api/users", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::users(s.users().list()));
        return r;
    });

    router_.add("GET", "/api/domains", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::domains(s.domains().list()));
        return r;
    });

    router_.add("GET", "/api/proxy", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::proxies(s.reverse_proxies().list()));
        return r;
    });

    router_.add("GET", "/api/ssl", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::ssl_certificates(s.ssl().list()));
        return r;
    });

    router_.add("GET", "/api/databases", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::databases(s.databases().list()));
        return r;
    });

    router_.add("GET", "/api/access-users", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::users(std::vector<user::User>()));
        // Return access users via their own listing
        auto& users = s.access_users().list();
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;
        for (const auto& u : users) {
            if (!first) json << ",";
            first = false;
            json << "{\"id\":" << u.id
                 << ",\"username\":\"" << u.username
                 << "\",\"enabled\":" << (u.enabled ? "true" : "false")
                 << "}";
        }
        json << "]}";
        r.body = json.str();
        return r;
    });

    router_.add("GET", "/api/profiles", [&s](const Request&) {
        Response r;
        auto& profiles = s.profiles().list();
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;
        for (const auto& p : profiles) {
            if (!first) json << ",";
            first = false;
            json << "{\"id\":" << p.id
                 << ",\"name\":\"" << JsonFormatter::escape(p.profile_name)
                 << "\",\"type\":\"" << profile::profile_type_to_string(p.type)
                 << "\",\"web_server\":\"" << JsonFormatter::escape(p.web_server)
                 << "\",\"description\":\"" << JsonFormatter::escape(p.description)
                 << "\",\"enabled\":" << (p.enabled ? "true" : "false")
                 << ",\"default\":" << (p.default_profile ? "true" : "false")
                 << "}";
        }
        json << "]}";
        r.body = json.str();
        return r;
    });

    router_.add("GET", "/api/nodes", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::nodes(s.nodes().list()));
        return r;
    });

    router_.add("GET", "/api/logs", [&s](const Request&) {
        std::string ts = "2024-01-01T00:00:00Z";
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        json << "{\"time\":\"" << ts << "\",\"level\":\"info\",\"message\":\"Daemon started\"},";
        json << "{\"time\":\"" << ts << "\",\"level\":\"info\",\"message\":\"Storage loaded\"},";
        json << "{\"time\":\"" << ts << "\",\"level\":\"info\",\"message\":\"REST API listening\"}";
        json << "]}";
        Response r;
        r.body = json.str();
        return r;
    });

    // Accept loop
    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) {
                services_.logger().error("ApiServer: Accept failed");
            }
            break;
        }
        handle_client(client_fd, this);
    }

    ::close(server_fd_);
    return true;
}

void ApiServer::stop() {
    running_ = false;
    ::shutdown(server_fd_, SHUT_RDWR);
}

Router& ApiServer::router() {
    return router_;
}

} // namespace containercp::api
