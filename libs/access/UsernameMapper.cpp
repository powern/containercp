#include "access/UsernameMapper.h"

#include <algorithm>
#include <cctype>

namespace containercp::access {

UsernameMapper::Result UsernameMapper::map(const std::string& username) {
    Result result;

    if (username.empty()) {
        result.error = "empty username";
        return result;
    }

    // Build normalized: lowercase, replace disallowed chars with _
    std::string norm;
    norm.reserve(username.size());
    for (unsigned char c : username) {
        char ch = static_cast<char>(std::tolower(c));
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
            norm.push_back(ch);
        } else {
            norm.push_back('_');
        }
    }

    // Collapse consecutive _, trim leading/trailing _
    std::string clean;
    clean.reserve(norm.size());
    for (std::size_t i = 0; i < norm.size(); ++i) {
        if (norm[i] == '_') {
            if (!clean.empty() && clean.back() != '_') clean.push_back('_');
        } else {
            clean.push_back(norm[i]);
        }
    }
    while (!clean.empty() && clean.back() == '_') clean.pop_back();
    while (!clean.empty() && clean.front() == '_') clean.erase(clean.begin());

    if (clean.empty()) {
        result.error = "username normalizes to empty";
        return result;
    }

    std::string canonical = std::string(kPrefix) + clean;
    if (canonical.size() > kMaxLength) {
        result.error = "canonical username too long (max " + std::to_string(kMaxLength) + ")";
        return result;
    }

    result.valid = true;
    result.canonical = canonical;
    return result;
}

} // namespace containercp::access
