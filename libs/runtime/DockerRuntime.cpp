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

core::OperationResult DockerRuntime::run_command(const std::string& site_dir, const std::string& command) {
    if (!check_docker()) {
        return {false, "Docker is not installed."};
    }

    // Try docker compose first, fall back to docker-compose
    std::string compose_cmd = "docker compose";
    {
        std::string test = compose_cmd + " version > /dev/null 2>&1";
        if (std::system(test.c_str()) != 0) {
            compose_cmd = "docker-compose";
        }
    }

    // Replace "docker compose" with the detected compose command
    std::string actual_cmd = command;
    auto pos = actual_cmd.find("docker compose");
    if (pos != std::string::npos) {
        actual_cmd.replace(pos, 14, compose_cmd);
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
