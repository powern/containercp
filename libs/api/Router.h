#ifndef CONTAINERCP_API_ROUTER_H
#define CONTAINERCP_API_ROUTER_H

#include "api/Request.h"
#include "api/Response.h"

#include <functional>
#include <string>
#include <vector>

namespace containercp::api {

using RouteHandler = std::function<Response(const Request&)>;

class Router {
public:
    void add(const std::string& method, const std::string& path, RouteHandler handler);
    void add_prefix(const std::string& method, const std::string& prefix, RouteHandler handler);
    Response dispatch(const Request& req) const;

private:
    struct Route {
        std::string method;
        std::string path;
        RouteHandler handler;
        bool is_prefix = false;
    };
    std::vector<Route> routes_;
};

} // namespace containercp::api

#endif // CONTAINERCP_API_ROUTER_H
