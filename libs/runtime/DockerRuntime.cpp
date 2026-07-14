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
                                  : core::OperationResult{false, "Docker Compose is not installed."};
    }
    compose_checked_ = true;

    // Try docker compose plugin (new style)
    if (std::system("docker compose version > /dev/null 2>&1") == 0) {
        compose_available_ = true;
        use_docker_compose_ = true;
        return {true, ""};
    }

    // Try docker-compose standalone binary (old style)
    if (std::system("docker-compose version > /dev/null 2>&1") == 0) {
        compose_available_ = true;
        use_docker_compose_ = false;
        return {true, ""};
    }

    compose_available_ = false;
    return {false, "Docker Compose is not installed."};
}

core::OperationResult DockerRuntime::run_command(const std::string& site_dir, const std::string& compose_cmd) {
    if (!check_docker()) {
        return {false, "Docker is not installed."};
    }

    auto check = check_compose();
    if (!check.success) {
        return check;
    }

    // Build the actual command using the correct compose variant
    std::string cmd;
    if (use_docker_compose_) {
        cmd = "cd " + site_dir + " && docker compose " + compose_cmd + " 2>&1";
    } else {
        cmd = "cd " + site_dir + " && docker-compose " + compose_cmd + " 2>&1";
    }

    int rc = std::system(cmd.c_str());

    int exit_code = -1;
    if (WIFEXITED(rc)) {
        exit_code = WEXITSTATUS(rc);
    }

    std::string label = use_docker_compose_ ? "docker compose " + compose_cmd
                                            : "docker-compose " + compose_cmd;
    logger_.info("DOCKER", label + " [" + std::to_string(exit_code) + "]");

    if (rc != 0) {
        return {false, "Command failed with exit code " + std::to_string(exit_code)};
    }
    return {true, ""};
}

core::OperationResult DockerRuntime::create_site_stack(const std::string& domain) {
    return run_command(sites_root_ + domain + "/", "up -d");
}

core::OperationResult DockerRuntime::start_site(const std::string& domain) {
    return create_site_stack(domain);
}

core::OperationResult DockerRuntime::stop_site(const std::string& domain) {
    return run_command(sites_root_ + domain + "/", "stop");
}

core::OperationResult DockerRuntime::remove_site(const std::string& domain) {
    return run_command(sites_root_ + domain + "/", "down");
}

core::OperationResult DockerRuntime::status(const std::string& domain) {
    return run_command(sites_root_ + domain + "/", "ps");
}

core::OperationResult DockerRuntime::connect_mail_network(
    uint64_t site_id, const std::string& domain) {
    if (!check_docker()) {
        return {false, "Docker is not installed."};
    }

    // Check if already connected
    std::string check_cmd = "docker inspect site-" + std::to_string(site_id) + "-php"
        " --format '{{range $k,$v:=.NetworkSettings.Networks}}{{$k}} {{end}}'"
        " 2>/dev/null | grep -q containercp-mail";

    if (std::system(check_cmd.c_str()) == 0) {
        // Already connected — idempotent
        logger_.info("MAIL", "site-" + std::to_string(site_id) + " already on containercp-mail");
        return {true, ""};
    }

    // Connect
    std::string cmd = "docker network connect containercp-mail site-"
        + std::to_string(site_id) + "-php 2>&1";
    int rc = std::system(cmd.c_str());
    int exit_code = -1;
    if (WIFEXITED(rc)) exit_code = WEXITSTATUS(rc);

    if (rc != 0) {
        logger_.warning("MAIL", "Failed to connect site-" + std::to_string(site_id)
                     + " to containercp-mail [" + std::to_string(exit_code) + "]");
        // Non-fatal — network may not exist yet
        return {true, "Network may not exist yet. Mail module must be active."};
    }

    logger_.info("MAIL", "Connected site-" + std::to_string(site_id) + " to containercp-mail");
    return {true, ""};
}

core::OperationResult DockerRuntime::disconnect_mail_network(
    uint64_t site_id, const std::string& domain) {
    (void)domain;
    if (!check_docker()) {
        return {false, "Docker is not installed."};
    }

    std::string cmd = "docker network disconnect containercp-mail site-"
        + std::to_string(site_id) + "-php 2>&1";
    std::system(cmd.c_str());
    return {true, ""};
}

core::OperationResult DockerRuntime::sync_site_mail(uint64_t site_id) {
    (void)site_id;
    // Trigger config regeneration via RuntimeSync::sync("mail")
    // This is handled by the daemon's runtime sync mechanism
    // For now, use a simple Postfix reload
    std::string cmd = "docker exec containercp-mail-postfix postfix reload 2>&1";
    int rc = std::system(cmd.c_str());

    if (rc != 0) {
        logger_.warning("MAIL", "Postfix reload returned non-zero");
    }

    // Also sync Dovecot config
    cmd = "docker exec containercp-mail-dovecot doveadm reload 2>&1";
    std::system(cmd.c_str());

    return {true, ""};
}

} // namespace containercp::runtime
