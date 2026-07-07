#ifndef CONTAINERCP_UTILS_VALIDATOR_H
#define CONTAINERCP_UTILS_VALIDATOR_H

#include <string>

namespace containercp::utils {

struct Validator {
    static bool is_valid_hostname(const std::string& hostname);
    static std::string normalize_hostname(const std::string& hostname);

    static bool is_valid_username(const std::string& username);
};

} // namespace containercp::utils

#endif // CONTAINERCP_UTILS_VALIDATOR_H
