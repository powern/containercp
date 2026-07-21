#include "database/MariaDBProvider.h"

#include "database/DatabaseIdentifierValidator.h"
#include "database/MariaDBSecureTempFile.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::database {
namespace {

DatabaseProviderResult provider_failure(std::string code, std::string message) {
    return {false, std::move(code), std::move(message), {}};
}

DatabaseProviderResult provider_success(std::string code, std::string message, std::string output = {}) {
    return {true, std::move(code), std::move(message), std::move(output)};
}

bool contains_case_insensitive(const std::string& value, const std::string& needle) {
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string haystack;
    std::string target;
    haystack.reserve(value.size());
    target.reserve(needle.size());
    std::transform(value.begin(), value.end(), std::back_inserter(haystack), lower);
    std::transform(needle.begin(), needle.end(), std::back_inserter(target), lower);
    return haystack.find(target) != std::string::npos;
}

std::string redact_identified_by(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size();) {
        const std::string keyword = "IDENTIFIED BY";
        if (i + keyword.size() <= value.size() && contains_case_insensitive(value.substr(i, keyword.size()), keyword)) {
            out += keyword;
            i += keyword.size();
            while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i])) != 0) {
                out.push_back(value[i++]);
            }
            out += "'<redacted>'";
            if (i < value.size() && (value[i] == '\'' || value[i] == '"')) {
                const char quote = value[i++];
                while (i < value.size()) {
                    const char c = value[i++];
                    if (c == quote) break;
                    if (c == '\\' && i < value.size()) ++i;
                }
            } else {
                while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i])) == 0 && value[i] != ';') ++i;
            }
            continue;
        }
        out.push_back(value[i++]);
    }
    return out;
}

std::string classify_mariadb_error(const std::string& value) {
    if (contains_case_insensitive(value, "ERROR 1227") && contains_case_insensitive(value, "RELOAD")) {
        return "mariadb_reload_privilege_required";
    }
    if (contains_case_insensitive(value, "Access denied") && contains_case_insensitive(value, "GRANT")) {
        return "mariadb_grant_privilege_denied";
    }
    if (contains_case_insensitive(value, "Access denied")) {
        return "mariadb_access_denied";
    }
    if (contains_case_insensitive(value, "Operation CREATE USER failed") || contains_case_insensitive(value, "ERROR 1396")) {
        return "mariadb_user_state_conflict";
    }
    return "mariadb_command_failed";
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
    const std::string redacted = redact_identified_by(value);
    std::string sanitized;
    sanitized.reserve(std::min<std::size_t>(redacted.size(), 160));
    for (unsigned char c : redacted) {
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

std::vector<std::string> MariaDBProvider::build_dump_args(const MariaDBConnectionTarget& target,
                                                          const std::string& container_option_path,
                                                          const std::string& database_name) const {
    return {
        "docker", "compose", "-f", target.compose_file,
        "exec", "-T", target.service,
        "mariadb-dump", "--defaults-extra-file=" + container_option_path,
        "--single-transaction", "--quick", "--skip-lock-tables",
        "--hex-blob", "--default-character-set=utf8mb4",
        "--skip-comments", "--skip-dump-date",
        database_name,
    };
}

bool regular_owner_only_file(const std::string& path) {
    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) return false;
    if (st.st_uid != ::geteuid()) return false;
    return (st.st_mode & (S_IRWXG | S_IRWXO)) == 0;
}

bool prepend_export_header(const std::string& output_path, const std::string& database_name) {
    const std::filesystem::path original(output_path);
    const auto tmp = original.parent_path() / (original.filename().string() + ".with-header");
    std::ifstream in(original, std::ios::binary);
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (!in.is_open() || fd < 0) {
        if (fd >= 0) (void)::close(fd);
        return false;
    }
    const std::string header = "-- ContainerCP DB-4 logical export\n-- database: " + database_name + "\n";
    if (::write(fd, header.data(), header.size()) != static_cast<ssize_t>(header.size())) {
        (void)::close(fd);
        (void)::unlink(tmp.c_str());
        return false;
    }
    char buffer[16384];
    while (in.good()) {
        in.read(buffer, sizeof(buffer));
        const auto got = in.gcount();
        if (got > 0 && ::write(fd, buffer, static_cast<std::size_t>(got)) != got) {
            (void)::close(fd);
            (void)::unlink(tmp.c_str());
            return false;
        }
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0) {
        (void)::unlink(tmp.c_str());
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, original, ec);
    if (ec) {
        (void)::unlink(tmp.c_str());
        return false;
    }
    return true;
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
        return provider_failure(classify_mariadb_error(command.err), "MariaDB operation failed: " + mariadb_sanitize_provider_error(command.err));
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
                            DatabaseIdentifierValidator::quote_identifier(database_name) + ".* TO " + quote_user(user_name) + ";\n",
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
                       "REVOKE ALL PRIVILEGES ON " + DatabaseIdentifierValidator::quote_identifier(database_name) + ".* FROM " + quote_user(user_name) + ";\n",
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

DatabaseProviderResult MariaDBProvider::export_database(const MariaDBConnectionTarget& target,
                                                        const std::string& database_name,
                                                        const std::string& user_name,
                                                        const std::string& password,
                                                        const std::string& output_path) const {
    const auto db_validation = DatabaseIdentifierValidator::validate_database_name(database_name);
    const auto user_validation = DatabaseIdentifierValidator::validate_user_name(user_name);
    if (!db_validation.valid) return provider_failure(db_validation.code, db_validation.message);
    if (!user_validation.valid) return provider_failure(user_validation.code, user_validation.message);
    if (target.compose_file.empty() || target.service.empty() || output_path.empty()) return provider_failure("target_invalid", "MariaDB export target is incomplete");
    if (!password_supported(password)) return provider_failure("password_invalid", "Database password is unsupported");

    MariaDBSecureTempFile option_file;
    try {
        option_file = MariaDBSecureTempFile::create("containercp-mariadb-dump-option", ".cnf", mariadb_service_account_option_file({user_name, password, "localhost"}));
    } catch (...) {
        return provider_failure("secret_transport_failed", "MariaDB secure credential transport could not be prepared");
    }
    const std::string container_option_path = "/tmp/containercp-" + option_file.path().filename().string();
    auto copy_result = runner_.run({"docker", "compose", "-f", target.compose_file,
                                   "cp", option_file.path().string(), target.service + ":" + container_option_path});
    if (copy_result.exit_code != 0) return provider_failure("mariadb_option_copy_failed", "MariaDB secure credential transport failed");
    auto cleanup = [&]() {
        (void)runner_.run({"docker", "compose", "-f", target.compose_file,
                           "exec", "-T", target.service, "rm", "-f", container_option_path});
    };
    const auto command = runner_.run_stdout_to_file(build_dump_args(target, container_option_path, database_name), output_path);
    cleanup();
    if (command.exit_code != 0) {
        (void)std::filesystem::remove(output_path);
        return provider_failure(classify_mariadb_error(command.err), "MariaDB dump failed: " + mariadb_sanitize_provider_error(command.err));
    }
    if (!regular_owner_only_file(output_path) || !prepend_export_header(output_path, database_name)) {
        (void)std::filesystem::remove(output_path);
        return provider_failure("artifact_finalize_failed", "MariaDB dump artifact could not be finalized safely");
    }
    return provider_success("export_completed", "MariaDB logical export completed");
}

DatabaseProviderResult MariaDBProvider::import_sql_file(const MariaDBConnectionTarget& target,
                                                        const std::string& database_name,
                                                        const std::string& user_name,
                                                        const std::string& password,
                                                        const std::string& input_path) const {
    const auto db_validation = DatabaseIdentifierValidator::validate_database_name(database_name);
    const auto user_validation = DatabaseIdentifierValidator::validate_user_name(user_name);
    if (!db_validation.valid) return provider_failure(db_validation.code, db_validation.message);
    if (!user_validation.valid) return provider_failure(user_validation.code, user_validation.message);
    if (target.compose_file.empty() || target.service.empty() || input_path.empty()) return provider_failure("target_invalid", "MariaDB import target is incomplete");
    if (!password_supported(password)) return provider_failure("password_invalid", "Database password is unsupported");
    if (!regular_owner_only_file(input_path)) return provider_failure("import_artifact_invalid", "Import artifact is not a safe regular file");

    MariaDBSecureTempFile option_file;
    try {
        option_file = MariaDBSecureTempFile::create("containercp-mariadb-import-option", ".cnf", mariadb_service_account_option_file({user_name, password, "localhost"}));
    } catch (...) {
        return provider_failure("secret_transport_failed", "MariaDB secure credential transport could not be prepared");
    }
    const std::string container_option_path = "/tmp/containercp-" + option_file.path().filename().string();
    auto copy_result = runner_.run({"docker", "compose", "-f", target.compose_file,
                                   "cp", option_file.path().string(), target.service + ":" + container_option_path});
    if (copy_result.exit_code != 0) return provider_failure("mariadb_option_copy_failed", "MariaDB secure credential transport failed");
    auto cleanup = [&]() {
        (void)runner_.run({"docker", "compose", "-f", target.compose_file,
                           "exec", "-T", target.service, "rm", "-f", container_option_path});
    };
    auto args = build_exec_args(target, container_option_path, database_name);
    args.push_back("--local-infile=0");
    const auto command = runner_.run_with_stdin_file(args, input_path);
    cleanup();
    if (command.exit_code != 0) {
        return provider_failure(classify_mariadb_error(command.err), "MariaDB import failed: " + mariadb_sanitize_provider_error(command.err));
    }
    return provider_success("import_completed", "MariaDB import completed");
}

} // namespace containercp::database
