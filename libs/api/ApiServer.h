#ifndef CONTAINERCP_API_API_SERVER_H
#define CONTAINERCP_API_API_SERVER_H

#include "api/Router.h"
#include "api/AuthMiddleware.h"
#include "core/ServiceRegistry.h"

#include <memory>
#include <string>

namespace containercp::api {

class ApiServer {
public:
    ApiServer(core::ServiceRegistry& services, int port);

    bool start();
    void stop();

    Router& router();

private:
    static void handle_client(int client_fd, ApiServer* server);
    Request parse_request(int client_fd) const;

    int port_;
    int server_fd_ = -1;
    bool running_ = false;
    Router router_;
    core::ServiceRegistry& services_;
    std::unique_ptr<AuthMiddleware> auth_;
};

} // namespace containercp::api

#endif // CONTAINERCP_API_API_SERVER_H
