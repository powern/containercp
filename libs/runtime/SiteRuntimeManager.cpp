#include "SiteRuntimeManager.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/wait.h>

namespace containercp::runtime {

static std::string trim(const std::string& s) {
    size_t start = 0, end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\n' || s[start] == '\r')) ++start;
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\n' || s[end-1] == '\r')) --end;
    return s.substr(start, end - start);
}

SiteRuntimeManager::SiteRuntimeManager(logger::Logger& logger, const std::string& sites_root)
    : logger_(logger)
    , sites_root_(sites_root)
{
}

std::string SiteRuntimeManager::container_status(const std::string& project, const std::string& service) const {
    std::string out_file = "/tmp/containercp-rt-" + project + "-" + service + ".txt";
    // Get container name from compose project
    std::string name_cmd = "docker compose --project-name " + project + " ps --format '{{.Name}}' " + service + " 2>/dev/null > " + out_file;
    std::system(name_cmd.c_str());
    std::ifstream name_in(out_file);
    std::string container_name;
    std::getline(name_in, container_name);
    name_in.close();
    container_name = trim(container_name);

    if (container_name.empty()) {
        std::remove(out_file.c_str());
        return "Stopped";
    }

    // Get container state via docker inspect
    std::string state_cmd = "docker inspect --format '{{.State.Status}}' " + container_name + " 2>/dev/null > " + out_file;
    std::system(state_cmd.c_str());
    std::ifstream state_in(out_file);
    std::string state;
    std::getline(state_in, state);
    state_in.close();
    state = trim(state);

    // Also check health
    std::string health_cmd = "docker inspect --format '{{.State.Health.Status}}' " + container_name + " 2>/dev/null > " + out_file;
    std::system(health_cmd.c_str());
    std::ifstream health_in(out_file);
    std::string health;
    std::getline(health_in, health);
    health_in.close();
    health = trim(health);

    std::remove(out_file.c_str());

    if (state == "running") {
        if (health == "unhealthy") return "Unhealthy";
        if (health == "starting") return "Starting";
        return "Running";
    }
    if (state == "exited" || state == "paused") return "Stopped";
    if (state == "restarting") return "Starting";
    return "Unknown";
}

SiteRuntimeStatus SiteRuntimeManager::get_status(uint64_t site_id) const {
    SiteRuntimeStatus s;
    std::string project = "site-" + std::to_string(site_id);

    s.web.status = container_status(project, "web");
    s.php.status = container_status(project, "php");

    // Default values for future use
    s.web.name = project + "-web";
    s.php.name = project + "-php";

    return s;
}

} // namespace containercp::runtime
