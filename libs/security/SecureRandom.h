#ifndef CONTAINERCP_SECURITY_SECURE_RANDOM_H
#define CONTAINERCP_SECURITY_SECURE_RANDOM_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace containercp::security {

class SecureRandom {
public:
    static std::optional<std::vector<unsigned char>> bytes(std::size_t count);
    static std::optional<std::string> hex(std::size_t byte_count);
    static std::optional<std::string> string(std::size_t length, const std::string& alphabet);
};

} // namespace containercp::security

#endif // CONTAINERCP_SECURITY_SECURE_RANDOM_H
