#ifndef CONTAINERCP_ACCESS_SSH_KEY_VALIDATOR_H
#define CONTAINERCP_ACCESS_SSH_KEY_VALIDATOR_H

#include "access/AccessKey.h"

#include <string>
#include <optional>

namespace containercp::access {

struct SshKeyValidation {
    bool valid = false;
    std::string error;          // empty if valid
    std::string key_type;       // canonical algorithm name
    std::string key_data;       // base64 blob (no algorithm prefix)
    std::string key_comment;    // optional
    std::string fingerprint;    // "SHA256:<base64-nopadding>"
};

class SshKeyValidator {
public:
    // Parse and validate an OpenSSH-format public key line.
    // Input format: "<algorithm> <base64-blob> [comment]"
    // Max input length: 8192 bytes.
    // Returns SshKeyValidation with valid=false + error on failure.
    static SshKeyValidation validate(const std::string& line);

    // Compute SHA256 fingerprint from a decoded SSH key blob (binary).
    // Output format: "SHA256:<base64-without-padding>"
    static std::string fingerprint(const std::string& decoded_blob);

    // Decode a base64 string to binary. Empty string on invalid input.
    static std::string base64_decode(const std::string& encoded);
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_SSH_KEY_VALIDATOR_H
