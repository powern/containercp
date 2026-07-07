#include "AuthMiddleware.h"

namespace containercp::api {

bool AllowAllAuth::authenticate(const Request& /*req*/, Response& /*resp*/) {
    return true;
}

} // namespace containercp::api
