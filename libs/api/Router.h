#ifndef CONTAINERCP_API_ROUTER_H
#define CONTAINERCP_API_ROUTER_H

#include "api/Request.h"
#include "api/Response.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace containercp::api {

using RouteHandler = std::function<Response(const Request&)>;

class Router {
public:
    void add(const std::string& method, const std::string& path, RouteHandler handler);
    Response dispatch(const Request& req) const;

private:
    struct Route {
        std::string method;
        std::string path;
        RouteHandler handler;
    };
    std::vector<Route> routes_;
};

} // namespace containercp::api

#endif // CONTAINERCP_API_ROUTER_H
