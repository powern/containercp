#include "core/Application.h"
#include "cli/CommandDispatcher.h"
#include "api/ApiServer.h"

#include <thread>

int main(int argc, char* argv[]) {
    containercp::core::Application::instance();

    int api_port = 8080;

    // Parse --api-port from CLI args
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--api-port") {
            api_port = std::stoi(argv[i + 1]);
            break;
        }
    }

    // Check environment variable
    const char* env_port = std::getenv("CONTAINERCP_API_PORT");
    if (env_port != nullptr) {
        api_port = std::stoi(env_port);
    }

    auto& services = containercp::core::Application::instance().services();

    // Start API server in background thread
    containercp::api::ApiServer api_server(services, api_port);
    std::thread api_thread([&api_server]() {
        api_server.start();
    });
    api_thread.detach();

    return containercp::cli::CommandDispatcher::run(argc, argv);
}
