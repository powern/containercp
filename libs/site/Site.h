#ifndef CONTAINERCP_SITE_SITE_H
#define CONTAINERCP_SITE_SITE_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::site {

struct Site : core::Resource {
    std::string domain;
    std::string owner;
    uint64_t node_id = 0;
    std::string db_name;
    std::string db_user;
    std::string db_password;
    std::string web_server = "apache";
    bool php_mail_enabled = false;
};

} // namespace containercp::site

#endif // CONTAINERCP_SITE_SITE_H
