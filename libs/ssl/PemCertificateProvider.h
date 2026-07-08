#ifndef CONTAINERCP_SSL_PEM_CERTIFICATE_PROVIDER_H
#define CONTAINERCP_SSL_PEM_CERTIFICATE_PROVIDER_H

#include "ssl/CertificateProvider.h"
#include "logger/Logger.h"

#include <string>

namespace containercp::ssl {

class PemCertificateProvider : public CertificateProvider {
public:
    explicit PemCertificateProvider(logger::Logger& logger);

    core::OperationResult request(const std::string& domain) override;
    core::OperationResult renew(const std::string& domain) override;
    core::OperationResult revoke(const std::string& domain) override;
    core::OperationResult status(const std::string& domain) override;

    std::string provider_id() const override;
    std::string provider_name() const override;
    bool supports_auto_renew() const override;

    std::string certificate_path(const std::string& domain) const override;
    std::string key_path(const std::string& domain) const override;
    std::string chain_path(const std::string& domain) const override;

private:
    logger::Logger& logger_;
    std::string ssl_dir_;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_PEM_CERTIFICATE_PROVIDER_H
