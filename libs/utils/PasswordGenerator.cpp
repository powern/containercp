#include "PasswordGenerator.h"

#include "security/SecureRandom.h"

namespace containercp::utils {

std::string PasswordGenerator::generate(int length) {
    if (length <= 0) return "";

    static constexpr char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789";

    auto password = security::SecureRandom::string(static_cast<std::size_t>(length), chars);
    return password.value_or("");
}

} // namespace containercp::utils
