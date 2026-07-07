#include "StringUtils.h"

#include <algorithm>
#include <cctype>

namespace containercp::utils {

std::string StringUtils::sanitize(const std::string& input) {
    std::string result = input;
    for (auto& c : result) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    return result;
}

std::string StringUtils::trim(const std::string& input) {
    auto start = input.begin();
    while (start != input.end() && std::isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    auto end = input.end();
    while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(start, end);
}

std::string StringUtils::bool_to_string(bool value) {
    return value ? "1" : "0";
}

bool StringUtils::string_to_bool(const std::string& value) {
    return value == "1" || value == "true" || value == "yes";
}

} // namespace containercp::utils
