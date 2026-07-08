#include "PemCertificateProvider.h"

namespace containercp::ssl {

PemCertificateProvider::PemCertificateProvider(logger::Logger& logger)
    : logger_(logger)
    , ssl_dir_("/srv/containercp/ssl")
{
}

core::OperationResult PemCertificateProvider::request(const std::string& domain) {
    logger_.info("PEM", "Placeholder: would import certificate for " + domain);
    return {true, ""};
}

core::OperationResult PemCertificateProvider::renew(const std::string& domain) {
    logger_.info("PEM", "PEM certificates cannot be auto-renewed for " + domain);
    (void)domain;
    return {false, "PEM certificates cannot be renewed automatically. Upload a new certificate."};
}

core::OperationResult PemCertificateProvider::revoke(const std::string& domain) {
    logger_.info("PEM", "Placeholder: would revoke certificate for " + domain);
    return {true, ""};
}

core::OperationResult PemCertificateProvider::status(const std::string& domain) {
    logger_.info("PEM", "Placeholder: would check status for " + domain);
    (void)domain;
    return {true, ""};
}

std::string PemCertificateProvider::provider_id() const {
    return "pem";
}

std::string PemCertificateProvider::provider_name() const {
    return "PEM Certificate";
}

bool PemCertificateProvider::supports_auto_renew() const {
    return false;
}

std::string PemCertificateProvider::certificate_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/fullchain.pem";
}

std::string PemCertificateProvider::key_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/privkey.pem";
}

std::string PemCertificateProvider::chain_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/chain.pem";
}

} // namespace containercp::ssl
