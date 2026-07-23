#ifndef CONTAINERCP_ACCESS_ACCESS_KEY_H
#define CONTAINERCP_ACCESS_ACCESS_KEY_H

#include <cstdint>
#include <string>

namespace containercp::access {

struct AccessKey {
    uint64_t id = 0;
    uint64_t access_user_id = 0;
    std::string key_type;       // "ssh-ed25519", "ecdsa-sha2-nistp256", "ssh-rsa", etc.
    std::string key_data;       // base64-encoded key blob (without algorithm prefix)
    std::string key_comment;    // optional user-provided comment
    std::string fingerprint;    // "SHA256:<base64-nopadding>"
    bool enabled = true;
    std::string created_at;
    std::string updated_at;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_ACCESS_KEY_H
