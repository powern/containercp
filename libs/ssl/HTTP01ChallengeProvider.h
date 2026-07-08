#ifndef CONTAINERCP_SSL_HTTP01_CHALLENGE_PROVIDER_H
#define CONTAINERCP_SSL_HTTP01_CHALLENGE_PROVIDER_H

#include "ssl/ChallengeProvider.h"
#include "logger/Logger.h"

#include <string>

namespace containercp::ssl {

class HTTP01ChallengeProvider : public ChallengeProvider {
public:
    explicit HTTP01ChallengeProvider(logger::Logger& logger);

    std::string type() const override;

    core::OperationResult prepare(const std::string& domain,
                                   const std::string& token,
                                   const std::string& key_authorization) override;

    core::OperationResult cleanup(const std::string& domain,
                                   const std::string& token) override;

    core::OperationResult verify(const std::string& domain) override;

    core::OperationResult can_validate(const std::string& domain) override;

private:
    logger::Logger& logger_;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_HTTP01_CHALLENGE_PROVIDER_H
