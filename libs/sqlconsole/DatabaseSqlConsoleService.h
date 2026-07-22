#ifndef CONTAINERCP_SQLCONSOLE_DATABASE_SQL_CONSOLE_SERVICE_H
#define CONTAINERCP_SQLCONSOLE_DATABASE_SQL_CONSOLE_SERVICE_H

#include "database/DatabaseProvider.h"
#include "sqlconsole/SqlConsoleSessionManager.h"

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

struct SqlConsoleProvisionResult : SqlConsoleCreateResult {
    SqlConsoleTemporaryCredential temporary_credential;
};

struct SqlConsoleCleanupRequest {
    std::string launch_id;
    database::MariaDBConnectionTarget target;
    database::DatabaseProviderCredential service_account;
};

class DatabaseSqlConsoleService {
public:
    explicit DatabaseSqlConsoleService(SqlConsoleSessionPolicy policy = {});
    DatabaseSqlConsoleService(const database::DatabaseProvider& provider, SqlConsoleSessionPolicy policy = {});

    SqlConsoleCreateResult create_launch_session(const SqlConsoleCreateRequest& request);
    SqlConsoleProvisionResult create_temporary_launch_session(const SqlConsoleProvisionRequest& request);
    SqlConsoleOperationResult redeem_launch_session(const std::string& launch_id, const std::string& launch_secret);
    SqlConsoleOperationResult touch_launch_session(const std::string& launch_id, const std::string& launch_secret);
    SqlConsoleOperationResult revoke_launch_session(const std::string& launch_id);
    SqlConsoleOperationResult revoke_temporary_launch_session(const SqlConsoleCleanupRequest& request);
    std::size_t sweep_expired_sessions();
    std::vector<SqlConsolePublicSession> list_sessions(uint64_t database_id = 0);
    SqlConsoleSessionManager& sessions();
    const SqlConsoleSessionManager& sessions() const;

private:
    std::string temporary_user_name(const std::string& launch_id) const;

    const database::DatabaseProvider* provider_ = nullptr;
    SqlConsoleSessionManager sessions_;
};

} // namespace containercp::sqlconsole

#endif // CONTAINERCP_SQLCONSOLE_DATABASE_SQL_CONSOLE_SERVICE_H
