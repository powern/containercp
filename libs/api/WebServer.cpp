#include "WebServer.h"
#include "api/JsonFormatter.h"
#include "api/Response.h"

#include <arpa/inet.h>
#include <cstring>
#include <cctype>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::api {
namespace {
constexpr int kMaxWebRequestBodyBytes = 6 * 1024 * 1024;

std::string json_string_value(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return "";
    ++pos;
    std::string out;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') break;
        if (c == '\\' && pos < json.size()) {
            out += json[pos++];
        } else {
            out += c;
        }
    }
    return out;
}

uint64_t json_u64_value(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    const auto start = pos;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) ++pos;
    if (start == pos) return 0;
    try { return static_cast<uint64_t>(std::stoull(json.substr(start, pos - start))); } catch (...) { return 0; }
}

std::string request_body(const std::string& raw_request) {
    const auto body_start = raw_request.find("\r\n\r\n");
    if (body_start == std::string::npos) return "";
    return raw_request.substr(body_start + 4);
}
}

WebServer::WebServer(core::ServiceRegistry& services, const std::string& bind_addr, int port, int api_port)
    : bind_addr_(bind_addr)
    , port_(port)
    , api_port_(api_port)
    , services_(services)
{
}

void WebServer::send_json(int client_fd, int status, const std::string& body) {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << " " << (status == 200 ? "OK" : status == 401 ? "Unauthorized" : status == 400 ? "Bad Request" : "Not Found") << "\r\n"
         << "Content-Type: application/json\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "\r\n"
         << body;
    std::string resp_str = resp.str();
    ::write(client_fd, resp_str.data(), resp_str.size());
    ::close(client_fd);
}

void WebServer::send_unauthorized(int client_fd) {
    send_json(client_fd, 401, "{\"success\":false,\"error\":\"Unauthorized\",\"login_required\":true}");
}

std::string WebServer::extract_session_token(const std::string& raw_request) const {
    std::istringstream stream(raw_request);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            if (!val.empty() && val[0] == ' ') val = val.substr(1);
            if (key == "X-Session-Token") {
                return val;
            }
        }
        if (line.empty()) break;
    }
    return "";
}

std::string WebServer::extract_header(const std::string& raw_request, const std::string& name) const {
    std::istringstream stream(raw_request);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            if (!val.empty() && val[0] == ' ') val = val.substr(1);
            if (key == name) return val;
        }
        if (line.empty()) break;
    }
    return "";
}

std::string WebServer::extract_cookie(const std::string& raw_request, const std::string& name) const {
    std::istringstream stream(raw_request);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) {
            if (line.empty()) break;
            continue;
        }
        std::string key = line.substr(0, colon);
        if (key != "Cookie") continue;
        std::string value = line.substr(colon + 1);
        std::size_t pos = 0;
        while (pos < value.size()) {
            while (pos < value.size() && (value[pos] == ' ' || value[pos] == ';')) ++pos;
            const auto eq = value.find('=', pos);
            if (eq == std::string::npos) break;
            std::string cookie_name = value.substr(pos, eq - pos);
            auto end = value.find(';', eq + 1);
            std::string cookie_value = value.substr(eq + 1, end == std::string::npos ? std::string::npos : end - eq - 1);
            if (cookie_name == name) return cookie_value;
            if (end == std::string::npos) break;
            pos = end + 1;
        }
    }
    return "";
}

bool WebServer::require_session(const std::string& raw_request, int client_fd) {
    std::string token = extract_session_token(raw_request);
    if (token.empty() || services_.auth().validate_session(token) == nullptr) {
        send_unauthorized(client_fd);
        return false;
    }
    return true;
}

void WebServer::handle_auth_login(const std::string& raw_request, int client_fd) {
    auto body_start = raw_request.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":\"Bad request\"}");
        services_.logger().info("Auth: login request malformed (no body)");
        return;
    }
    std::string body = raw_request.substr(body_start + 4);

    auto uname_pos = body.find("\"username\":\"");
    auto pwd_pos = body.find("\"password\":\"");
    if (uname_pos == std::string::npos || pwd_pos == std::string::npos) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":\"Missing credentials\"}");
        services_.logger().info("Auth: login missing username or password in request body");
        return;
    }

    // Skip past the field name and opening quote: "username":" is 12 chars
    uname_pos += 12;
    auto uname_end = body.find("\"", uname_pos);
    pwd_pos += 12;
    auto pwd_end = body.find("\"", pwd_pos);

    if (uname_end == std::string::npos || pwd_end == std::string::npos) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":\"Bad request\"}");
        services_.logger().info("Auth: login malformed JSON structure");
        return;
    }

    std::string username = body.substr(uname_pos, uname_end - uname_pos);
    std::string password = body.substr(pwd_pos, pwd_end - pwd_pos);

    std::string token = services_.auth().authenticate(username, password);
    if (token.empty()) {
        send_json(client_fd, 401, "{\"success\":false,\"error\":\"Invalid credentials\"}");
        return;
    }

    auto* user = services_.auth_users().find(username);
    bool must_change = user != nullptr && user->must_change_password;

    std::string resp = "{\"success\":true,\"data\":{\"token\":\"" + token
        + "\",\"username\":\"" + username
        + "\",\"must_change_password\":" + (must_change ? "true" : "false") + "}}";
    send_json(client_fd, 200, resp);
    services_.logger().info("Auth: login success — '" + username + "'");
}

void WebServer::handle_auth_change_password(const std::string& raw_request, int client_fd) {
    if (!require_session(raw_request, client_fd)) return;

    auto body_start = raw_request.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":\"Bad request\"}");
        return;
    }
    std::string body = raw_request.substr(body_start + 4);

    auto old_pos = body.find("\"old_password\":\"");
    auto new_pos = body.find("\"new_password\":\"");
    if (old_pos == std::string::npos || new_pos == std::string::npos) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":\"Missing fields\"}");
        return;
    }

    old_pos += 16;
    auto old_end = body.find("\"", old_pos);
    new_pos += 16;
    auto new_end = body.find("\"", new_pos);

    if (old_end == std::string::npos || new_end == std::string::npos) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":\"Bad request\"}");
        return;
    }

    std::string old_password = body.substr(old_pos, old_end - old_pos);
    std::string new_password = body.substr(new_pos, new_end - new_pos);

    if (old_password.empty() || new_password.empty()) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":\"Missing fields\"}");
        return;
    }

    std::string token = extract_session_token(raw_request);
    if (services_.auth().change_password(token, old_password, new_password)) {
        send_json(client_fd, 200, "{\"success\":true,\"data\":{\"message\":\"Password changed\"}}");
    } else {
        send_json(client_fd, 400, "{\"success\":false,\"error\":\"Failed to change password\"}");
    }
}

void WebServer::handle_auth_logout(const std::string& raw_request, int client_fd) {
    std::string token = extract_session_token(raw_request);
    if (!token.empty()) {
        services_.auth().logout(token);
    }
    send_json(client_fd, 200, "{\"success\":true,\"data\":{\"message\":\"Logged out\"}}");
}

void WebServer::handle_auth_me(const std::string& raw_request, int client_fd) {
    std::string token = extract_session_token(raw_request);
    auto* session = services_.auth().validate_session(token);
    if (!session) {
        send_unauthorized(client_fd);
        return;
    }

    auto* user = services_.auth_users().find(session->username);
    std::string resp = "{\"success\":true,\"data\":{"
        "\"username\":\"" + session->username + "\","
        "\"role\":\"" + session->role + "\","
        "\"must_change_password\":" + (user && user->must_change_password ? "true" : "false") + "}}";
    send_json(client_fd, 200, resp);
}

void WebServer::handle_sql_console_auth(const Request& req, const std::string& raw_request, int client_fd) {
    const std::string prefix = "/sql-console/internal/auth/";
    std::string launch_id = req.path.substr(prefix.size());
    if (launch_id.size() != 32) {
        send_unauthorized(client_fd);
        return;
    }
    for (unsigned char c : launch_id) {
        if (std::isxdigit(c) == 0) {
            send_unauthorized(client_fd);
            return;
        }
    }

    const std::string launch_secret = extract_cookie(raw_request, "ccp_sql_console_secret");
    if (launch_secret.empty()) {
        send_unauthorized(client_fd);
        return;
    }
    const auto authorized = services_.sql_console().authorize_launch_session(launch_id, launch_secret);
    if (!authorized.success) {
        if (authorized.code == "session_expired" || authorized.code == "session_revoked") {
            (void)services_.cleanup_sql_console_launch_session(launch_id);
        }
        send_unauthorized(client_fd);
        return;
    }
    send_json(client_fd, 200, "{\"success\":true}");
}

void WebServer::handle_sql_console_logout(const std::string& raw_request, int client_fd) {
    const std::string supplied_token = extract_header(raw_request, "X-ContainerCP-SqlConsole-Internal");
    if (supplied_token.empty() || supplied_token != services_.sql_console().internal_api_token()) {
        send_json(client_fd, 403, "{\"success\":false,\"error\":{\"code\":\"internal_token_invalid\",\"message\":\"SQL Console internal token is invalid\"}}");
        return;
    }
    const auto body = request_body(raw_request);
    const auto launch_id = json_string_value(body, "launch_id");
    if (launch_id.empty()) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":{\"code\":\"launch_id_required\",\"message\":\"SQL Console launch id is required\"}}");
        return;
    }
    const auto cleaned = services_.cleanup_sql_console_launch_session(launch_id);
    if (!cleaned.success) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":{\"code\":\"cleanup_failed\",\"message\":\"SQL Console cleanup failed\"}}");
        return;
    }
    send_json(client_fd, 200, "{\"success\":true}");
}

void WebServer::handle_sql_console_redeem(const std::string& raw_request, int client_fd) {
    const std::string supplied_token = extract_header(raw_request, "X-ContainerCP-SqlConsole-Internal");
    if (supplied_token.empty() || supplied_token != services_.sql_console().internal_api_token()) {
        send_json(client_fd, 403, "{\"success\":false,\"error\":{\"code\":\"internal_token_invalid\",\"message\":\"SQL Console internal token is invalid\"}}");
        return;
    }

    const auto body = request_body(raw_request);
    const auto launch_id = json_string_value(body, "launch_id");
    const auto database_id = json_u64_value(body, "database_id");
    const auto launch_secret = extract_cookie(raw_request, "ccp_sql_console_secret");
    if (launch_id.empty() || database_id == 0 || launch_secret.empty()) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":{\"code\":\"launch_cookie_required\",\"message\":\"SQL Console launch id, database id, and secret cookie are required\"}}");
        return;
    }

    const auto redeemed = services_.sql_console().redeem_internal_launch_session(launch_id, launch_secret, database_id);
    if (!redeemed.success) {
        send_json(client_fd, 400, "{\"success\":false,\"error\":{\"code\":\"" + JsonFormatter::escape(redeemed.code) +
                             "\",\"message\":\"" + JsonFormatter::escape(redeemed.message) + "\"}}");
        return;
    }

    send_json(client_fd, 200,
              "{\"success\":true,\"data\":{\"database_name\":\"" + JsonFormatter::escape(redeemed.temporary_credential.database_name) +
              "\",\"database_user\":\"" + JsonFormatter::escape(redeemed.temporary_credential.user_name) +
              "\",\"database_password\":\"" + JsonFormatter::escape(redeemed.temporary_credential.password) + "\"}}");
}

void WebServer::handle_client(int client_fd, WebServer* server) {
    char buf[65536];
    ssize_t n = ::read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        ::close(client_fd);
        return;
    }
    buf[n] = '\0';
    std::string raw(buf, static_cast<std::size_t>(n));
    auto header_end = raw.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        auto header = raw.substr(0, header_end);
        auto cl_pos = header.find("Content-Length:");
        if (cl_pos != std::string::npos) {
            cl_pos += 15;
            while (cl_pos < header.size() && header[cl_pos] == ' ') ++cl_pos;
            auto cl_end = header.find("\r\n", cl_pos);
            int expected = 0;
            try { expected = std::stoi(header.substr(cl_pos, cl_end - cl_pos)); } catch (...) { expected = 0; }
            if (expected > 0 && expected <= kMaxWebRequestBodyBytes) {
                const std::size_t body_start = header_end + 4;
                while (raw.size() < body_start + static_cast<std::size_t>(expected)) {
                    ssize_t more = ::read(client_fd, buf, sizeof(buf));
                    if (more <= 0) break;
                    raw.append(buf, static_cast<std::size_t>(more));
                }
            }
        }
    }

    Request req = server->parse_request(raw);

    // Public routes (no session required)
    if (req.path == "/ui-api/auth/login") {
        server->handle_auth_login(raw, client_fd);
        return;
    }
    if (req.path == "/ui-api/auth/logout") {
        server->handle_auth_logout(raw, client_fd);
        return;
    }
    if (req.path == "/ui-api/health" || req.path == "/api/health" || req.path == "/ui-api/api/health") {
        server->proxy_to_api(raw, client_fd);
        return;
    }

    // ACME HTTP-01 challenge file serving (before auth, always public)
    if (req.path.find("/.well-known/acme-challenge/") == 0 && req.method == "GET") {
        // Read challenge file from admin SSL directory or sites directory
        // Format: /.well-known/acme-challenge/<token>
        std::string prefix = "/.well-known/acme-challenge/";
        std::string token = req.path.substr(prefix.size());
        std::string paths[] = {
            server->services_.config().data_root() + "/ssl/0/.well-known/acme-challenge/" + token,
            server->services_.config().data_root() + "/ssl/.well-known/acme-challenge/" + token,
        };
        std::string content;
        for (const auto& p : paths) {
            std::ifstream f(p);
            if (f.is_open()) {
                std::getline(f, content, '\0');
                break;
            }
        }
        if (!content.empty()) {
            std::ostringstream resp;
            resp << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/octet-stream\r\n"
                 << "Content-Length: " << content.size() << "\r\n"
                 << "\r\n"
                 << content;
            std::string rs = resp.str();
            ::write(client_fd, rs.data(), rs.size());
        } else {
            server->send_json(client_fd, 404, "{\"success\":false,\"error\":\"Challenge token not found\"}");
        }
        ::close(client_fd);
        return;
    }

    // Auth routes that require session
    if (req.path == "/ui-api/auth/change-password") {
        server->handle_auth_change_password(raw, client_fd);
        return;
    }
    if (req.path == "/ui-api/auth/me") {
        server->handle_auth_me(raw, client_fd);
        return;
    }

    if (req.path.find("/sql-console/internal/auth/") == 0) {
        server->handle_sql_console_auth(req, raw, client_fd);
        return;
    }

    if (req.path == "/sql-console/internal/redeem") {
        server->handle_sql_console_redeem(raw, client_fd);
        return;
    }

    if (req.path == "/sql-console/internal/logout") {
        server->handle_sql_console_logout(raw, client_fd);
        return;
    }

    // All other /ui-api/* routes require session
    if (req.path.find("/ui-api/") == 0) {
        if (!server->require_session(raw, client_fd)) {
            return;
        }
        server->proxy_to_api(raw, client_fd);
        return;
    }

    // Proxy /api/* requests to the API server (for Reverse Proxy access)
    if (req.path.find("/api/") == 0) {
        if (!server->require_session(raw, client_fd)) {
            return;
        }
        server->proxy_to_api(raw, client_fd);
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
        send_json(client_fd, 502, "{\"success\":false,\"error\":\"API unavailable\"}");
        ::close(sock);
        return;
    }

    // Rewrite path: /ui-api/... -> /api/...
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
    services_.logger().info("Web UI: Login required");

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
