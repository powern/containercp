#ifndef CONTAINERCP_ACCESS_ACCESS_USER_H
#define CONTAINERCP_ACCESS_ACCESS_USER_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::access {

struct AccessUser : core::Resource {
    std::string username;
    std::string auth_type = "password";
    std::string password_hash;
    bool enabled = true;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_ACCESS_USER_H
