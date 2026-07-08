#include "Router.h"

namespace containercp::api {

void Router::add(const std::string& method, const std::string& path, RouteHandler handler) {
    routes_.push_back({method, path, std::move(handler), false});
}

void Router::add_prefix(const std::string& method, const std::string& prefix, RouteHandler handler) {
    routes_.push_back({method, prefix, std::move(handler), true});
}

Response Router::dispatch(const Request& req) const {
    for (const auto& route : routes_) {
        if (route.method != req.method) continue;
        if (route.is_prefix) {
            if (req.path.compare(0, route.path.size(), route.path) == 0) {
                return route.handler(req);
            }
        } else {
            if (route.path == req.path) {
                return route.handler(req);
            }
        }
    }

    Response resp;
    resp.status_code = 404;
    resp.body = "{\"success\":false,\"error\":\"Not found\"}";
    return resp;
}

} // namespace containercp::api
