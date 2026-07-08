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
    std::vector<SslCertificate*> find_due_for_renewal();
    std::vector<SslCertificate*> find_by_status(const std::string& status);
    const std::vector<SslCertificate>& list() const;

    void update_status(uint64_t id, const std::string& status);
    void update_https(uint64_t id, bool https_enabled, bool redirect_enabled);
    void set_metadata(uint64_t id, const std::string& issued_at,
                      const std::string& expires_at, const std::string& renew_after);
    void set_error(uint64_t id, const std::string& error_msg);
    void set_domains(uint64_t id, const std::string& domains_str);

    void set_certificates(const std::vector<SslCertificate>& certs);

private:
    std::vector<SslCertificate> certs_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_SSL_CERTIFICATE_MANAGER_H
