#ifndef CONTAINERCP_PROXY_REVERSE_PROXY_H
#define CONTAINERCP_PROXY_REVERSE_PROXY_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::proxy {

struct ReverseProxy : core::Resource {
    std::string domain;
    uint64_t site_id = 0;
    std::string provider = "nginx";
    std::string config_path;
    std::string upstream;
    bool enabled = true;
    std::string status = "active";
};

} // namespace containercp::proxy

#endif // CONTAINERCP_PROXY_REVERSE_PROXY_H
