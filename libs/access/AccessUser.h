#ifndef CONTAINERCP_ACCESS_ACCESS_USER_H
#define CONTAINERCP_ACCESS_ACCESS_USER_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::access {

struct AccessUser : core::Resource {
    std::string username;
    uint64_t site_id = 0;
    std::string domain;
    std::string auth_type = "password";
    bool enabled = true;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_ACCESS_USER_H
