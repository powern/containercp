#ifndef CONTAINERCP_SSL_CERTIFICATE_PROVIDER_H
#define CONTAINERCP_SSL_CERTIFICATE_PROVIDER_H

#include "core/OperationResult.h"

#include <string>

namespace containercp::ssl {

class CertificateProvider {
public:
    virtual ~CertificateProvider() = default;

    virtual core::OperationResult request(const std::string& domain) = 0;
    virtual core::OperationResult renew(const std::string& domain) = 0;
    virtual core::OperationResult revoke(const std::string& domain) = 0;
    virtual core::OperationResult status(const std::string& domain) = 0;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_CERTIFICATE_PROVIDER_H
