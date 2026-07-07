#ifndef CONTAINERCP_API_AUTH_MIDDLEWARE_H
#define CONTAINERCP_API_AUTH_MIDDLEWARE_H

#include "api/Request.h"
#include "api/Response.h"

namespace containercp::api {

class AuthMiddleware {
public:
    virtual ~AuthMiddleware() = default;

    virtual bool authenticate(const Request& req, Response& resp) = 0;
};

class AllowAllAuth : public AuthMiddleware {
public:
    bool authenticate(const Request& req, Response& resp) override;
};

} // namespace containercp::api

#endif // CONTAINERCP_API_AUTH_MIDDLEWARE_H
