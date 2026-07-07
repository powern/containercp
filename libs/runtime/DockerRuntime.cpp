#include "DockerRuntime.h"

#include <cstdlib>
#include <sys/wait.h>

namespace containercp::runtime {

DockerRuntime::DockerRuntime(logger::Logger& logger, const std::string& sites_root)
    : logger_(logger)
    , sites_root_(sites_root)
{
}

bool DockerRuntime::check_docker() {
    if (docker_checked_) {
        return docker_available_;
    }
    docker_checked_ = true;
    constexpr const char* cmd = "docker --version > /dev/null 2>&1";
    int rc = std::system(cmd);
    docker_available_ = (rc == 0);
    return docker_available_;
}

core::OperationResult DockerRuntime::check_compose() {
    if (!check_docker()) {
        return {false, "Docker is not installed."};
    }
    if (compose_checked_) {
        return compose_available_ ? core::OperationResult{true, ""}
                                  : core::OperationResult{false, "Docker Compose plugin is not installed."};
    }
    compose_checked_ = true;
    // Check docker compose plugin first, then standalone binary
    if (std::system("docker compose version > /dev/null 2>&1") == 0) {
        compose_available_ = true;
        return {true, ""};
    }
    if (std::system("docker-compose --version > /dev/null 2>&1") == 0) {
        compose_available_ = true;
        return {true, ""};
    }
    compose_available_ = false;
    return {false, "Docker Compose plugin is not installed."};
}

core::OperationResult DockerRuntime::run_command(const std::string& site_dir, const std::string& command) {
    if (!check_docker()) {
        return {false, "Docker is not installed."};
    }

    // Check compose availability and determine the correct command
    auto compose_check = check_compose();
    if (!compose_check.success) {
        return compose_check;
    }

    // Replace "docker compose" with the correct command
    std::string actual_cmd = command;
    auto pos = actual_cmd.find("docker compose");
    if (pos != std::string::npos) {
        // Determine which compose command to use
        static bool checked = false;
        static bool use_docker_compose = true;
        if (!checked) {
            checked = true;
            use_docker_compose = (std::system("docker compose version > /dev/null 2>&1") == 0);
        }
        if (!use_docker_compose) {
            actual_cmd.replace(pos, 14, "docker-compose");
        }
    }

    std::string shell_cmd = "cd " + site_dir + " && " + actual_cmd + " 2>&1";
    int rc = std::system(shell_cmd.c_str());

    int exit_code = -1;
    if (WIFEXITED(rc)) {
        exit_code = WEXITSTATUS(rc);
    }

    logger_.info(actual_cmd + " [" + std::to_string(exit_code) + "]");

    if (rc != 0) {
        return {false, "Command failed with exit code " + std::to_string(exit_code)};
    }
    return {true, ""};
}

core::OperationResult DockerRuntime::create_site_stack(const std::string& domain) {
    return run_command(sites_root_ + domain + "/", "docker compose up -d");
}

core::OperationResult DockerRuntime::start_site(const std::string& domain) {
    return create_site_stack(domain);
}

core::OperationResult DockerRuntime::stop_site(const std::string& domain) {
    return run_command(sites_root_ + domain + "/", "docker compose stop");
}

core::OperationResult DockerRuntime::remove_site(const std::string& domain) {
    return run_command(sites_root_ + domain + "/", "docker compose down");
}

core::OperationResult DockerRuntime::status(const std::string& domain) {
    return run_command(sites_root_ + domain + "/", "docker compose ps");
}

} // namespace containercp::runtime
