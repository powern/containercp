#include "database/DatabaseIdentifierValidator.h"

namespace containercp::database {
namespace {

DatabaseIdentifierValidationResult ok() {
    return {true, "valid", "Identifier is valid"};
}

DatabaseIdentifierValidationResult fail(std::string code, std::string message) {
    return {false, std::move(code), std::move(message)};
}

bool allowed_identifier_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

DatabaseIdentifierValidationResult validate_identifier(const std::string& value,
                                                       std::size_t max_length,
                                                       const std::string& kind) {
    if (value.empty()) {
        return fail(kind + "_empty", kind + " is required");
    }
    if (value.size() > max_length) {
        return fail(kind + "_too_long", kind + " is too long");
    }
    const unsigned char first = static_cast<unsigned char>(value.front());
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z'))) {
        return fail(kind + "_invalid_start", kind + " must start with an ASCII letter");
    }
    for (unsigned char c : value) {
        if (!allowed_identifier_char(c)) {
            return fail(kind + "_invalid_character", kind + " contains unsupported characters");
        }
    }
    return ok();
}

} // namespace

DatabaseIdentifierValidationResult DatabaseIdentifierValidator::validate_database_name(const std::string& value) {
    return validate_identifier(value, kMaxDatabaseNameLength, "database_name");
}

DatabaseIdentifierValidationResult DatabaseIdentifierValidator::validate_user_name(const std::string& value) {
    return validate_identifier(value, kMaxUserNameLength, "database_user");
}

std::string DatabaseIdentifierValidator::quote_identifier(const std::string& value) {
    const auto validation = validate_database_name(value);
    if (!validation.valid) {
        return {};
    }
    return "`" + value + "`";
}

} // namespace containercp::database
