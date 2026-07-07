#ifndef CONTAINERCP_API_WEB_SERVER_H
#define CONTAINERCP_API_WEB_SERVER_H

#include "api/Request.h"
#include "api/Response.h"
#include "core/ServiceRegistry.h"

#include <string>

namespace containercp::api {

class WebServer {
public:
    WebServer(core::ServiceRegistry& services, const std::string& bind_addr, int port, int api_port);

    bool start();
    void stop();

private:
    static void handle_client(int client_fd, WebServer* server);
    Response serve_static(const std::string& path) const;
    Request parse_request(const std::string& raw) const;
    void proxy_to_api(const std::string& raw_request, int client_fd);
    void handle_auth_login(const std::string& raw_request, int client_fd);
    void handle_auth_change_password(const std::string& raw_request, int client_fd);
    void handle_auth_logout(const std::string& raw_request, int client_fd);
    void handle_auth_me(const std::string& raw_request, int client_fd);
    bool require_session(const std::string& raw_request, int client_fd);
    std::string extract_session_token(const std::string& raw_request) const;
    void send_json(int client_fd, int status, const std::string& body);
    void send_unauthorized(int client_fd);

    std::string bind_addr_;
    int port_;
    int api_port_;
    int server_fd_ = -1;
    bool running_ = false;
    core::ServiceRegistry& services_;
};

} // namespace containercp::api

#endif // CONTAINERCP_API_WEB_SERVER_H
