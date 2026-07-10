#include "core/Application.h"
#include "core/StartupManager.h"
#include "api/ApiServer.h"
#include "api/WebServer.h"
#include "daemon/DaemonApp.h"
#include "daemon/CommandProtocol.h"

#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

// Path for single-instance PID file
static const char* PID_FILE = "/srv/containercp/containercpd.pid";

static int parse_arg(int argc, char* argv[], const std::string& name, int default_val) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == name) {
            return std::stoi(argv[i + 1]);
        }
    }
    return default_val;
}

static int parse_env(const std::string& name, int default_val) {
    const char* val = std::getenv(name.c_str());
    if (val != nullptr) return std::stoi(val);
    return default_val;
}

static bool is_process_alive(pid_t pid) {
    return ::kill(pid, 0) == 0;
}

static bool acquire_single_instance(const std::string& data_root) {
    ::mkdir((data_root + "/").c_str(), 0755);

    std::ifstream pid_file(PID_FILE);
    if (pid_file.is_open()) {
        pid_t existing_pid = 0;
        pid_file >> existing_pid;
        pid_file.close();

        if (existing_pid > 0 && is_process_alive(existing_pid)) {
            std::cerr << "[ERROR] [SYSTEM] Another daemon instance is already running (PID " << existing_pid << ")." << std::endl;
            std::cerr << "[ERROR] [SYSTEM] If the daemon crashed, remove " << PID_FILE << " and try again." << std::endl;
            return false;
        }
        ::unlink(PID_FILE);
    }

    std::ofstream out(PID_FILE);
    if (!out.is_open()) {
        std::cerr << "[ERROR] [SYSTEM] Failed to write PID file: " << PID_FILE << std::endl;
        return false;
    }
    out << ::getpid();
    out.close();
    return true;
}

static void release_single_instance() {
    ::unlink(PID_FILE);
}

int main(int argc, char* argv[]) {
    // Ensure data directory exists for PID file
    ::mkdir("/srv/containercp", 0755);

    if (!acquire_single_instance("/srv/containercp")) {
        return 1;
    }
    struct Cleanup { ~Cleanup() { release_single_instance(); } } cleanup;

    // Load server hostname from env or file
    std::string server_hostname;
    {
        const char* env = std::getenv("SERVER_HOSTNAME");
        if (env != nullptr && env[0] != '\0') {
            server_hostname = env;
        } else {
            std::ifstream f("/srv/containercp/server_hostname");
            if (f.is_open()) std::getline(f, server_hostname);
        }
    }

    // Decide: bootstrap or normal mode
    if (containercp::core::StartupManager::needs_bootstrap("/srv/containercp", server_hostname)) {
        // Run Setup Wizard on 0.0.0.0:80
        std::cerr << "[SYSTEM] Starting in BOOTSTRAP mode." << std::endl;
        std::cerr << "[SYSTEM] Open http://<server-ip>/ in your browser to configure." << std::endl;
        return containercp::core::StartupManager::run_bootstrap("/srv/containercp");
    }

    // === Normal Mode ===
    containercp::core::Application::instance();
    auto& services = containercp::core::Application::instance().services();
    auto& log = services.logger();

    // Load persisted settings (server_hostname from env or file)
    services.config().load_server_hostname();
    log.info("SYSTEM", "Server hostname: '" + services.config().server_hostname() + "'");

    // Ensure database directory exists
    services.filesystem().create_directory(services.config().database_dir());

    int api_port = parse_env("CONTAINERCP_API_PORT", parse_arg(argc, argv, "--api-port", 8080));
    int ui_port = parse_env("CONTAINERCP_UI_PORT", parse_arg(argc, argv, "--ui-port", 8081));

    const std::string socket_path = services.config().data_root() + "/containercpd.sock";

    // Startup recovery — verify all required directories
    log.info("SYSTEM", "Verifying required directories...");
    services.filesystem().create_directory(services.config().database_dir());
    services.filesystem().create_directory(services.config().data_root() + "/ssl/");
    services.filesystem().create_directory(services.config().data_root() + "/proxy/");
    services.filesystem().create_directory(services.config().data_root() + "/proxy/sites/");
    services.filesystem().create_directory(services.config().data_root() + "/backups/");
    services.filesystem().create_directory(services.config().log_root());

    // Start REST API server on localhost only (background thread)
    containercp::api::ApiServer api_server(services, api_port);
    std::thread api_thread([&api_server]() { api_server.start(); });
    api_thread.detach();

    // Detect Docker bridge gateway for Web UI binding.
    // The Web UI binds to the Docker gateway IP so the nginx proxy container
    // (running inside Docker) can reach it. 127.0.0.1 would not work because
    // the nginx container cannot reach the host's loopback interface.
    std::string ui_bind = containercp::core::ServiceRegistry::detect_docker_gateway(log);
    log.info("SYSTEM", "Web UI bind address: " + ui_bind);

    // Start Web UI server on Docker bridge gateway (external access only through reverse proxy)
    containercp::api::WebServer web_server(services, ui_bind, ui_port, api_port);
    std::thread web_thread([&web_server]() { web_server.start(); });
    web_thread.detach();

    // Remove old socket file
    ::unlink(socket_path.c_str());

    // Create UNIX socket server
    int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) { log.error("SYSTEM", "Failed to create UNIX socket"); return 1; }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log.error("SYSTEM", "Failed to bind UNIX socket");
        ::close(server_fd);
        return 1;
    }
    ::chmod(socket_path.c_str(), 0666);
    if (::listen(server_fd, 5) < 0) {
        log.error("SYSTEM", "Failed to listen on UNIX socket");
        ::close(server_fd);
        return 1;
    }

    log.info("SYSTEM", "Listening on " + socket_path);
    log.info("SYSTEM", "REST API on 127.0.0.1:" + std::to_string(api_port));
    log.info("SYSTEM", "Web UI on 127.0.0.1:" + std::to_string(ui_port));
    log.info("SYSTEM", "Login required");

    // Startup recovery — ensure central proxy container and network
    log.info("SYSTEM", "Ensuring central proxy...");
    auto proxy_result = services.proxy_provider().ensure_central_proxy();
    if (!proxy_result.success) {
        log.error("SYSTEM", "Failed to ensure central proxy: " + proxy_result.message);
    }

    // If proxy failed, log and continue in degraded mode.
    // RecoveryManager will retry proxy recovery at runtime.
    // Do NOT mark setup as incomplete — that would force bootstrap mode
    // on next restart, creating a crash loop when port 80 is already in use.
    if (!proxy_result.success) {
        log.warning("SYSTEM", "Central proxy unavailable. "
                    "RecoveryManager will retry automatically.");
    }

    containercp::daemon::DaemonApp daemon(services);
    services.start();

    while (true) {
        struct sockaddr_un client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) { log.error("SYSTEM", "Accept failed"); break; }
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
    services.shutdown();
    return 0;
}
