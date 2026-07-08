#include "HTTP01ChallengeProvider.h"

namespace containercp::ssl {

HTTP01ChallengeProvider::HTTP01ChallengeProvider(logger::Logger& logger)
    : logger_(logger)
{
}

std::string HTTP01ChallengeProvider::type() const {
    return "http-01";
}

core::OperationResult HTTP01ChallengeProvider::prepare(
    const std::string& domain,
    const std::string& token,
    const std::string& key_authorization)
{
    logger_.info("HTTP-01", "Placeholder: would write challenge token for " + domain);
    (void)token;
    (void)key_authorization;
    return {true, ""};
}

core::OperationResult HTTP01ChallengeProvider::cleanup(
    const std::string& domain,
    const std::string& token)
{
    logger_.info("HTTP-01", "Placeholder: would clean up challenge for " + domain);
    (void)token;
    return {true, ""};
}

core::OperationResult HTTP01ChallengeProvider::verify(
    const std::string& domain)
{
    logger_.info("HTTP-01", "Placeholder: would verify challenge for " + domain);
    (void)domain;
    return {true, ""};
}

} // namespace containercp::ssl
