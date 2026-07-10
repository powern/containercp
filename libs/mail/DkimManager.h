#ifndef CONTAINERCP_MAIL_DKIM_MANAGER_H
#define CONTAINERCP_MAIL_DKIM_MANAGER_H

#include <string>

namespace containercp::mail {

// DKIM key management for mail domains.
//
// Handles key generation and DNS record preparation.
// Independent from any specific mail provider or runtime.
// A future MailProvider consumes the generated keys rather
// than owning the creation process.
class DkimManager {
public:
    // Generate a 2048-bit RSA DKIM key pair for a domain.
    //   dkim_dir   — base directory for DKIM key storage
    //   domain     — the domain name (e.g. example.com)
    //   selector   — DNS selector (e.g. "dkim")
    //
    // Returns the DKIM DNS TXT record value on success.
    // Returns empty string on failure.
    static std::string generate_key(const std::string& dkim_dir,
                                     const std::string& domain,
                                     const std::string& selector);
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_DKIM_MANAGER_H
