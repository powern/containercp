#ifndef CONTAINERCP_DATABASE_MARIADB_PROVIDER_H
#define CONTAINERCP_DATABASE_MARIADB_PROVIDER_H

#include "database/DatabaseProvider.h"

#include <vector>

namespace containercp::database {

class MariaDBProvider : public DatabaseProvider {
public:
    explicit MariaDBProvider(const MariaDBProcessRunner& runner);

    DatabaseProviderResult verify_service_account(const MariaDBConnectionTarget& target,
                                                  const DatabaseProviderCredential& credential) const override;
    DatabaseProviderResult database_exists(const MariaDBConnectionTarget& target,
                                           const DatabaseProviderCredential& credential,
                                           const std::string& database_name) const override;
    DatabaseProviderResult user_exists(const MariaDBConnectionTarget& target,
                                       const DatabaseProviderCredential& credential,
                                       const std::string& user_name) const override;
    DatabaseProviderResult user_schema_grant_count(const MariaDBConnectionTarget& target,
                                                   const DatabaseProviderCredential& credential,
                                                   const std::string& user_name) const override;
    DatabaseProviderResult create_database(const MariaDBConnectionTarget& target,
                                           const DatabaseProviderCredential& credential,
                                           const std::string& database_name) const override;
    DatabaseProviderResult create_or_update_user(const MariaDBConnectionTarget& target,
                                                 const DatabaseProviderCredential& credential,
                                                 const std::string& user_name,
                                                 const std::string& password) const override;
    DatabaseProviderResult grant_database_privileges(const MariaDBConnectionTarget& target,
                                                      const DatabaseProviderCredential& credential,
                                                      const std::string& database_name,
                                                      const std::string& user_name) const override;
    DatabaseProviderResult create_temporary_sql_console_user(const MariaDBConnectionTarget& target,
                                                             const DatabaseProviderCredential& credential,
                                                             const std::string& database_name,
                                                             const std::string& user_name,
                                                             const std::string& password) const override;
    DatabaseProviderResult drop_temporary_sql_console_user(const MariaDBConnectionTarget& target,
                                                           const DatabaseProviderCredential& credential,
                                                           const std::string& database_name,
                                                           const std::string& user_name) const override;
    DatabaseProviderResult revoke_database_privileges(const MariaDBConnectionTarget& target,
                                                       const DatabaseProviderCredential& credential,
                                                       const std::string& database_name,
                                                       const std::string& user_name) const override;
    DatabaseProviderResult drop_database(const MariaDBConnectionTarget& target,
                                         const DatabaseProviderCredential& credential,
                                         const std::string& database_name) const override;
    DatabaseProviderResult drop_user(const MariaDBConnectionTarget& target,
                                     const DatabaseProviderCredential& credential,
                                     const std::string& user_name) const override;
    DatabaseProviderResult verify_login(const MariaDBConnectionTarget& target,
                                         const std::string& database_name,
                                         const std::string& user_name,
                                         const std::string& password) const override;
    DatabaseProviderResult export_database(const MariaDBConnectionTarget& target,
                                           const std::string& database_name,
                                           const std::string& user_name,
                                           const std::string& password,
                                           const std::string& output_path) const override;
    DatabaseProviderResult import_sql_file(const MariaDBConnectionTarget& target,
                                           const std::string& database_name,
                                           const std::string& user_name,
                                           const std::string& password,
                                           const std::string& input_path) const override;

private:
    DatabaseProviderResult execute_sql(const MariaDBConnectionTarget& target,
                                       const DatabaseProviderCredential& credential,
                                       const std::string& sql,
                                       const std::string& success_code,
                                       const std::string& database_name = {}) const;
    std::vector<std::string> build_exec_args(const MariaDBConnectionTarget& target,
                                              const std::string& container_option_path,
                                              const std::string& database_name) const;
    std::vector<std::string> build_dump_args(const MariaDBConnectionTarget& target,
                                             const std::string& container_option_path,
                                             const std::string& database_name) const;

    const MariaDBProcessRunner& runner_;
};

std::string mariadb_service_account_option_file(const DatabaseProviderCredential& credential);
std::string mariadb_sanitize_provider_error(const std::string& value);

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_MARIADB_PROVIDER_H
