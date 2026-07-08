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
    c.status = "http_only";
    c.auto_renew = true;
    c.https_enabled = false;
    c.redirect_enabled = false;
    c.version = 1;
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

std::vector<SslCertificate*> SslCertificateManager::find_due_for_renewal() {
    std::vector<SslCertificate*> result;
    for (auto& c : certs_) {
        if (c.status == "active" && c.auto_renew && !c.renew_after.empty()) {
            result.push_back(&c);
        }
    }
    return result;
}

std::vector<SslCertificate*> SslCertificateManager::find_by_status(const std::string& status) {
    std::vector<SslCertificate*> result;
    for (auto& c : certs_) {
        if (c.status == status) {
            result.push_back(&c);
        }
    }
    return result;
}

const std::vector<SslCertificate>& SslCertificateManager::list() const {
    return certs_;
}

void SslCertificateManager::update_status(uint64_t id, const std::string& status) {
    for (auto& c : certs_) {
        if (c.id == id) {
            c.status = status;
            return;
        }
    }
}

void SslCertificateManager::update_https(uint64_t id, bool https_enabled, bool redirect_enabled) {
    for (auto& c : certs_) {
        if (c.id == id) {
            c.https_enabled = https_enabled;
            c.redirect_enabled = redirect_enabled;
            return;
        }
    }
}

void SslCertificateManager::set_metadata(uint64_t id, const std::string& issued_at,
                                          const std::string& expires_at,
                                          const std::string& renew_after) {
    for (auto& c : certs_) {
        if (c.id == id) {
            c.issued_at = issued_at;
            c.expires_at = expires_at;
            c.renew_after = renew_after;
            return;
        }
    }
}

void SslCertificateManager::set_error(uint64_t id, const std::string& error_msg) {
    for (auto& c : certs_) {
        if (c.id == id) {
            c.last_error = error_msg;
            c.status = "error";
            return;
        }
    }
}

void SslCertificateManager::set_domains(uint64_t id, const std::string& domains_str) {
    for (auto& c : certs_) {
        if (c.id == id) {
            c.domains = domains_str;
            return;
        }
    }
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
