#ifndef CONTAINERCP_MAIL_SITE_MAIL_CREDENTIALS_H
#define CONTAINERCP_MAIL_SITE_MAIL_CREDENTIALS_H

#include "core/OperationResult.h"

#include <cstdint>
#include <optional>
#include <string>

namespace containercp::mail {

class SiteMailCredentials {
public:
    struct Credential {
        std::string username;
        std::string password;
        std::string password_hash;
        std::string domain;
    };

    virtual ~SiteMailCredentials() = default;

    virtual Credential generate(uint64_t site_id, const std::string& domain) = 0;
    virtual bool remove(uint64_t site_id) = 0;
    virtual std::optional<Credential> find(uint64_t site_id) = 0;
    virtual core::OperationResult apply(const Credential& cred) = 0;
    virtual core::OperationResult revoke(const Credential& cred) = 0;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_SITE_MAIL_CREDENTIALS_H
