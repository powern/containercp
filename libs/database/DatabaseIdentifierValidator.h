#ifndef CONTAINERCP_DATABASE_DATABASE_IDENTIFIER_VALIDATOR_H
#define CONTAINERCP_DATABASE_DATABASE_IDENTIFIER_VALIDATOR_H

#include <string>

namespace containercp::database {

struct DatabaseIdentifierValidationResult {
    bool valid = false;
    std::string code;
    std::string message;
};

class DatabaseIdentifierValidator {
public:
    static constexpr std::size_t kMaxDatabaseNameLength = 64;
    static constexpr std::size_t kMaxUserNameLength = 32;

    static DatabaseIdentifierValidationResult validate_database_name(const std::string& value);
    static DatabaseIdentifierValidationResult validate_user_name(const std::string& value);
    static std::string quote_identifier(const std::string& value);
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_IDENTIFIER_VALIDATOR_H
