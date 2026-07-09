#include "SiteRuntimeManager.h"
#include "runtime/ServiceRole.h"

#include <algorithm>

namespace containercp::runtime {

namespace {

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
                                       const std::string& sites_root,
                                       RuntimeActionExecutor& executor)
    : logger_(logger)
    , sites_root_(sites_root)
    , executor_(executor)
{
    if (!sites_root_.empty() && sites_root_.back() == '/') {
        sites_root_.pop_back();
    }
}

std::vector<std::string> SiteRuntimeManager::services_for_action(
    const std::string& action) const {
    return roles_to_compose_services(roles_from_action(action));
}

SiteRuntimeStatus SiteRuntimeManager::get_status(uint64_t site_id,
                                                  const std::string& domain) const {
    (void)site_id;
    SiteRuntimeStatus s;
    std::string compose_dir = path_join(sites_root_, domain);

    s.web   = executor_.service_status(compose_dir, "web");
    s.php   = executor_.service_status(compose_dir, "php");
    s.db    = executor_.service_status(compose_dir, "mariadb");
    s.cache = executor_.service_status(compose_dir, "redis");

    return s;
}

} // namespace containercp::runtime
