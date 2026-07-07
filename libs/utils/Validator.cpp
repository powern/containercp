#include "Validator.h"

#include <cctype>
#include <string>

namespace containercp::utils {

bool Validator::is_valid_hostname(const std::string& hostname) {
    return validate_hostname(hostname).empty();
}

bool Validator::is_valid_username(const std::string& username) {
    return validate_username(username).empty();
}

std::string Validator::validate_hostname(const std::string& hostname) {
    if (hostname.empty()) return "Domain is empty";
    if (hostname.length() > 253) return "Domain is longer than 253 characters";

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
        if (label_len == 0) return "Domain contains an empty label";
        if (label_len > 63) return "Label is longer than 63 characters: " + hostname.substr(start, 63) + "...";

        if (hostname[start] == '-') return "Label cannot start with a hyphen: " + hostname.substr(start, end - start);
        if (hostname[end - 1] == '-') return "Label cannot end with a hyphen: " + hostname.substr(start, end - start);

        for (std::size_t i = start; i < end; ++i) {
            char c = hostname[i];
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') {
                return "Label contains invalid character: " + std::string(1, c);
            }
        }

        start = end + 1;
    }

    if (dot_count < 1) return "Domain must contain at least one dot";
    return {};
}

std::string Validator::validate_username(const std::string& username) {
    if (username.empty()) return "Username is empty";
    if (username.length() < 3 || username.length() > 32) return "Username must be between 3 and 32 characters";

    if (std::isdigit(static_cast<unsigned char>(username[0]))) return "Username cannot start with a digit";
    if (username[0] == '-') return "Username cannot start with a hyphen";

    if (username.back() == '-') return "Username cannot end with a hyphen";
    if (username.back() == '_') return "Username cannot end with an underscore";

    for (auto c : username) {
        if (std::isupper(static_cast<unsigned char>(c))) return "Username cannot contain uppercase letters";
        if (!std::islower(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
            return "Username can only contain lowercase letters, digits, hyphens, and underscores";
        }
    }

    return {};
}

std::string Validator::normalize_hostname(const std::string& hostname) {
    std::string result = hostname;
    for (auto& c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

} // namespace containercp::utils
