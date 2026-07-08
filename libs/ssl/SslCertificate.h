#ifndef CONTAINERCP_SSL_SSL_CERTIFICATE_H
#define CONTAINERCP_SSL_SSL_CERTIFICATE_H

#include "core/Resource.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::ssl {

struct SslCertificate : core::Resource {
    uint64_t domain_id = 0;
    std::string domain;
    std::string provider = "placeholder";
    std::string certificate_path;
    std::string key_path;
    std::string chain_path;
    std::string issued_at;
    std::string expires_at;
    std::string renew_after;
    std::string status = "http_only";
    bool auto_renew = true;
    bool https_enabled = false;
    bool redirect_enabled = false;

    std::string domains;
    std::string challenge_type;
    std::string last_error;
    std::string last_validation;
    int renew_attempts = 0;
    int version = 1;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_SSL_CERTIFICATE_H
