#ifndef CONTAINERCP_SSL_CHALLENGE_PROVIDER_H
#define CONTAINERCP_SSL_CHALLENGE_PROVIDER_H

#include "core/OperationResult.h"

#include <string>

namespace containercp::ssl {

class ChallengeProvider {
public:
    virtual ~ChallengeProvider() = default;

    virtual std::string type() const = 0;

    virtual core::OperationResult prepare(const std::string& domain,
                                           const std::string& token,
                                           const std::string& key_authorization) = 0;

    virtual core::OperationResult cleanup(const std::string& domain,
                                           const std::string& token) = 0;

    virtual core::OperationResult verify(const std::string& domain) = 0;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_CHALLENGE_PROVIDER_H
