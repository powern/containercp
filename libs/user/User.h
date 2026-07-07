#ifndef CONTAINERCP_USER_USER_H
#define CONTAINERCP_USER_USER_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::user {

struct User : core::Resource {
    std::string username;
    uint64_t uid = 0;
    std::string home_directory;
    std::string shell;
    bool enabled = true;
};

} // namespace containercp::user

#endif // CONTAINERCP_USER_USER_H
