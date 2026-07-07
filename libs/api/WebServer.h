#ifndef CONTAINERCP_API_WEB_SERVER_H
#define CONTAINERCP_API_WEB_SERVER_H

#include "api/Request.h"
#include "api/Response.h"
#include "core/ServiceRegistry.h"

#include <string>

namespace containercp::api {

class WebServer {
public:
    WebServer(core::ServiceRegistry& services, const std::string& bind_addr, int port);

    bool start();
    void stop();

private:
    static void handle_client(int client_fd, WebServer* server);
    Response serve_static(const std::string& path) const;
    Request parse_request(int client_fd) const;

    std::string bind_addr_;
    int port_;
    int server_fd_ = -1;
    bool running_ = false;
    core::ServiceRegistry& services_;
};

} // namespace containercp::api

#endif // CONTAINERCP_API_WEB_SERVER_H
