#ifndef CONTAINERCP_SSL_SSL_CERTIFICATE_MANAGER_H
#define CONTAINERCP_SSL_SSL_CERTIFICATE_MANAGER_H

#include "ssl/SslCertificate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::ssl {

class SslCertificateManager {
public:
    uint64_t create(uint64_t domain_id, const std::string& domain,
                    const std::string& cert_path, const std::string& key_path);
    bool remove(uint64_t id);
    SslCertificate* find(uint64_t id);
    SslCertificate* find_by_domain(const std::string& domain);
    std::vector<SslCertificate*> find_expiring();
    const std::vector<SslCertificate>& list() const;

    void set_certificates(const std::vector<SslCertificate>& certs);

private:
    std::vector<SslCertificate> certs_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_SSL_CERTIFICATE_MANAGER_H
