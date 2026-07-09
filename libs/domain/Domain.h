#ifndef CONTAINERCP_DOMAIN_DOMAIN_H
#define CONTAINERCP_DOMAIN_DOMAIN_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::domain {

struct Domain : core::Resource {
    std::string fqdn;
    uint64_t owner_id = 0;
    uint64_t site_id = 0;
    std::string php_version = "8.4";
    bool ssl_enabled = false;
    bool enabled = true;
    std::string type = "primary";      // primary, alias, redirect, wildcard
    std::string target;                // site domain for primary/alias, URL for redirect
};

} // namespace containercp::domain

#endif // CONTAINERCP_DOMAIN_DOMAIN_H
