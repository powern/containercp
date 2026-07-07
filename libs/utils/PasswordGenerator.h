#ifndef CONTAINERCP_UTILS_PASSWORD_GENERATOR_H
#define CONTAINERCP_UTILS_PASSWORD_GENERATOR_H

#include <string>

namespace containercp::utils {

class PasswordGenerator {
public:
    static std::string generate(int length = 32);

private:
    PasswordGenerator() = default;
};

} // namespace containercp::utils

#endif // CONTAINERCP_UTILS_PASSWORD_GENERATOR_H
