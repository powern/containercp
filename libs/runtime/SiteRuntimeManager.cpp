#include "SiteRuntimeManager.h"
#include "runtime/ServiceRole.h"

#include <algorithm>
#include <cctype>

namespace containercp::runtime {

namespace {

std::string trim(const std::string& s) {
    size_t start = 0, end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\n' || s[start] == '\r')) ++start;
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\n' || s[end-1] == '\r')) --end;
    return s.substr(start, end - start);
}

std::vector<std::string> make_actions() {
    return {"restart-web", "restart-php", "restart-db", "restart-redis", "restart-all"};
}

} // anonymous namespace

const std::vector<std::string>& SiteRuntimeManager::valid_actions() {
    static const std::vector<std::string> actions = make_actions();
    return actions;
}

std::string SiteRuntimeManager::path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    bool a_has_slash = (a.back() == '/');
    bool b_has_slash = (b.front() == '/');
    if (a_has_slash && b_has_slash) return a + b.substr(1);
    if (!a_has_slash && !b_has_slash) return a + '/' + b;
    return a + b;
}

SiteRuntimeManager::SiteRuntimeManager(logger::Logger& logger,
                                       const std::string& sites_root)
    : logger_(logger)
    , sites_root_(sites_root)
{
    if (!sites_root_.empty() && sites_root_.back() == '/') {
        sites_root_.pop_back();
    }
}

std::vector<std::string> SiteRuntimeManager::services_for_action(
    const std::string& action) const {
    return roles_to_compose_services(roles_from_action(action));
}

std::string SiteRuntimeManager::container_status(const std::string& compose_dir,
                                                  const std::string& service) const {
    auto ps_result = executor_.run({
        "docker", "compose", "--project-directory", compose_dir,
        "ps", "--format", "{{.Name}}", service
    });

    if (ps_result.exit_code != 0) {
        logger_.error("SITE_RT",
            "docker compose ps failed for " + compose_dir + "/" + service +
            " exit=" + std::to_string(ps_result.exit_code) +
            " stderr=" + trim(ps_result.err));
        return "Error";
    }

    std::string container_name = trim(ps_result.out);
    if (container_name.empty()) {
        return "Stopped";
    }

    auto inspect_result = executor_.run({
        "docker", "inspect", container_name,
        "--format", "{{.State.Status}}|{{.State.Health.Status}}"
    });

    if (inspect_result.exit_code != 0 || inspect_result.out.empty()) {
        logger_.error("SITE_RT",
            "docker inspect failed for " + container_name +
            " exit=" + std::to_string(inspect_result.exit_code) +
            " stderr=" + trim(inspect_result.err));
        return "Error";
    }

    std::string combined = trim(inspect_result.out);
    size_t sep = combined.find('|');
    std::string state = (sep != std::string::npos) ? combined.substr(0, sep) : combined;
    std::string health = (sep != std::string::npos) ? combined.substr(sep + 1) : "";

    if (state == "running") {
        if (health == "unhealthy") return "Unhealthy";
        if (health == "starting") return "Starting";
        return "Running";
    }
    if (state == "exited" || state == "paused" || state == "removing") return "Stopped";
    if (state == "restarting") return "Starting";
    if (state == "created") return "Stopped";
    return "Unknown";
}

SiteRuntimeStatus SiteRuntimeManager::get_status(uint64_t site_id,
                                                  const std::string& domain) const {
    (void)site_id;
    SiteRuntimeStatus s;
    std::string compose_dir = path_join(sites_root_, domain);

    s.web.status = container_status(compose_dir, "web");
    s.php.status = container_status(compose_dir, "php");
    s.db.status = container_status(compose_dir, "mariadb");
    s.cache.status = container_status(compose_dir, "redis");
    s.web.name = domain + "-web";
    s.php.name = domain + "-php";
    s.db.name = domain + "-mariadb";
    s.cache.name = domain + "-redis";

    return s;
}

} // namespace containercp::runtime
