#include "database/MariaDBProvider.h"

#include "database/DatabaseIdentifierValidator.h"
#include "database/MariaDBSecureTempFile.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace containercp::database {
namespace {

DatabaseProviderResult provider_failure(std::string code, std::string message) {
    return {false, std::move(code), std::move(message), {}};
}

DatabaseProviderResult provider_success(std::string code, std::string message, std::string output = {}) {
    return {true, std::move(code), std::move(message), std::move(output)};
}

bool password_supported(const std::string& value) {
    if (value.empty() || value.size() > 256) {
        return false;
    }
    for (unsigned char c : value) {
        if (c == '\0' || c == 0x7f || (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\b')) {
            return false;
        }
    }
    return true;
}

std::string option_file_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (unsigned char c : value) {
        switch (c) {
        case '\b': escaped += "\\b"; break;
        case '\t': escaped += "\\t"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case ' ': escaped += "\\s"; break;
        case '\\': escaped += "\\\\"; break;
        default: escaped.push_back(static_cast<char>(c)); break;
        }
    }
    return escaped;
}

std::string quote_user(const std::string& user) {
    return mariadb_quote_sql_string(user) + "@" + mariadb_quote_sql_string("%");
}

bool output_count_positive(const std::string& output) {
    for (unsigned char c : output) {
        if (std::isdigit(c) != 0) {
            return c != '0';
        }
        if (!std::isspace(c)) {
            return false;
        }
    }
    return false;
}

} // namespace

MariaDBProvider::MariaDBProvider(const MariaDBProcessRunner& runner)
    : runner_(runner) {
}

std::string mariadb_service_account_option_file(const DatabaseProviderCredential& credential) {
    return "[client]\nuser=" + credential.user +
           "\npassword=" + option_file_escape(credential.password) +
           "\nhost=" + option_file_escape(credential.host) + "\n";
}

std::string mariadb_sanitize_provider_error(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(std::min<std::size_t>(value.size(), 160));
    for (unsigned char c : value) {
        if (sanitized.size() >= 160) {
            break;
        }
        if (std::iscntrl(c) != 0) {
            sanitized.push_back(' ');
        } else {
            sanitized.push_back(static_cast<char>(c));
        }
    }
    if (sanitized.empty()) {
        return "MariaDB operation failed";
    }
    return sanitized;
}

std::vector<std::string> MariaDBProvider::build_exec_args(const MariaDBConnectionTarget& target,
                                                          const std::string& container_option_path,
                                                          const std::string& database_name) const {
    std::vector<std::string> args = {
        "docker", "compose", "-f", target.compose_file,
        "exec", "-T", target.service,
        "mariadb", "--defaults-extra-file=" + container_option_path,
        "--batch", "--raw", "--skip-column-names",
    };
    if (!database_name.empty()) {
        args.push_back("--database");
        args.push_back(database_name);
    }
    return args;
}

DatabaseProviderResult MariaDBProvider::execute_sql(const MariaDBConnectionTarget& target,
                                                    const DatabaseProviderCredential& credential,
                                                    const std::string& sql,
                                                    const std::string& success_code,
                                                    const std::string& database_name) const {
    if (target.compose_file.empty() || target.service.empty()) {
        return provider_failure("target_invalid", "MariaDB target is incomplete");
    }
    if (credential.user.empty() || credential.host.empty() || !password_supported(credential.password)) {
        return provider_failure("credential_transport_invalid", "MariaDB service-account credential is unsupported");
    }

    MariaDBSecureTempFile option_file;
    MariaDBSecureTempFile sql_file;
    try {
        option_file = MariaDBSecureTempFile::create("containercp-mariadb-option", ".cnf", mariadb_service_account_option_file(credential));
        sql_file = MariaDBSecureTempFile::create("containercp-mariadb-query", ".sql", sql);
    } catch (...) {
        return provider_failure("secret_transport_failed", "MariaDB secure credential transport could not be prepared");
    }

    const std::string container_option_path = "/tmp/containercp-" + option_file.path().filename().string();
    auto copy_result = runner_.run({"docker", "compose", "-f", target.compose_file,
                                   "cp", option_file.path().string(), target.service + ":" + container_option_path});
    if (copy_result.exit_code != 0) {
        return provider_failure("mariadb_option_copy_failed", "MariaDB secure credential transport failed");
    }

    auto cleanup = [&]() {
        (void)runner_.run({"docker", "compose", "-f", target.compose_file,
                           "exec", "-T", target.service, "rm", "-f", container_option_path});
    };

    const auto command = runner_.run_with_stdin_file(build_exec_args(target, container_option_path, database_name), sql_file.path().string());
    cleanup();
    if (command.exit_code != 0) {
        return provider_failure("mariadb_command_failed", "MariaDB operation failed: " + mariadb_sanitize_provider_error(command.err));
    }
    return provider_success(success_code, "MariaDB operation completed", command.out);
}

DatabaseProviderResult MariaDBProvider::verify_service_account(const MariaDBConnectionTarget& target,
                                                               const DatabaseProviderCredential& credential) const {
    return execute_sql(target, credential, "SELECT 1;\n", "service_account_verified");
}

DatabaseProviderResult MariaDBProvider::database_exists(const MariaDBConnectionTarget& target,
                                                        const DatabaseProviderCredential& credential,
                                                        const std::string& database_name) const {
    const auto validation = DatabaseIdentifierValidator::validate_database_name(database_name);
    if (!validation.valid) {
        return provider_failure(validation.code, validation.message);
    }
    auto result = execute_sql(target,
                              credential,
                              "SELECT COUNT(*) FROM INFORMATION_SCHEMA.SCHEMATA WHERE SCHEMA_NAME = " + mariadb_quote_sql_string(database_name) + ";\n",
                              "database_exists_checked");
    result.success = result.success && output_count_positive(result.output);
    result.code = result.success ? "database_exists" : "database_missing";
    return result;
}

DatabaseProviderResult MariaDBProvider::user_exists(const MariaDBConnectionTarget& target,
                                                    const DatabaseProviderCredential& credential,
                                                    const std::string& user_name) const {
    const auto validation = DatabaseIdentifierValidator::validate_user_name(user_name);
    if (!validation.valid) {
        return provider_failure(validation.code, validation.message);
    }
    auto result = execute_sql(target,
                              credential,
                              "SELECT COUNT(*) FROM mysql.user WHERE User = " + mariadb_quote_sql_string(user_name) + " AND Host = '%';\n",
                              "user_exists_checked");
    result.success = result.success && output_count_positive(result.output);
    result.code = result.success ? "user_exists" : "user_missing";
    return result;
}

DatabaseProviderResult MariaDBProvider::user_schema_grant_count(const MariaDBConnectionTarget& target,
                                                                const DatabaseProviderCredential& credential,
                                                                const std::string& user_name) const {
    const auto validation = DatabaseIdentifierValidator::validate_user_name(user_name);
    if (!validation.valid) {
        return provider_failure(validation.code, validation.message);
    }
    return execute_sql(target,
                       credential,
                       "SELECT COUNT(DISTINCT Db) FROM mysql.db WHERE User = " + mariadb_quote_sql_string(user_name) + " AND Host = '%';\n",
                       "user_grant_count_checked");
}

DatabaseProviderResult MariaDBProvider::create_database(const MariaDBConnectionTarget& target,
                                                        const DatabaseProviderCredential& credential,
                                                        const std::string& database_name) const {
    const auto validation = DatabaseIdentifierValidator::validate_database_name(database_name);
    if (!validation.valid) {
        return provider_failure(validation.code, validation.message);
    }
    return execute_sql(target,
                       credential,
                       "CREATE DATABASE " + DatabaseIdentifierValidator::quote_identifier(database_name) +
                           " CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;\n",
                       "database_created");
}

DatabaseProviderResult MariaDBProvider::create_or_update_user(const MariaDBConnectionTarget& target,
                                                              const DatabaseProviderCredential& credential,
                                                              const std::string& user_name,
                                                              const std::string& password) const {
    const auto validation = DatabaseIdentifierValidator::validate_user_name(user_name);
    if (!validation.valid) {
        return provider_failure(validation.code, validation.message);
    }
    if (!password_supported(password)) {
        return provider_failure("password_invalid", "Generated database password is unsupported");
    }
    const std::string identity = quote_user(user_name);
    return execute_sql(target,
                       credential,
                       "CREATE USER IF NOT EXISTS " + identity + " IDENTIFIED BY " + mariadb_quote_sql_string(password) + ";\n"
                       "ALTER USER " + identity + " IDENTIFIED BY " + mariadb_quote_sql_string(password) + ";\n",
                       "user_created_or_updated");
}

DatabaseProviderResult MariaDBProvider::grant_database_privileges(const MariaDBConnectionTarget& target,
                                                                  const DatabaseProviderCredential& credential,
                                                                  const std::string& database_name,
                                                                  const std::string& user_name) const {
    const auto db_validation = DatabaseIdentifierValidator::validate_database_name(database_name);
    const auto user_validation = DatabaseIdentifierValidator::validate_user_name(user_name);
    if (!db_validation.valid) return provider_failure(db_validation.code, db_validation.message);
    if (!user_validation.valid) return provider_failure(user_validation.code, user_validation.message);
    return execute_sql(target,
                       credential,
                       "GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, INDEX, ALTER, CREATE TEMPORARY TABLES, LOCK TABLES ON " +
                           DatabaseIdentifierValidator::quote_identifier(database_name) + ".* TO " + quote_user(user_name) + ";\nFLUSH PRIVILEGES;\n",
                       "privileges_granted");
}

DatabaseProviderResult MariaDBProvider::revoke_database_privileges(const MariaDBConnectionTarget& target,
                                                                   const DatabaseProviderCredential& credential,
                                                                   const std::string& database_name,
                                                                   const std::string& user_name) const {
    const auto db_validation = DatabaseIdentifierValidator::validate_database_name(database_name);
    const auto user_validation = DatabaseIdentifierValidator::validate_user_name(user_name);
    if (!db_validation.valid) return provider_failure(db_validation.code, db_validation.message);
    if (!user_validation.valid) return provider_failure(user_validation.code, user_validation.message);
    return execute_sql(target,
                       credential,
                       "REVOKE ALL PRIVILEGES ON " + DatabaseIdentifierValidator::quote_identifier(database_name) + ".* FROM " + quote_user(user_name) + ";\nFLUSH PRIVILEGES;\n",
                       "privileges_revoked");
}

DatabaseProviderResult MariaDBProvider::drop_database(const MariaDBConnectionTarget& target,
                                                      const DatabaseProviderCredential& credential,
                                                      const std::string& database_name) const {
    const auto validation = DatabaseIdentifierValidator::validate_database_name(database_name);
    if (!validation.valid) {
        return provider_failure(validation.code, validation.message);
    }
    return execute_sql(target,
                       credential,
                       "DROP DATABASE IF EXISTS " + DatabaseIdentifierValidator::quote_identifier(database_name) + ";\n",
                       "database_dropped");
}

DatabaseProviderResult MariaDBProvider::drop_user(const MariaDBConnectionTarget& target,
                                                  const DatabaseProviderCredential& credential,
                                                  const std::string& user_name) const {
    const auto validation = DatabaseIdentifierValidator::validate_user_name(user_name);
    if (!validation.valid) {
        return provider_failure(validation.code, validation.message);
    }
    return execute_sql(target, credential, "DROP USER IF EXISTS " + quote_user(user_name) + ";\n", "user_dropped");
}

DatabaseProviderResult MariaDBProvider::verify_login(const MariaDBConnectionTarget& target,
                                                     const std::string& database_name,
                                                     const std::string& user_name,
                                                     const std::string& password) const {
    const auto db_validation = DatabaseIdentifierValidator::validate_database_name(database_name);
    const auto user_validation = DatabaseIdentifierValidator::validate_user_name(user_name);
    if (!db_validation.valid) return provider_failure(db_validation.code, db_validation.message);
    if (!user_validation.valid) return provider_failure(user_validation.code, user_validation.message);
    if (!password_supported(password)) return provider_failure("password_invalid", "Database password is unsupported");
    return execute_sql(target, {user_name, password, "localhost"}, "SELECT 1;\n", "login_verified", database_name);
}

} // namespace containercp::database
