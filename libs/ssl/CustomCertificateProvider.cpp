#include "CustomCertificateProvider.h"

namespace containercp::ssl {

CustomCertificateProvider::CustomCertificateProvider(logger::Logger& logger)
    : logger_(logger)
    , ssl_dir_("/srv/containercp/ssl")
{
}

core::OperationResult CustomCertificateProvider::request(const std::string& domain) {
    logger_.info("CustomCertificate", "Placeholder: would import certificate for " + domain);
    return {true, ""};
}

core::OperationResult CustomCertificateProvider::renew(const std::string& domain) {
    logger_.info("CustomCertificate", "Custom certificates cannot be auto-renewed for " + domain);
    (void)domain;
    return {false, "Custom certificates cannot be renewed automatically. Upload a new certificate."};
}

core::OperationResult CustomCertificateProvider::revoke(const std::string& domain) {
    logger_.info("CustomCertificate", "Placeholder: would revoke certificate for " + domain);
    return {true, ""};
}

core::OperationResult CustomCertificateProvider::status(const std::string& domain) {
    logger_.info("CustomCertificate", "Placeholder: would check status for " + domain);
    (void)domain;
    return {true, ""};
}

std::string CustomCertificateProvider::provider_name() const {
    return "custom";
}

std::string CustomCertificateProvider::certificate_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/fullchain.pem";
}

std::string CustomCertificateProvider::key_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/privkey.pem";
}

std::string CustomCertificateProvider::chain_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/chain.pem";
}

} // namespace containercp::ssl
