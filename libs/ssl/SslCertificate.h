#ifndef CONTAINERCP_SSL_SSL_CERTIFICATE_H
#define CONTAINERCP_SSL_SSL_CERTIFICATE_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::ssl {

struct SslCertificate : core::Resource {
    uint64_t domain_id = 0;
    std::string domain;
    std::string provider = "placeholder";
    std::string certificate_path;
    std::string key_path;
    std::string expires_at = "unknown";
    std::string status = "placeholder";
    bool auto_renew = true;
    bool enabled = true;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_SSL_CERTIFICATE_H
