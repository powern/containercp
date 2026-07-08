#ifndef CONTAINERCP_SSL_CERTIFICATE_PROVIDER_H
#define CONTAINERCP_SSL_CERTIFICATE_PROVIDER_H

#include "core/OperationResult.h"

#include <string>

namespace containercp::ssl {

inline constexpr const char* ACME_CHALLENGE_PATH = "/.well-known/acme-challenge/";

class CertificateProvider {
public:
    virtual ~CertificateProvider() = default;

    virtual core::OperationResult request(const std::string& domain) = 0;
    virtual core::OperationResult renew(const std::string& domain) = 0;
    virtual core::OperationResult revoke(const std::string& domain) = 0;
    virtual core::OperationResult status(const std::string& domain) = 0;

    virtual std::string provider_id() const = 0;
    virtual std::string provider_name() const = 0;
    virtual bool supports_auto_renew() const = 0;
    virtual bool supports_dns_challenge() const { return false; }
    virtual core::OperationResult request_dns(const std::string& domain) {
        (void)domain;
        return {false, "DNS challenge not supported by this provider"};
    }

    virtual std::string certificate_path(const std::string& domain) const = 0;
    virtual std::string key_path(const std::string& domain) const = 0;
    virtual std::string chain_path(const std::string& domain) const = 0;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_CERTIFICATE_PROVIDER_H
