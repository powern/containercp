#include "PasswordGenerator.h"

#include <random>

namespace containercp::utils {

std::string PasswordGenerator::generate(int length) {
    static constexpr char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789";

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    std::string password;
    password.reserve(length);
    for (int i = 0; i < length; ++i) {
        password += chars[dist(gen)];
    }
    return password;
}

} // namespace containercp::utils
