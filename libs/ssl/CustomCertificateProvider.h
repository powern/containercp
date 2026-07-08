#ifndef CONTAINERCP_SSL_CUSTOM_CERTIFICATE_PROVIDER_H
#define CONTAINERCP_SSL_CUSTOM_CERTIFICATE_PROVIDER_H

#include "ssl/CertificateProvider.h"
#include "logger/Logger.h"

#include <string>

namespace containercp::ssl {

class CustomCertificateProvider : public CertificateProvider {
public:
    explicit CustomCertificateProvider(logger::Logger& logger);

    core::OperationResult request(const std::string& domain) override;
    core::OperationResult renew(const std::string& domain) override;
    core::OperationResult revoke(const std::string& domain) override;
    core::OperationResult status(const std::string& domain) override;

    std::string provider_name() const override;

    std::string certificate_path(const std::string& domain) const override;
    std::string key_path(const std::string& domain) const override;
    std::string chain_path(const std::string& domain) const override;

private:
    logger::Logger& logger_;
    std::string ssl_dir_;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_CUSTOM_CERTIFICATE_PROVIDER_H
