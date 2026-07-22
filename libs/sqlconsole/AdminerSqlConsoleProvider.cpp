#include "sqlconsole/AdminerSqlConsoleProvider.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace containercp::sqlconsole {
namespace {

bool launch_id_valid(const std::string& value) {
    if (value.size() != 32) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string compose_project_name(const std::string& site_root, const std::string& site_domain) {
    std::string base = std::filesystem::path(site_root).filename().string();
    if (base.empty()) base = site_domain;
    std::string out;
    out.reserve(base.size());
    for (unsigned char c : base) {
        if (std::isalnum(c) != 0 || c == '_' || c == '-') {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out.empty() ? "containercp" : out;
}

SqlConsoleProviderResult failure(std::string code, std::string message) {
    return {false, std::move(code), std::move(message), {}, {}};
}

SqlConsoleProviderResult success(std::string code, std::string message, std::string container_name) {
    const auto upstream = "http://" + container_name + ":8080";
    return {true, std::move(code), std::move(message), std::move(container_name), upstream};
}

} // namespace

CommandExecutorSqlConsoleRuntimeRunner::CommandExecutorSqlConsoleRuntimeRunner(const runtime::CommandExecutor& executor)
    : executor_(executor) {
}

runtime::CommandResult CommandExecutorSqlConsoleRuntimeRunner::run(const std::vector<std::string>& args) const {
    return executor_.run(args);
}

AdminerSqlConsoleProvider::AdminerSqlConsoleProvider(const SqlConsoleRuntimeRunner& runner, std::string image)
    : runner_(runner)
    , image_(std::move(image)) {
}

std::string AdminerSqlConsoleProvider::name() const {
    return "adminer";
}

std::string AdminerSqlConsoleProvider::container_name(const std::string& launch_id) const {
    if (!launch_id_valid(launch_id)) return {};
    return "ccp-sqlconsole-" + launch_id.substr(0, 24);
}

std::string AdminerSqlConsoleProvider::site_network_name(const SqlConsoleProviderLaunchRequest& request) const {
    if (request.site_id == 0) return {};
    return compose_project_name(request.site_root, request.site_domain) + "_containercp-site-" + std::to_string(request.site_id);
}

std::vector<std::string> AdminerSqlConsoleProvider::start_args(const SqlConsoleProviderLaunchRequest& request) const {
    const auto cname = container_name(request.launch_id);
    const auto network = site_network_name(request);
    if (cname.empty() || network.empty()) return {};
    return {
        "docker", "run", "-d", "--rm",
        "--name", cname,
        "--network", network,
        "--label", "containercp.sql_console.provider=adminer",
        "--label", "containercp.sql_console.launch_id=" + request.launch_id,
        "--label", "containercp.site.id=" + std::to_string(request.site_id),
        "--label", "containercp.database.id=" + std::to_string(request.database_id),
        "--pull", "missing",
        image_,
    };
}

SqlConsoleProviderResult AdminerSqlConsoleProvider::start(const SqlConsoleProviderLaunchRequest& request) const {
    if (request.provider != "adminer") return failure("provider_mismatch", "SQL Console provider is not Adminer");
    const auto args = start_args(request);
    if (args.empty()) return failure("launch_request_invalid", "Adminer launch request is incomplete");
    const auto command = runner_.run(args);
    if (command.exit_code != 0) return failure("adminer_start_failed", "Adminer container could not be started");
    return success("adminer_started", "Adminer container started", container_name(request.launch_id));
}

SqlConsoleProviderResult AdminerSqlConsoleProvider::stop(const std::string& launch_id) const {
    const auto cname = container_name(launch_id);
    if (cname.empty()) return failure("launch_id_invalid", "SQL Console launch id is invalid");
    const auto command = runner_.run({"docker", "stop", cname});
    if (command.exit_code != 0) return failure("adminer_stop_failed", "Adminer container could not be stopped");
    return success("adminer_stopped", "Adminer container stopped", cname);
}

SqlConsoleProviderResult AdminerSqlConsoleProvider::status(const std::string& launch_id) const {
    const auto cname = container_name(launch_id);
    if (cname.empty()) return failure("launch_id_invalid", "SQL Console launch id is invalid");
    const auto command = runner_.run({"docker", "inspect", "-f", "{{.State.Running}}", cname});
    if (command.exit_code != 0 || command.out.find("true") == std::string::npos) {
        return failure("adminer_not_running", "Adminer container is not running");
    }
    return success("adminer_running", "Adminer container is running", cname);
}

} // namespace containercp::sqlconsole
