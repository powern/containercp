#include "LetsEncryptProvider.h"

namespace containercp::ssl {

LetsEncryptProvider::LetsEncryptProvider(logger::Logger& logger)
    : logger_(logger)
{
}

core::OperationResult LetsEncryptProvider::request(const std::string& domain) {
    logger_.info("LetsEncryptProvider: Requesting certificate for " + domain);
    return {true, ""};
}

core::OperationResult LetsEncryptProvider::renew(const std::string& domain) {
    logger_.info("LetsEncryptProvider: Renewing certificate for " + domain);
    return {true, ""};
}

core::OperationResult LetsEncryptProvider::revoke(const std::string& domain) {
    logger_.info("LetsEncryptProvider: Revoking certificate for " + domain);
    return {true, ""};
}

core::OperationResult LetsEncryptProvider::status(const std::string& domain) {
    logger_.info("LetsEncryptProvider: Status for " + domain);
    return {true, ""};
}

} // namespace containercp::ssl
