#include "MariaDBCredentialProvider.h"

#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::database {
namespace {

namespace fs = std::filesystem;

constexpr const char* kContainerScript =
    "tmpd=$(mktemp -d); "
    "trap 'rm -rf \"$tmpd\"' EXIT HUP INT TERM; "
    "conf=\"$tmpd/client.cnf\"; sql=\"$tmpd/query.sql\"; "
    "awk 'BEGIN{part=\"conf\"} /^--CONTAINERCP-SQL--$/{part=\"sql\"; next} {if(part==\"conf\") print > c; else print > s}' c=\"$conf\" s=\"$sql\"; "
    "chmod 600 \"$conf\" \"$sql\"; "
    "mariadb --defaults-extra-file=\"$conf\" < \"$sql\"";

MariaDBCredentialResult failure(std::string code, std::string message) {
    MariaDBCredentialResult result;
    result.success = false;
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

MariaDBCredentialResult success(std::string code, std::string message) {
    MariaDBCredentialResult result;
    result.success = true;
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

bool write_protected_file(const fs::path& path, const std::string& content) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return false;
    }
    const char* data = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            (void)::close(fd);
            (void)::unlink(path.c_str());
            return false;
        }
        if (written == 0) {
            (void)::close(fd);
            (void)::unlink(path.c_str());
            return false;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    const bool ok = ::fsync(fd) == 0 && ::close(fd) == 0;
    if (!ok) {
        (void)::unlink(path.c_str());
    }
    return ok;
}

fs::path make_bundle_path() {
    static unsigned long counter = 0;
    ++counter;
    return fs::temp_directory_path() /
           ("containercp-mariadb-credentials-" + std::to_string(::getpid()) + "-" + std::to_string(counter));
}

std::string defaults_bundle(const std::string& user,
                            const std::string& password,
                            const std::string& host,
                            const std::string& sql) {
    return "[client]\nuser=" + user + "\npassword=" + password + "\nhost=" + host +
           "\n--CONTAINERCP-SQL--\n" + sql + "\n";
}

std::string alter_user_sql(const MariaDBUserIdentity& identity, const std::string& password) {
    return "ALTER USER " + mariadb_quote_sql_string(identity.user) + "@" + mariadb_quote_sql_string(identity.host) +
           " IDENTIFIED BY " + mariadb_quote_sql_string(password) + ";\nFLUSH PRIVILEGES;";
}

} // namespace

MariaDBCommandExecutorRunner::MariaDBCommandExecutorRunner(const runtime::CommandExecutor& executor)
    : executor_(executor) {
}

runtime::CommandResult MariaDBCommandExecutorRunner::run_with_stdin_file(const std::vector<std::string>& args,
                                                                         const std::string& stdin_file,
                                                                         const std::string& workdir) const {
    return executor_.run_with_stdin_file(args, stdin_file, workdir);
}

MariaDBCredentialProvider::MariaDBCredentialProvider(const MariaDBProcessRunner& runner)
    : runner_(runner) {
}

std::string mariadb_quote_sql_string(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\\' || c == '\'') {
            quoted.push_back('\\');
        }
        quoted.push_back(c);
    }
    quoted.push_back('\'');
    return quoted;
}

MariaDBCredentialResult MariaDBCredentialProvider::execute_sql(const MariaDBConnectionTarget& target,
                                                               const std::string& mysql_user,
                                                               const std::string& mysql_password,
                                                               const std::string& mysql_host,
                                                               const std::string& sql,
                                                               const std::string& success_code) const {
    if (target.compose_file.empty() || target.service.empty()) {
        return failure("target_invalid", "MariaDB target is incomplete");
    }
    if (mysql_user.empty()) {
        return failure("credential_invalid", "MariaDB credential is incomplete");
    }

    const fs::path bundle_path = make_bundle_path();
    if (!write_protected_file(bundle_path, defaults_bundle(mysql_user, mysql_password, mysql_host, sql))) {
        return failure("secret_transport_failed", "MariaDB credential transport could not be prepared");
    }

    const std::vector<std::string> args = {
        "docker", "compose", "-f", target.compose_file,
        "exec", "-T", target.service,
        "sh", "-eu", "-c", kContainerScript,
    };

    const auto command = runner_.run_with_stdin_file(args, bundle_path.string());
    (void)::unlink(bundle_path.c_str());
    if (command.exit_code != 0) {
        return failure("mariadb_command_failed", "MariaDB credential operation failed");
    }
    return success(success_code, "MariaDB credential operation completed");
}

MariaDBCredentialResult MariaDBCredentialProvider::verify_password(const MariaDBConnectionTarget& target,
                                                                   const MariaDBUserIdentity& identity,
                                                                   const std::string& password) const {
    return execute_sql(target, identity.user, password, "localhost", "SELECT 1;", "verified");
}

MariaDBCredentialResult MariaDBCredentialProvider::change_password(const MariaDBConnectionTarget& target,
                                                                   const MariaDBAdminCredential& admin,
                                                                   const MariaDBUserIdentity& identity,
                                                                   const std::string& new_password) const {
    return execute_sql(target, admin.user, admin.password, admin.host, alter_user_sql(identity, new_password), "password_changed");
}

MariaDBCredentialResult MariaDBCredentialProvider::restore_password(const MariaDBConnectionTarget& target,
                                                                    const MariaDBAdminCredential& admin,
                                                                    const MariaDBUserIdentity& identity,
                                                                    const std::string& old_password) const {
    return execute_sql(target, admin.user, admin.password, admin.host, alter_user_sql(identity, old_password), "password_restored");
}

MariaDBCredentialResult MariaDBCredentialProvider::detect_shared_user(const MariaDBConnectionTarget& target,
                                                                      const MariaDBAdminCredential& admin,
                                                                      const MariaDBUserIdentity& identity) const {
    const std::string sql = "SELECT COUNT(*) FROM mysql.user WHERE User = " + mariadb_quote_sql_string(identity.user) + ";";
    auto result = execute_sql(target, admin.user, admin.password, admin.host, sql, "shared_user_checked");
    result.shared_user = false;
    return result;
}

} // namespace containercp::database
