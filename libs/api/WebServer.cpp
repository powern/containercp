#include "WebServer.h"
#include "api/Response.h"

#include <arpa/inet.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <random>

namespace containercp::api {

WebServer::WebServer(core::ServiceRegistry& services, const std::string& bind_addr, int port, int api_port)
    : bind_addr_(bind_addr)
    , port_(port)
    , api_port_(api_port)
    , services_(services)
{
}

void WebServer::load_password() {
    std::string dir = services_.config().config_root();
    std::string path = dir + "/ui-password";

    ::mkdir(dir.c_str(), 0755);

    std::ifstream f(path);
    if (f.is_open()) {
        std::getline(f, password_);
        f.close();
        return;
    }

    std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, chars.size() - 1);
    password_.reserve(16);
    for (int i = 0; i < 16; ++i) {
        password_ += chars[dist(gen)];
    }

    std::ofstream of(path);
    if (of.is_open()) {
        of << password_ << std::endl;
        of.close();
    }

    services_.logger().info("WebUI password generated: " + password_);
    services_.logger().info("WebUI username: admin");
    services_.logger().info("WebUI password file: " + path);
}

bool WebServer::check_auth(const Request& req) const {
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) {
        return false;
    }

    const std::string creds = "admin:" + password_;
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    size_t i = 0;
    while (i < creds.size()) {
        size_t start = i;
        unsigned char c1 = creds[i++];
        unsigned char c2 = (i < creds.size()) ? creds[i++] : 0;
        unsigned char c3 = (i < creds.size()) ? creds[i++] : 0;
        size_t read = i - start;
        encoded += b64[c1 >> 2];
        encoded += b64[((c1 & 0x3) << 4) | (c2 >> 4)];
        encoded += (read >= 2) ? b64[((c2 & 0xf) << 2) | (c3 >> 6)] : '=';
        encoded += (read >= 3) ? b64[c3 & 0x3f] : '=';
    }

    return it->second == "Basic " + encoded;
}

void WebServer::handle_client(int client_fd, WebServer* server) {
    char buf[65536];
    ssize_t n = ::read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        ::close(client_fd);
        return;
    }
    buf[n] = '\0';
    std::string raw(buf);

    Request req = server->parse_request(raw);

    if (!server->check_auth(req)) {
        std::string body = "Unauthorized";
        std::ostringstream resp;
        resp << "HTTP/1.1 401 Unauthorized\r\n"
             << "Content-Type: text/plain\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "WWW-Authenticate: Basic realm=\"ContainerCP Web UI\"\r\n"
             << "\r\n"
             << body;
        std::string resp_str = resp.str();
        ::write(client_fd, resp_str.data(), resp_str.size());
        ::close(client_fd);
        return;
    }

    // Proxy UI API calls to internal API
    if (req.path.find("/ui-api/") == 0) {
        server->proxy_to_api(raw, client_fd);
        return;
    }

    // Reject raw API paths
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

void WebServer::proxy_to_api(const std::string& raw_request, int client_fd) {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ::close(client_fd);
        return;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = ::htons(api_port_);

    if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::string body = "{\"success\":false,\"error\":\"API unavailable\"}";
        std::ostringstream resp;
        resp << "HTTP/1.1 502 Bad Gateway\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "\r\n"
             << body;
        std::string resp_str = resp.str();
        ::write(client_fd, resp_str.data(), resp_str.size());
        ::close(sock);
        ::close(client_fd);
        return;
    }

    // Rewrite path in the first line: /ui-api/... -> /api/...
    size_t sp1 = raw_request.find(' ');
    size_t sp2 = raw_request.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        ::close(sock);
        ::close(client_fd);
        return;
    }
    std::string req_path = raw_request.substr(sp1 + 1, sp2 - sp1 - 1);
    if (req_path.find("/ui-api") == 0) {
        req_path = req_path.substr(7);
    }
    std::string rewritten = raw_request.substr(0, sp1 + 1) + req_path + raw_request.substr(sp2);

    ::write(sock, rewritten.data(), rewritten.size());

    char resp_buf[65536];
    ssize_t resp_n;
    while ((resp_n = ::read(sock, resp_buf, sizeof(resp_buf))) > 0) {
        ::write(client_fd, resp_buf, resp_n);
    }

    ::close(sock);
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

Request WebServer::parse_request(const std::string& raw) const {
    Request req;
    std::istringstream stream(raw);
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
    load_password();

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
    services_.logger().info("Web UI: Listening on http://" + bind_addr_ + ":" + std::to_string(port_) + "/");
    services_.logger().info("Web UI: Username: admin");
    services_.logger().info("Web UI: Password file: " + services_.config().config_root() + "/ui-password");

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
