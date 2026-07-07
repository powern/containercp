#ifndef CONTAINERCP_AUTH_AUTH_USER_H
#define CONTAINERCP_AUTH_AUTH_USER_H

#include "core/Resource.h"

#include <string>

namespace containercp::auth {

struct AuthUser : core::Resource {
    std::string username;
    std::string password_hash;
    bool must_change_password = false;
    bool enabled = true;
    std::string role = "admin";
};

} // namespace containercp::auth

#endif // CONTAINERCP_AUTH_AUTH_USER_H
