#ifndef CONTAINERCP_ACCESS_SSH_KEY_GENERATOR_H
#define CONTAINERCP_ACCESS_SSH_KEY_GENERATOR_H

#include <string>

namespace containercp::access {

struct GeneratedSshKey {
    bool success = false;
    std::string error;
    std::string public_key;
    std::string private_key;
};

// Generates a key pair without keeping either key on disk after returning.
class SshKeyGenerator {
public:
    GeneratedSshKey generate(const std::string& type, const std::string& comment) const;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_SSH_KEY_GENERATOR_H
