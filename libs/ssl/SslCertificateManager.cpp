#include "SslCertificateManager.h"

namespace containercp::ssl {

uint64_t SslCertificateManager::create(uint64_t domain_id, const std::string& domain,
                                        const std::string& cert_path, const std::string& key_path) {
    SslCertificate c;
    c.id = next_id_++;
    c.name = domain;
    c.domain_id = domain_id;
    c.domain = domain;
    c.certificate_path = cert_path;
    c.key_path = key_path;
    c.provider = "placeholder";
    c.expires_at = "unknown";
    c.status = "requested";
    c.auto_renew = true;
    c.enabled = true;
    certs_.push_back(std::move(c));
    return c.id;
}

bool SslCertificateManager::remove(uint64_t id) {
    for (auto it = certs_.begin(); it != certs_.end(); ++it) {
        if (it->id == id) {
            certs_.erase(it);
            return true;
        }
    }
    return false;
}

SslCertificate* SslCertificateManager::find(uint64_t id) {
    for (auto& c : certs_) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

SslCertificate* SslCertificateManager::find_by_domain(const std::string& domain) {
    for (auto& c : certs_) {
        if (c.domain == domain) {
            return &c;
        }
    }
    return nullptr;
}

std::vector<SslCertificate*> SslCertificateManager::find_expiring() {
    std::vector<SslCertificate*> result;
    for (auto& c : certs_) {
        if (c.status == "active" && c.auto_renew) {
            result.push_back(&c);
        }
    }
    return result;
}

const std::vector<SslCertificate>& SslCertificateManager::list() const {
    return certs_;
}

void SslCertificateManager::set_certificates(const std::vector<SslCertificate>& certs) {
    certs_ = certs;
    next_id_ = 1;
    for (const auto& c : certs_) {
        if (c.id >= next_id_) {
            next_id_ = c.id + 1;
        }
    }
}

} // namespace containercp::ssl
