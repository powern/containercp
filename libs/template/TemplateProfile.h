#ifndef CONTAINERCP_TEMPLATE_TEMPLATE_PROFILE_H
#define CONTAINERCP_TEMPLATE_TEMPLATE_PROFILE_H

#include "core/Resource.h"

#include <string>

namespace containercp::template_engine {

struct TemplateProfile : core::Resource {
    std::string profile_name;
    std::string web_server;
    std::string runtime = "docker";
    std::string template_path;
    std::string description;
    bool enabled = true;
    bool default_profile = false;
};

} // namespace containercp::template_engine

#endif // CONTAINERCP_TEMPLATE_TEMPLATE_PROFILE_H
