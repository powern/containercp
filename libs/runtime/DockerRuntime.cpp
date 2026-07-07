#include "DockerRuntime.h"

#include <cstdlib>

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

    std::string cmd = "cd " + site_dir + " && " + command + " 2>&1";
    int rc = std::system(cmd.c_str());

    logger_.info(command + " [" + std::to_string(rc) + "]");

    if (rc != 0) {
        return {false, "Command failed with exit code " + std::to_string(rc)};
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
