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
    std::string web_template_profile;
    // Transient provisioning choice. Rendered during create only; not persisted separately.
    std::string template_profile;
    bool php_mail_enabled = false;

    // Transient: set by Storage::load_sites() when the 6th field was present.
    // Used by ServiceRegistry for one-time legacy migration.
    // NOT persisted — serialization skips this field.
    bool php_mail_enabled_present = false;
};

} // namespace containercp::site

#endif // CONTAINERCP_SITE_SITE_H
