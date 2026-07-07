#include "core/Application.h"
#include "api/ApiServer.h"
#include "daemon/DaemonApp.h"
#include "daemon/CommandProtocol.h"

#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

int main(int argc, char* argv[]) {
    containercp::core::Application::instance();
    auto& services = containercp::core::Application::instance().services();

    int api_port = 8080;

    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--api-port") {
            api_port = std::stoi(argv[i + 1]);
            break;
        }
    }

    const char* env_port = std::getenv("CONTAINERCP_API_PORT");
    if (env_port != nullptr) {
        api_port = std::stoi(env_port);
    }

    const std::string socket_path = services.config().data_root() + "/containercpd.sock";

    // Start REST API server in background thread
    containercp::api::ApiServer api_server(services, api_port);
    std::thread api_thread([&api_server]() {
        api_server.start();
    });
    api_thread.detach();

    // Remove old socket file
    ::unlink(socket_path.c_str());

    // Create UNIX socket server
    int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        services.logger().error("Daemon: Failed to create UNIX socket");
        return 1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        services.logger().error("Daemon: Failed to bind UNIX socket");
        ::close(server_fd);
        return 1;
    }

    ::chmod(socket_path.c_str(), 0666);

    if (::listen(server_fd, 5) < 0) {
        services.logger().error("Daemon: Failed to listen on UNIX socket");
        ::close(server_fd);
        return 1;
    }

    services.logger().info("Daemon: Listening on " + socket_path);
    services.logger().info("Daemon: REST API on 127.0.0.1:" + std::to_string(api_port));

    containercp::daemon::DaemonApp daemon(services);

    // Accept loop
    while (true) {
        struct sockaddr_un client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            services.logger().error("Daemon: Accept failed");
            break;
        }

        char buf[65536];
        ssize_t n = ::read(client_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string response = daemon.handle_command(std::string(buf));
            ::write(client_fd, response.c_str(), response.size());
        }
        ::close(client_fd);
    }

    ::close(server_fd);
    ::unlink(socket_path.c_str());
    return 0;
}
