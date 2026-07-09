#ifndef CONTAINERCP_SSL_HTTP01_CHALLENGE_PROVIDER_H
#define CONTAINERCP_SSL_HTTP01_CHALLENGE_PROVIDER_H

#include "ssl/ChallengeProvider.h"
#include "logger/Logger.h"

#include <string>

namespace containercp::ssl {

class HTTP01ChallengeProvider : public ChallengeProvider {
public:
    explicit HTTP01ChallengeProvider(logger::Logger& logger,
                                      const std::string& sites_root,
                                      const std::string& admin_challenge_root = "");

    void set_admin_hostname(const std::string& hostname);

    std::string type() const override;

    core::OperationResult prepare(const std::string& domain,
                                   const std::string& token,
                                   const std::string& key_authorization) override;

    core::OperationResult cleanup(const std::string& domain,
                                   const std::string& token) override;

    core::OperationResult verify(const std::string& domain) override;

    core::OperationResult can_validate(const std::string& domain) override;

    std::string challenge_dir(const std::string& domain) const;
    std::string admin_hostname() const { return admin_hostname_; }

private:
    logger::Logger& logger_;
    std::string sites_root_;
    std::string admin_challenge_root_;
    std::string admin_hostname_;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_HTTP01_CHALLENGE_PROVIDER_H
