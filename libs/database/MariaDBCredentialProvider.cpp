#include "MariaDBCredentialProvider.h"

#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::database {
namespace {

namespace fs = std::filesystem;

constexpr const char* kContainerScript =
    "tmpd=$(mktemp -d); "
    "trap 'rm -rf \"$tmpd\"' EXIT HUP INT TERM; "
    "conf=\"$tmpd/client.cnf\"; sql=\"$tmpd/query.sql\"; "
    "IFS= read -r magic; [ \"$magic\" = CONTAINERCP-MARIADB-FRAME-V1 ]; "
    "IFS= read -r conf_len; IFS= read -r sql_len; "
    "case \"$conf_len\" in (''|*[!0-9]*) exit 64;; esac; "
    "case \"$sql_len\" in (''|*[!0-9]*) exit 64;; esac; "
    "dd bs=1 count=\"$conf_len\" of=\"$conf\" status=none; "
    "dd bs=1 count=\"$sql_len\" of=\"$sql\" status=none; "
    "chmod 600 \"$conf\" \"$sql\"; "
    "mariadb --batch --raw --skip-column-names --defaults-extra-file=\"$conf\" < \"$sql\"";

constexpr std::size_t kMaxTransportValueLength = 256;

struct SharedUserFacts {
    int exact_identity = -1;
    int username_identities = -1;
    int other_host_identities = -1;
    int schema_grants = -1;
};

MariaDBCredentialResult failure(std::string code, std::string message) {
    MariaDBCredentialResult result;
    result.success = false;
    result.code = std::move(code);
    result.message = std::move(message);
    result.shared_assessment.state = MariaDBSharedCredentialAssessmentState::Unknown;
    return result;
}

MariaDBCredentialResult success(std::string code, std::string message) {
    MariaDBCredentialResult result;
    result.success = true;
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

bool parse_nonnegative_int(const std::string& value, int& parsed) {
    if (value.empty()) {
        return false;
    }
    long long number = 0;
    for (char c : value) {
        if (c < '0' || c > '9') {
            return false;
        }
        number = number * 10 + (c - '0');
        if (number > 1000000000LL) {
            return false;
        }
    }
    parsed = static_cast<int>(number);
    return true;
}

bool parse_shared_user_facts(const std::string& output, SharedUserFacts& facts) {
    std::istringstream stream(output);
    std::string line;
    std::map<std::string, int*> fields = {
        {"exact_identity", &facts.exact_identity},
        {"username_identities", &facts.username_identities},
        {"other_host_identities", &facts.other_host_identities},
        {"schema_grants", &facts.schema_grants},
    };
    std::map<std::string, bool> seen;
    bool saw_line = false;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        saw_line = true;
        const auto tab = line.find('\t');
        if (tab == std::string::npos || line.find('\t', tab + 1) != std::string::npos) {
            return false;
        }
        const std::string key = line.substr(0, tab);
        const std::string value = line.substr(tab + 1);
        auto it = fields.find(key);
        if (it == fields.end() || seen[key]) {
            return false;
        }
        int parsed = -1;
        if (!parse_nonnegative_int(value, parsed)) {
            return false;
        }
        *it->second = parsed;
        seen[key] = true;
    }
    if (!saw_line) {
        return false;
    }
    for (const auto& field : fields) {
        if (!seen[field.first]) {
            return false;
        }
    }
    return true;
}

MariaDBSharedCredentialAssessment assess_from_facts(const MariaDBUserIdentity& identity, const SharedUserFacts& facts) {
    MariaDBSharedCredentialAssessment assessment;
    assessment.identity = identity;
    assessment.exact_identity_count = facts.exact_identity;
    assessment.username_identity_count = facts.username_identities;
    assessment.other_host_identity_count = facts.other_host_identities;
    assessment.schema_grant_count = facts.schema_grants;
    assessment.exact_identity_exists = facts.exact_identity == 1;
    assessment.username_has_other_hosts = facts.other_host_identities > 0;

    if (facts.exact_identity == 0) {
        assessment.state = facts.username_identities > 0
            ? MariaDBSharedCredentialAssessmentState::MultipleHostIdentities
            : MariaDBSharedCredentialAssessmentState::IdentityMissing;
        return assessment;
    }
    if (facts.exact_identity != 1) {
        assessment.state = MariaDBSharedCredentialAssessmentState::Unknown;
        return assessment;
    }
    if (facts.other_host_identities > 0 || facts.username_identities > 1) {
        assessment.state = MariaDBSharedCredentialAssessmentState::MultipleHostIdentities;
        return assessment;
    }
    if (facts.schema_grants == 1) {
        assessment.state = MariaDBSharedCredentialAssessmentState::NotShared;
        return assessment;
    }
    if (facts.schema_grants > 1) {
        assessment.state = MariaDBSharedCredentialAssessmentState::Shared;
        return assessment;
    }

    assessment.state = MariaDBSharedCredentialAssessmentState::Unknown;
    return assessment;
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

bool is_safe_mariadb_transport_value(const std::string& value) {
    if (value.empty() || value.size() > kMaxTransportValueLength) {
        return false;
    }
    if (value.find("--CONTAINERCP-SQL--") != std::string::npos) {
        return false;
    }
    for (unsigned char c : value) {
        const bool alpha_num = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        const bool punctuation = c == '_' || c == '-' || c == '.' || c == '@' || c == '%' || c == ':' || c == '$';
        if (!alpha_num && !punctuation) {
            return false;
        }
    }
    return true;
}

MariaDBCredentialResult transport_value_failure() {
    return failure("credential_transport_invalid", "MariaDB credential transport input is unsupported");
}

bool is_safe_identity(const MariaDBUserIdentity& identity) {
    return is_safe_mariadb_transport_value(identity.user) && is_safe_mariadb_transport_value(identity.host);
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
    const std::string defaults = "[client]\nuser=" + user + "\npassword=" + password + "\nhost=" + host + "\n";
    return "CONTAINERCP-MARIADB-FRAME-V1\n" + std::to_string(defaults.size()) + "\n" +
           std::to_string(sql.size()) + "\n" + defaults + sql;
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

std::string mariadb_shared_credential_assessment_state_to_string(MariaDBSharedCredentialAssessmentState state) {
    switch (state) {
    case MariaDBSharedCredentialAssessmentState::NotShared:
        return "not_shared";
    case MariaDBSharedCredentialAssessmentState::Shared:
        return "shared";
    case MariaDBSharedCredentialAssessmentState::Unknown:
        return "unknown";
    case MariaDBSharedCredentialAssessmentState::IdentityMissing:
        return "identity_missing";
    case MariaDBSharedCredentialAssessmentState::MultipleHostIdentities:
        return "multiple_host_identities";
    case MariaDBSharedCredentialAssessmentState::MetadataConflict:
        return "metadata_conflict";
    }
    return "unknown";
}

bool mariadb_shared_credential_rotation_allowed(MariaDBSharedCredentialAssessmentState state) {
    return state == MariaDBSharedCredentialAssessmentState::NotShared;
}

MariaDBCredentialResult MariaDBCredentialProvider::execute_sql(const MariaDBConnectionTarget& target,
                                                               const std::string& mysql_user,
                                                               const std::string& mysql_password,
                                                               const std::string& mysql_host,
                                                               const std::string& sql,
                                                               const std::string& success_code,
                                                               runtime::CommandResult* command_out) const {
    if (target.compose_file.empty() || target.service.empty()) {
        return failure("target_invalid", "MariaDB target is incomplete");
    }
    if (!is_safe_mariadb_transport_value(mysql_user) ||
        !is_safe_mariadb_transport_value(mysql_password) ||
        !is_safe_mariadb_transport_value(mysql_host)) {
        return transport_value_failure();
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
    if (command_out != nullptr) {
        *command_out = command;
    }
    if (command.exit_code != 0) {
        return failure("mariadb_command_failed", "MariaDB credential operation failed");
    }
    return success(success_code, "MariaDB credential operation completed");
}

MariaDBCredentialResult MariaDBCredentialProvider::verify_password(const MariaDBConnectionTarget& target,
                                                                    const MariaDBUserIdentity& identity,
                                                                    const std::string& password) const {
    if (!is_safe_identity(identity)) {
        return transport_value_failure();
    }
    return execute_sql(target, identity.user, password, "localhost", "SELECT 1;", "verified");
}

MariaDBCredentialResult MariaDBCredentialProvider::change_password(const MariaDBConnectionTarget& target,
                                                                    const MariaDBAdminCredential& admin,
                                                                    const MariaDBUserIdentity& identity,
                                                                    const std::string& new_password) const {
    if (!is_safe_identity(identity) || !is_safe_mariadb_transport_value(new_password)) {
        return transport_value_failure();
    }
    return execute_sql(target, admin.user, admin.password, admin.host, alter_user_sql(identity, new_password), "password_changed");
}

MariaDBCredentialResult MariaDBCredentialProvider::restore_password(const MariaDBConnectionTarget& target,
                                                                    const MariaDBAdminCredential& admin,
                                                                      const MariaDBUserIdentity& identity,
                                                                      const std::string& old_password) const {
    if (!is_safe_identity(identity) || !is_safe_mariadb_transport_value(old_password)) {
        return transport_value_failure();
    }
    return execute_sql(target, admin.user, admin.password, admin.host, alter_user_sql(identity, old_password), "password_restored");
}

MariaDBCredentialResult MariaDBCredentialProvider::detect_shared_user(const MariaDBConnectionTarget& target,
                                                                       const MariaDBAdminCredential& admin,
                                                                       const MariaDBUserIdentity& identity) const {
    if (!is_safe_identity(identity)) {
        auto result = transport_value_failure();
        result.shared_assessment.identity = identity;
        result.shared_assessment.state = MariaDBSharedCredentialAssessmentState::Unknown;
        result.shared_user = true;
        return result;
    }
    const std::string quoted_user = mariadb_quote_sql_string(identity.user);
    const std::string quoted_host = mariadb_quote_sql_string(identity.host);
    const std::string sql =
        "SELECT 'exact_identity', COUNT(*) FROM mysql.user WHERE User = " + quoted_user + " AND Host = " + quoted_host + "\n"
        "UNION ALL SELECT 'username_identities', COUNT(*) FROM mysql.user WHERE User = " + quoted_user + "\n"
        "UNION ALL SELECT 'other_host_identities', COUNT(*) FROM mysql.user WHERE User = " + quoted_user + " AND Host <> " + quoted_host + "\n"
        "UNION ALL SELECT 'schema_grants', COUNT(DISTINCT Db) FROM mysql.db WHERE User = " + quoted_user + " AND Host = " + quoted_host + ";";
    runtime::CommandResult command;
    auto result = execute_sql(target, admin.user, admin.password, admin.host, sql, "shared_user_checked", &command);
    result.shared_assessment.identity = identity;
    if (!result.success) {
        result.shared_assessment.state = MariaDBSharedCredentialAssessmentState::Unknown;
        result.shared_user = true;
        return result;
    }

    SharedUserFacts facts;
    if (!parse_shared_user_facts(command.out, facts)) {
        result.success = false;
        result.code = "shared_user_assessment_invalid";
        result.message = "MariaDB shared credential assessment failed";
        result.shared_assessment.identity = identity;
        result.shared_assessment.state = MariaDBSharedCredentialAssessmentState::Unknown;
        result.shared_user = true;
        return result;
    }

    result.shared_assessment = assess_from_facts(identity, facts);
    result.shared_user = result.shared_assessment.state != MariaDBSharedCredentialAssessmentState::NotShared;
    return result;
}

} // namespace containercp::database
