#include "MailPasswordHasher.h"

#include <cstring>
#include <random>
#include <string>

#include <crypt.h>

namespace containercp::mail {

static const char SALT_CHARS[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";

static std::string generate_salt(size_t length = 16) {
    std::string salt = "$6$";  // SHA-512-CRYPT prefix
    std::random_device rd;
    // Append random characters for the salt body
    for (size_t i = 0; i < length; ++i) {
        salt += SALT_CHARS[rd() % (sizeof(SALT_CHARS) - 1)];
    }
    salt += '$';
    return salt;
}

std::string MailPasswordHasher::hash(const std::string& password) {
    std::string salt = generate_salt();
    struct crypt_data data;
    memset(&data, 0, sizeof(data));
    char* result = ::crypt_r(password.c_str(), salt.c_str(), &data);
    if (result == nullptr || result[0] == '*') {
        return "";
    }
    return std::string(result);
}

bool MailPasswordHasher::verify(const std::string& password,
                                 const std::string& hash) {
    if (hash.empty() || hash.size() < 3) return false;
    // Extract the salt from the stored hash (everything up to the last $)
    size_t last_dollar = hash.rfind('$');
    if (last_dollar == std::string::npos || last_dollar < 3) return false;
    std::string salt = hash.substr(0, last_dollar + 1);

    struct crypt_data data;
    memset(&data, 0, sizeof(data));
    char* result = ::crypt_r(password.c_str(), salt.c_str(), &data);
    if (result == nullptr) return false;
    return hash == result;
}

} // namespace containercp::mail
