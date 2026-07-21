#ifndef CONTAINERCP_DATABASE_DATABASE_PROVIDER_H
#define CONTAINERCP_DATABASE_DATABASE_PROVIDER_H

#include "database/MariaDBCredentialProvider.h"

#include <string>

namespace containercp::database {

struct DatabaseProviderResult {
    bool success = false;
    std::string code;
    std::string message;
    std::string output;
};

struct DatabaseProviderCredential {
    std::string user;
    std::string password;
    std::string host = "localhost";
};

class DatabaseProvider {
public:
    virtual ~DatabaseProvider() = default;

    virtual DatabaseProviderResult verify_service_account(const MariaDBConnectionTarget& target,
                                                          const DatabaseProviderCredential& credential) const = 0;
    virtual DatabaseProviderResult database_exists(const MariaDBConnectionTarget& target,
                                                   const DatabaseProviderCredential& credential,
                                                   const std::string& database_name) const = 0;
    virtual DatabaseProviderResult user_exists(const MariaDBConnectionTarget& target,
                                               const DatabaseProviderCredential& credential,
                                               const std::string& user_name) const = 0;
    virtual DatabaseProviderResult user_schema_grant_count(const MariaDBConnectionTarget& target,
                                                           const DatabaseProviderCredential& credential,
                                                           const std::string& user_name) const = 0;
    virtual DatabaseProviderResult create_database(const MariaDBConnectionTarget& target,
                                                   const DatabaseProviderCredential& credential,
                                                   const std::string& database_name) const = 0;
    virtual DatabaseProviderResult create_or_update_user(const MariaDBConnectionTarget& target,
                                                         const DatabaseProviderCredential& credential,
                                                         const std::string& user_name,
                                                         const std::string& password) const = 0;
    virtual DatabaseProviderResult grant_database_privileges(const MariaDBConnectionTarget& target,
                                                             const DatabaseProviderCredential& credential,
                                                             const std::string& database_name,
                                                             const std::string& user_name) const = 0;
    virtual DatabaseProviderResult revoke_database_privileges(const MariaDBConnectionTarget& target,
                                                              const DatabaseProviderCredential& credential,
                                                              const std::string& database_name,
                                                              const std::string& user_name) const = 0;
    virtual DatabaseProviderResult drop_database(const MariaDBConnectionTarget& target,
                                                 const DatabaseProviderCredential& credential,
                                                 const std::string& database_name) const = 0;
    virtual DatabaseProviderResult drop_user(const MariaDBConnectionTarget& target,
                                             const DatabaseProviderCredential& credential,
                                             const std::string& user_name) const = 0;
    virtual DatabaseProviderResult verify_login(const MariaDBConnectionTarget& target,
                                                const std::string& database_name,
                                                const std::string& user_name,
                                                const std::string& password) const = 0;
    virtual DatabaseProviderResult export_database(const MariaDBConnectionTarget& target,
                                                   const std::string& database_name,
                                                   const std::string& user_name,
                                                   const std::string& password,
                                                   const std::string& output_path) const = 0;
    virtual DatabaseProviderResult import_sql_file(const MariaDBConnectionTarget& target,
                                                   const std::string& database_name,
                                                   const std::string& user_name,
                                                   const std::string& password,
                                                   const std::string& input_path) const = 0;
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_PROVIDER_H
