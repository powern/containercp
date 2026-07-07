#include "WebServer.h"
#include "api/Response.h"

#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>



namespace containercp::api {

WebServer::WebServer(core::ServiceRegistry& services, const std::string& bind_addr, int port)
    : bind_addr_(bind_addr)
    , port_(port)
    , services_(services)
{
}

void WebServer::handle_client(int client_fd, WebServer* server) {
    Request req = server->parse_request(client_fd);

    // Reject API paths — API is only on localhost
    if (req.path.find("/api/") == 0) {
        Response resp;
        resp.status_code = 403;
        resp.body = "Forbidden: API is not available on this port. Use 127.0.0.1:8080.";
        std::string resp_str = resp.to_string();
        ::write(client_fd, resp_str.data(), resp_str.size());
        ::close(client_fd);
        return;
    }

    // Serve static files
    Response resp = server->serve_static(req.path);
    std::string resp_str = resp.to_string();
    ::write(client_fd, resp_str.data(), resp_str.size());
    ::close(client_fd);
}

Response WebServer::serve_static(const std::string& path) const {
    std::string file_path = services_.config().source_root() + "/web";
    if (path == "/" || path.empty()) {
        file_path += "/index.html";
    } else {
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

Request WebServer::parse_request(int client_fd) const {
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

    return req;
}

bool WebServer::start() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        services_.logger().error("WebServer: Failed to create socket");
        return false;
    }

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr(bind_addr_.c_str());
    addr.sin_port = ::htons(port_);

    if (::bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        services_.logger().error("WebServer: Failed to bind to " + bind_addr_ + ":" + std::to_string(port_));
        ::close(server_fd_);
        return false;
    }

    if (::listen(server_fd_, 5) < 0) {
        services_.logger().error("WebServer: Failed to listen");
        ::close(server_fd_);
        return false;
    }

    running_ = true;
    services_.logger().info("WebServer: Listening on " + bind_addr_ + ":" + std::to_string(port_));

    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) {
                services_.logger().error("WebServer: Accept failed");
            }
            break;
        }
        handle_client(client_fd, this);
    }

    ::close(server_fd_);
    return true;
}

void WebServer::stop() {
    running_ = false;
    ::shutdown(server_fd_, SHUT_RDWR);
}

} // namespace containercp::api
