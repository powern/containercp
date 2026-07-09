#include "RuntimeActionExecutor.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace containercp::runtime {

namespace {

std::string trim(const std::string& s) {
    size_t start = 0, end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\n' || s[start] == '\r')) ++start;
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\n' || s[end-1] == '\r')) --end;
    return s.substr(start, end - start);
}

} // anonymous namespace

RuntimeActionExecutor::RuntimeActionExecutor(logger::Logger& logger)
    : logger_(logger)
{
}

core::OperationResult RuntimeActionExecutor::check_available() {
    auto docker_check = executor_.run({"docker", "--version"});
    if (docker_check.exit_code != 0) {
        return {false, "Docker is not available"};
    }
    auto compose_check = executor_.run({"docker", "compose", "version"});
    if (compose_check.exit_code != 0) {
        return {false, "Docker Compose plugin is not available"};
    }
    return {true, ""};
}

core::OperationResult RuntimeActionExecutor::compose_action(
    const std::string& compose_dir,
    const std::string& subcommand,
    const std::vector<std::string>& services) {

    std::vector<std::string> args;
    args.reserve(5 + services.size());
    args.push_back("docker");
    args.push_back("compose");
    args.push_back("--project-directory");
    args.push_back(compose_dir);
    args.push_back(subcommand);
    for (const auto& svc : services) {
        args.push_back(svc);
    }

    auto result = executor_.run(args);

    // Build a log-friendly description
    std::ostringstream desc;
    desc << subcommand;
    if (!services.empty()) {
        desc << " [";
        for (size_t i = 0; i < services.size(); ++i) {
            if (i > 0) desc << ", ";
            desc << services[i];
        }
        desc << "]";
    }

    if (result.exit_code != 0) {
        std::string err = trim(result.err);
        if (err.empty()) err = trim(result.out);
        logger_.error("COMPOSE",
            compose_dir + " " + desc.str() +
            " exit=" + std::to_string(result.exit_code) +
            " stderr=" + err);
        return {false, "docker compose " + desc.str() + " failed: " + err};
    }

    logger_.info("COMPOSE",
        compose_dir + " " + desc.str() + " ok");
    return {true, "docker compose " + desc.str() + " completed"};
}

std::vector<std::string> RuntimeActionExecutor::list_services(const std::string& compose_dir) {
    auto result = executor_.run({
        "docker", "compose", "--project-directory", compose_dir,
        "config", "--services"
    });
    if (result.exit_code != 0) return {};

    std::vector<std::string> services;
    std::istringstream stream(result.out);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (!line.empty()) services.push_back(line);
    }
    return services;
}

ContainerStatus RuntimeActionExecutor::service_status(
    const std::string& compose_dir,
    const std::string& service) const {

    ContainerStatus result;
    result.name = service;

    // Step 1: get container name from docker compose ps
    auto ps_result = executor_.run({
        "docker", "compose", "--project-directory", compose_dir,
        "ps", "--format", "{{.Name}}", service
    });

    if (ps_result.exit_code != 0) {
        logger_.error("RT_STAT",
            "docker compose ps failed for " + compose_dir + "/" + service +
            " exit=" + std::to_string(ps_result.exit_code) +
            " stderr=" + trim(ps_result.err));
        result.status = "Error";
        return result;
    }

    std::string container_name = trim(ps_result.out);
    if (container_name.empty()) {
        result.status = "Stopped";
        return result;
    }
    result.name = container_name;

    // Step 2: inspect container state and health
    auto inspect_result = executor_.run({
        "docker", "inspect", container_name,
        "--format", "{{.State.Status}}|{{.State.Health.Status}}"
    });

    if (inspect_result.exit_code != 0 || inspect_result.out.empty()) {
        logger_.error("RT_STAT",
            "docker inspect failed for " + container_name +
            " exit=" + std::to_string(inspect_result.exit_code) +
            " stderr=" + trim(inspect_result.err));
        result.status = "Error";
        return result;
    }

    std::string combined = trim(inspect_result.out);
    size_t sep = combined.find('|');
    std::string state = (sep != std::string::npos) ? combined.substr(0, sep) : combined;
    std::string health = (sep != std::string::npos) ? combined.substr(sep + 1) : "";
    result.health = health;

    if (state == "running") {
        if (health == "unhealthy") result.status = "Unhealthy";
        else if (health == "starting") result.status = "Starting";
        else result.status = "Running";
    } else if (state == "exited" || state == "paused" || state == "removing") {
        result.status = "Stopped";
    } else if (state == "restarting") {
        result.status = "Starting";
    } else if (state == "created") {
        result.status = "Stopped";
    } else {
        result.status = "Unknown";
    }

    return result;
}

core::OperationResult RuntimeActionExecutor::restart_services(
    const std::string& compose_dir,
    const std::vector<std::string>& services) {
    return compose_action(compose_dir, "restart", services);
}

} // namespace containercp::runtime
