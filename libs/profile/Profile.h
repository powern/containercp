#ifndef CONTAINERCP_PROFILE_PROFILE_H
#define CONTAINERCP_PROFILE_PROFILE_H

#include "core/Resource.h"
#include "profile/ProfileType.h"

#include <string>

namespace containercp::profile {

struct Profile : core::Resource {
    std::string profile_name;
    ProfileType type = ProfileType::WEB_SERVER;
    std::string web_server;
    std::string runtime = "docker";
    std::string template_path;
    std::string description;
    bool enabled = true;
    bool default_profile = false;
};

} // namespace containercp::profile

#endif // CONTAINERCP_PROFILE_PROFILE_H
