#ifndef CONTAINERCP_DATABASE_DATABASE_H
#define CONTAINERCP_DATABASE_DATABASE_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::database {

struct Database : core::Resource {
    std::string db_name;
    std::string db_user;
    std::string db_password;
    std::string engine = "mariadb";
    std::string version = "lts";
    uint64_t owner_id = 0;
    uint64_t site_id = 0;
    bool enabled = true;
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_H
