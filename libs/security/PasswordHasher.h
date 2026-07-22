#ifndef CONTAINERCP_SECURITY_PASSWORD_HASHER_H
#define CONTAINERCP_SECURITY_PASSWORD_HASHER_H

#include <string>

namespace containercp::security {

class PasswordHasher {
public:
    static std::string hash(const std::string& password);
    static bool verify(const std::string& password, const std::string& encoded_hash);
    static bool is_supported_hash(const std::string& encoded_hash);
    static const char* backend_name();
};

} // namespace containercp::security

#endif // CONTAINERCP_SECURITY_PASSWORD_HASHER_H
