#include "Router.h"

namespace containercp::api {

void Router::add(const std::string& method, const std::string& path, RouteHandler handler) {
    routes_.push_back({method, path, std::move(handler), false});
}

void Router::add_prefix(const std::string& method, const std::string& prefix, RouteHandler handler) {
    routes_.push_back({method, prefix, std::move(handler), true});
}

Response Router::dispatch(const Request& req) const {
    // Exact routes always win. This keeps a broad prefix route from
    // swallowing a more specific endpoint registered later.
    for (const auto& route : routes_) {
        if (route.method != req.method) continue;
        if (!route.is_prefix && route.path == req.path) return route.handler(req);
    }

    // Prefix routes are selected by specificity rather than registration
    // order. Nested resources such as /users/<id>/keys/<key_id> therefore
    // cannot be intercepted by /users/<id> handlers.
    const Route* best = nullptr;
    for (const auto& route : routes_) {
        if (route.method != req.method || !route.is_prefix) continue;
        if (req.path.compare(0, route.path.size(), route.path) != 0) continue;
        if (best == nullptr || route.path.size() > best->path.size()) best = &route;
    }
    if (best != nullptr) return best->handler(req);

    Response resp;
    resp.status_code = 404;
    resp.body = "{\"success\":false,\"error\":\"Not found\"}";
    return resp;
}

} // namespace containercp::api
