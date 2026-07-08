#ifndef CONTAINERCP_SSL_LETS_ENCRYPT_PROVIDER_H
#define CONTAINERCP_SSL_LETS_ENCRYPT_PROVIDER_H

#include "ssl/CertificateProvider.h"
#include "ssl/ChallengeProvider.h"
#include "ssl/AcmeClient.h"
#include "ssl/CertificateStore.h"
#include "logger/Logger.h"

#include <string>

namespace containercp::ssl {

// LetsEncryptProvider is an adapter around AcmeClient.
//
// It orchestrates the full ACME lifecycle:
//   preflight → account → order → authz → challenge → finalize → download → store
//
// ChallengeProvider handles the transport-specific challenge serving (HTTP-01).
// CertificateStore handles all disk I/O (atomic writes, metadata).
// AcmeClient handles the ACME protocol (JWT, HTTP calls).

class LetsEncryptProvider : public CertificateProvider {
public:
    LetsEncryptProvider(logger::Logger& logger,
                        ChallengeProvider& challenge,
                        CertificateStore& store);

    core::OperationResult request(const std::string& domain) override;
    core::OperationResult renew(const std::string& domain) override;
    core::OperationResult revoke(const std::string& domain) override;
    core::OperationResult status(const std::string& domain) override;

    void set_staging(bool staging);

    std::string provider_id() const override;
    std::string provider_name() const override;
    bool supports_auto_renew() const override;

    std::string certificate_path(const std::string& domain) const override;
    std::string key_path(const std::string& domain) const override;
    std::string chain_path(const std::string& domain) const override;

private:
    core::OperationResult preflight_validation(const std::string& domain);
    core::OperationResult issue_certificate(uint64_t site_id,
                                             const std::string& domain,
                                             const std::vector<std::string>& domains);

    logger::Logger& logger_;
    ChallengeProvider& challenge_;
    CertificateStore& store_;
    AcmeClient acme_;
    std::string ssl_dir_;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_LETS_ENCRYPT_PROVIDER_H
