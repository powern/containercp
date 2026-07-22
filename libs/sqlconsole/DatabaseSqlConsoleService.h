#ifndef CONTAINERCP_SQLCONSOLE_DATABASE_SQL_CONSOLE_SERVICE_H
#define CONTAINERCP_SQLCONSOLE_DATABASE_SQL_CONSOLE_SERVICE_H

#include "sqlconsole/SqlConsoleSessionManager.h"

namespace containercp::sqlconsole {

class DatabaseSqlConsoleService {
public:
    explicit DatabaseSqlConsoleService(SqlConsoleSessionPolicy policy = {});

    SqlConsoleCreateResult create_launch_session(const SqlConsoleCreateRequest& request);
    SqlConsoleOperationResult redeem_launch_session(const std::string& launch_id, const std::string& launch_secret);
    SqlConsoleOperationResult touch_launch_session(const std::string& launch_id, const std::string& launch_secret);
    SqlConsoleOperationResult revoke_launch_session(const std::string& launch_id);
    std::size_t sweep_expired_sessions();
    std::vector<SqlConsolePublicSession> list_sessions(uint64_t database_id = 0);
    SqlConsoleSessionManager& sessions();
    const SqlConsoleSessionManager& sessions() const;

private:
    SqlConsoleSessionManager sessions_;
};

} // namespace containercp::sqlconsole

#endif // CONTAINERCP_SQLCONSOLE_DATABASE_SQL_CONSOLE_SERVICE_H
