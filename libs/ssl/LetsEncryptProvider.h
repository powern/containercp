#ifndef CONTAINERCP_SSL_LETS_ENCRYPT_PROVIDER_H
#define CONTAINERCP_SSL_LETS_ENCRYPT_PROVIDER_H

#include "ssl/CertificateProvider.h"
#include "logger/Logger.h"

#include <string>

namespace containercp::ssl {

class LetsEncryptProvider : public CertificateProvider {
public:
    explicit LetsEncryptProvider(logger::Logger& logger);

    core::OperationResult request(const std::string& domain) override;
    core::OperationResult renew(const std::string& domain) override;
    core::OperationResult revoke(const std::string& domain) override;
    core::OperationResult status(const std::string& domain) override;

private:
    logger::Logger& logger_;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_LETS_ENCRYPT_PROVIDER_H
