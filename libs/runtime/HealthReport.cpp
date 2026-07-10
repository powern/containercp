#include "HealthReport.h"

namespace containercp::runtime {

void HealthRegistry::register_check(const std::string& name,
                                     HealthCheck check) {
    checks_[name] = std::move(check);
}

HealthReport HealthRegistry::check(const std::string& name) {
    auto it = checks_.find(name);
    if (it == checks_.end()) {
        HealthReport r;
        r.status = "error";
        r.services.push_back({"registry", "error", "No check registered: " + name});
        return r;
    }
    return it->second();
}

std::vector<std::pair<std::string, HealthReport>> HealthRegistry::check_all() {
    std::vector<std::pair<std::string, HealthReport>> results;
    for (auto& [name, check] : checks_) {
        results.emplace_back(name, check());
    }
    return results;
}

} // namespace containercp::runtime
