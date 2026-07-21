#ifndef CONTAINERCP_DATABASE_MARIADB_CREDENTIAL_PROVIDER_H
#define CONTAINERCP_DATABASE_MARIADB_CREDENTIAL_PROVIDER_H

#include "runtime/CommandExecutor.h"

#include <string>
#include <vector>

namespace containercp::database {

struct MariaDBUserIdentity {
    std::string user;
    std::string host = "%";
};

struct MariaDBConnectionTarget {
    std::string compose_file;
    std::string service = "mariadb";
};

struct MariaDBAdminCredential {
    std::string user;
    std::string password;
    std::string host = "localhost";
};

enum class MariaDBSharedCredentialAssessmentState {
    NotShared,
    Shared,
    Unknown,
    IdentityMissing,
    MultipleHostIdentities,
    MetadataConflict,
};

struct MariaDBSharedCredentialAssessment {
    MariaDBSharedCredentialAssessmentState state = MariaDBSharedCredentialAssessmentState::Unknown;
    MariaDBUserIdentity identity;
    bool exact_identity_exists = false;
    bool username_has_other_hosts = false;
    int exact_identity_count = -1;
    int username_identity_count = -1;
    int other_host_identity_count = -1;
    int schema_grant_count = -1;
    int metadata_reference_count = -1;
};

struct MariaDBCredentialResult {
    bool success = false;
    std::string code;
    std::string message;
    bool shared_user = false;
    MariaDBSharedCredentialAssessment shared_assessment;
};

class MariaDBProcessRunner {
public:
    virtual ~MariaDBProcessRunner() = default;
    virtual runtime::CommandResult run(const std::vector<std::string>& args,
                                       const std::string& workdir = "") const;
    virtual runtime::CommandResult run_with_stdin_file(const std::vector<std::string>& args,
                                                       const std::string& stdin_file,
                                                       const std::string& workdir = "") const = 0;
};

class MariaDBCommandExecutorRunner : public MariaDBProcessRunner {
public:
    explicit MariaDBCommandExecutorRunner(const runtime::CommandExecutor& executor);

    runtime::CommandResult run(const std::vector<std::string>& args,
                               const std::string& workdir = "") const override;

    runtime::CommandResult run_with_stdin_file(const std::vector<std::string>& args,
                                               const std::string& stdin_file,
                                               const std::string& workdir = "") const override;

private:
    const runtime::CommandExecutor& executor_;
};

class MariaDBCredentialProvider {
public:
    explicit MariaDBCredentialProvider(const MariaDBProcessRunner& runner);

    MariaDBCredentialResult verify_password(const MariaDBConnectionTarget& target,
                                            const MariaDBUserIdentity& identity,
                                            const std::string& password) const;

    MariaDBCredentialResult change_password(const MariaDBConnectionTarget& target,
                                            const MariaDBAdminCredential& admin,
                                            const MariaDBUserIdentity& identity,
                                            const std::string& new_password) const;

    MariaDBCredentialResult restore_password(const MariaDBConnectionTarget& target,
                                             const MariaDBAdminCredential& admin,
                                             const MariaDBUserIdentity& identity,
                                             const std::string& old_password) const;

    MariaDBCredentialResult detect_shared_user(const MariaDBConnectionTarget& target,
                                               const MariaDBAdminCredential& admin,
                                               const MariaDBUserIdentity& identity) const;

private:
    MariaDBCredentialResult execute_sql(const MariaDBConnectionTarget& target,
                                        const std::string& mysql_user,
                                        const std::string& mysql_password,
                                        const std::string& mysql_host,
                                        const std::string& sql,
                                        const std::string& success_code,
                                        runtime::CommandResult* command_out = nullptr) const;

    const MariaDBProcessRunner& runner_;
};

std::string mariadb_quote_sql_string(const std::string& value);
std::string mariadb_shared_credential_assessment_state_to_string(MariaDBSharedCredentialAssessmentState state);
bool mariadb_shared_credential_rotation_allowed(MariaDBSharedCredentialAssessmentState state);

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_MARIADB_CREDENTIAL_PROVIDER_H
