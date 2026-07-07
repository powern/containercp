#include "DockerRuntime.h"

#include <cstdlib>
#include <string>

namespace containercp::runtime {

DockerRuntime::DockerRuntime(logger::Logger& logger, const std::string& sites_root)
    : logger_(logger)
    , sites_root_(sites_root)
{
}

bool DockerRuntime::check_docker() {
    int rc = std::system("docker --version > /dev/null 2>&1");
    return rc == 0;
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
    return run_command(sites_root_ + domain + "/", "docker compose up -d");
}

core::OperationResult DockerRuntime::stop_site(const std::string& domain) {
    return run_command(sites_root_ + domain + "/", "docker compose stop");
}

core::OperationResult DockerRuntime::remove_site(const std::string& domain) {
    return run_command(sites_root_ + domain + "/", "docker compose down");
}

core::OperationResult DockerRuntime::status(const std::string& domain) {
    if (!check_docker()) {
        return {false, "Docker is not installed."};
    }

    std::string site_dir = sites_root_ + domain + "/";
    std::string cmd = "cd " + site_dir + " && docker compose ps 2>&1";
    int rc = std::system(cmd.c_str());

    logger_.info("docker compose ps [" + std::to_string(rc) + "]");

    if (rc != 0) {
        return {false, "Command failed with exit code " + std::to_string(rc)};
    }
    return {true, ""};
}

} // namespace containercp::runtime
