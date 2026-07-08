#include "LetsEncryptProvider.h"

namespace containercp::ssl {

LetsEncryptProvider::LetsEncryptProvider(logger::Logger& logger, ChallengeProvider& challenge)
    : logger_(logger)
    , challenge_(challenge)
    , ssl_dir_("/srv/containercp/ssl")
{
}

core::OperationResult LetsEncryptProvider::request(const std::string& domain) {
    logger_.info("LetsEncrypt", "Placeholder: would request certificate for " + domain);
    (void)challenge_;
    return {true, ""};
}

core::OperationResult LetsEncryptProvider::renew(const std::string& domain) {
    logger_.info("LetsEncrypt", "Placeholder: would renew certificate for " + domain);
    return {true, ""};
}

core::OperationResult LetsEncryptProvider::revoke(const std::string& domain) {
    logger_.info("LetsEncrypt", "Placeholder: would revoke certificate for " + domain);
    return {true, ""};
}

core::OperationResult LetsEncryptProvider::status(const std::string& domain) {
    logger_.info("LetsEncrypt", "Placeholder: would check status for " + domain);
    (void)domain;
    return {true, ""};
}

std::string LetsEncryptProvider::provider_id() const {
    return "letsencrypt";
}

std::string LetsEncryptProvider::provider_name() const {
    return "Let's Encrypt";
}

bool LetsEncryptProvider::supports_auto_renew() const {
    return true;
}

std::string LetsEncryptProvider::certificate_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/fullchain.pem";
}

std::string LetsEncryptProvider::key_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/privkey.pem";
}

std::string LetsEncryptProvider::chain_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/chain.pem";
}

} // namespace containercp::ssl
