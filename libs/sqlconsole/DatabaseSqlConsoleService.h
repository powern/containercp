#ifndef CONTAINERCP_SQLCONSOLE_DATABASE_SQL_CONSOLE_SERVICE_H
#define CONTAINERCP_SQLCONSOLE_DATABASE_SQL_CONSOLE_SERVICE_H

#include "database/DatabaseProvider.h"
#include "sqlconsole/SqlConsoleSessionManager.h"
#include "sqlconsole/SqlConsoleSessionStore.h"

#include <functional>
#include <filesystem>
#include <optional>

namespace containercp::sqlconsole {

struct SqlConsoleTemporaryCredential {
    std::string database_name;
    std::string user_name;
    std::string password;
};

struct SqlConsoleProvisionRequest {
    SqlConsoleCreateRequest launch;
    database::MariaDBConnectionTarget target;
    database::DatabaseProviderCredential service_account;
    std::string database_name;
};

struct SqlConsoleSiteProvisionRequest {
    SqlConsoleCreateRequest launch;
    std::filesystem::path site_root;
    std::string database_name;
};

struct SqlConsoleProvisionResult : SqlConsoleCreateResult {
    SqlConsoleTemporaryCredential temporary_credential;
};

struct SqlConsoleCleanupRequest {
    std::string launch_id;
    database::MariaDBConnectionTarget target;
    database::DatabaseProviderCredential service_account;
};

struct SqlConsoleRecoveryTarget {
    database::MariaDBConnectionTarget target;
    database::DatabaseProviderCredential service_account;
};

struct SqlConsoleRecoveryResult {
    bool success = true;
    std::string code = "recovered";
    std::string message = "SQL Console persisted sessions recovered";
    std::size_t inspected = 0;
    std::size_t cleaned = 0;
    std::size_t failed = 0;
};

struct SqlConsoleInternalRedeemResult : SqlConsoleOperationResult {
    SqlConsoleTemporaryCredential temporary_credential;
};

class DatabaseSqlConsoleService {
public:
    explicit DatabaseSqlConsoleService(SqlConsoleSessionPolicy policy = {});
    DatabaseSqlConsoleService(const database::DatabaseProvider& provider, SqlConsoleSessionPolicy policy = {});
    DatabaseSqlConsoleService(const database::DatabaseProvider& provider, SqlConsoleSessionStore& store, SqlConsoleSessionPolicy policy = {});

    SqlConsoleCreateResult create_launch_session(const SqlConsoleCreateRequest& request);
    SqlConsoleProvisionResult create_temporary_launch_session(const SqlConsoleProvisionRequest& request);
    SqlConsoleProvisionResult create_site_temporary_launch_session(const SqlConsoleSiteProvisionRequest& request);
    SqlConsoleOperationResult redeem_launch_session(const std::string& launch_id, const std::string& launch_secret);
    SqlConsoleOperationResult authorize_launch_session(const std::string& launch_id, const std::string& launch_secret);
    SqlConsoleInternalRedeemResult redeem_internal_launch_session(const std::string& launch_id, const std::string& launch_secret);
    SqlConsoleOperationResult touch_launch_session(const std::string& launch_id, const std::string& launch_secret);
    SqlConsoleOperationResult revoke_launch_session(const std::string& launch_id);
    SqlConsoleOperationResult revoke_temporary_launch_session(const SqlConsoleCleanupRequest& request);
    SqlConsoleOperationResult revoke_site_temporary_launch_session(const std::string& launch_id, const std::filesystem::path& site_root);
    std::size_t sweep_expired_sessions();
    SqlConsoleRecoveryResult recover_persisted_sessions(const std::function<std::optional<SqlConsoleRecoveryTarget>(const SqlConsoleSession&)>& resolver);
    std::vector<SqlConsolePublicSession> list_sessions(uint64_t database_id = 0);
    const std::string& internal_api_token() const;
    SqlConsoleSessionManager& sessions();
    const SqlConsoleSessionManager& sessions() const;

private:
    std::string temporary_user_name(const std::string& launch_id) const;
    SqlConsoleProvisionRequest resolve_site_request(const SqlConsoleSiteProvisionRequest& request) const;
    void persist_sessions() const;

    const database::DatabaseProvider* provider_ = nullptr;
    SqlConsoleSessionStore* store_ = nullptr;
    std::string internal_api_token_;
    SqlConsoleSessionManager sessions_;
};

} // namespace containercp::sqlconsole

#endif // CONTAINERCP_SQLCONSOLE_DATABASE_SQL_CONSOLE_SERVICE_H
