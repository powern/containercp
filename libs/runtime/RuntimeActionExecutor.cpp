#include "RuntimeActionExecutor.h"

#include <algorithm>
#include <cctype>
#include <sstream>

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

core::OperationResult RuntimeActionExecutor::restart_services(
    const std::string& compose_dir,
    const std::vector<std::string>& services) {
    return compose_action(compose_dir, "restart", services);
}

} // namespace containercp::runtime
