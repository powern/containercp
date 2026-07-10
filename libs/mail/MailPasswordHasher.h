#ifndef CONTAINERCP_MAIL_MAIL_PASSWORD_HASHER_H
#define CONTAINERCP_MAIL_MAIL_PASSWORD_HASHER_H

#include <string>

namespace containercp::mail {

// Password hashing for mail module mailboxes.
//
// Uses SHA-512-CRYPT ($6$), compatible with Dovecot's
// default_password_scheme = SHA512-CRYPT.
//
// A future MailProvider can consume these hashes directly
// without re-hashing.  Plaintext passwords are never stored.
class MailPasswordHasher {
public:
    // Hash a plaintext password.  Returns SHA-512-CRYPT hash.
    // Returns empty string on failure.
    static std::string hash(const std::string& password);

    // Verify a plaintext password against an existing hash.
    static bool verify(const std::string& password, const std::string& hash);
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_MAIL_PASSWORD_HASHER_H
