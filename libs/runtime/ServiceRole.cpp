#include "ServiceRole.h"

namespace containercp::runtime {

std::string role_to_action_suffix(ServiceRole role) {
    switch (role) {
        case ServiceRole::Frontend: return "web";
        case ServiceRole::PHP:      return "php";
        case ServiceRole::Database: return "db";
        case ServiceRole::Cache:    return "redis";
    }
    return "";
}

std::vector<ServiceRole> roles_from_action(const std::string& action) {
    if (action == "restart-web")    return {ServiceRole::Frontend};
    if (action == "restart-php")    return {ServiceRole::PHP};
    if (action == "restart-db")     return {ServiceRole::Database};
    if (action == "restart-redis")  return {ServiceRole::Cache};
    if (action == "restart-all")    return {};  // empty = all compose services
    return {};
}

std::string role_to_compose_service(ServiceRole role) {
    switch (role) {
        case ServiceRole::Frontend: return "web";
        case ServiceRole::PHP:      return "php";
        case ServiceRole::Database: return "mariadb";
        case ServiceRole::Cache:    return "redis";
    }
    return "";
}

std::vector<std::string> roles_to_compose_services(const std::vector<ServiceRole>& roles) {
    if (roles.empty()) return {};  // empty = all services
    std::vector<std::string> names;
    names.reserve(roles.size());
    for (auto role : roles) {
        names.push_back(role_to_compose_service(role));
    }
    return names;
}

} // namespace containercp::runtime
