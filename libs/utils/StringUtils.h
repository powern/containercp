#ifndef CONTAINERCP_UTILS_STRING_UTILS_H
#define CONTAINERCP_UTILS_STRING_UTILS_H

#include <string>

namespace containercp::utils {

struct StringUtils {
    static std::string sanitize(const std::string& input);
    static std::string trim(const std::string& input);
    static std::string bool_to_string(bool value);
    static bool string_to_bool(const std::string& value);
};

} // namespace containercp::utils

#endif // CONTAINERCP_UTILS_STRING_UTILS_H
