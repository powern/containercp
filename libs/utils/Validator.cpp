#include "Validator.h"

#include <cctype>
#include <string>

namespace containercp::utils {

bool Validator::is_valid_hostname(const std::string& hostname) {
    if (hostname.empty()) return false;
    if (hostname.length() > 253) return false;

    std::size_t start = 0;
    std::size_t dot_count = 0;

    while (start < hostname.length()) {
        auto end = hostname.find('.', start);
        if (end == std::string::npos) {
            end = hostname.length();
        } else {
            ++dot_count;
        }

        std::size_t label_len = end - start;
        if (label_len == 0 || label_len > 63) return false;

        if (hostname[start] == '-' || hostname[end - 1] == '-') return false;

        for (std::size_t i = start; i < end; ++i) {
            char c = hostname[i];
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') {
                return false;
            }
        }

        start = end + 1;
    }

    return dot_count >= 1;
}

std::string Validator::normalize_hostname(const std::string& hostname) {
    std::string result = hostname;
    for (auto& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

bool Validator::is_valid_username(const std::string& username) {
    if (username.empty()) return false;
    if (username.length() < 3 || username.length() > 32) return false;

    if (std::isdigit(static_cast<unsigned char>(username[0]))) return false;
    if (username[0] == '-') return false;

    if (username.back() == '-' || username.back() == '_') return false;

    for (auto c : username) {
        if (std::isupper(static_cast<unsigned char>(c))) return false;
        if (!std::islower(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
            return false;
        }
    }

    return true;
}

} // namespace containercp::utils
