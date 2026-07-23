#ifndef CONTAINERCP_ACCESS_USERNAME_MAPPER_H
#define CONTAINERCP_ACCESS_USERNAME_MAPPER_H

#include <string>
#include <optional>

namespace containercp::access {

// Validates and normalizes an AccessUser.username into a canonical
// managed Linux username of the form "au-<normalized>".
//
// Rules:
//  - max output length (including prefix) = 32
//  - lowercase only
//  - allowed: a-z, 0-9, -, _
//  - disallowed chars replaced with _
//  - leading/trailing _ stripped
//  - multiple consecutive _ collapsed
//  - must produce non-empty name after normalization
//  - prefix "au-" always prepended
class UsernameMapper {
public:
    static constexpr const char* kPrefix = "au-";
    static constexpr std::size_t kMaxLength = 32;

    struct Result {
        bool        valid = false;
        std::string canonical;      // "au-<normalized>"
        std::string error;          // empty if valid
    };

    static Result map(const std::string& username);
};

} // namespace containercp::access

#endif
