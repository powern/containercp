#include "sqlconsole/DatabaseSqlConsoleService.h"

namespace containercp::sqlconsole {

DatabaseSqlConsoleService::DatabaseSqlConsoleService(SqlConsoleSessionPolicy policy)
    : sessions_(policy)
{
}

SqlConsoleCreateResult DatabaseSqlConsoleService::create_launch_session(const SqlConsoleCreateRequest& request) {
    return sessions_.create(request);
}

SqlConsoleOperationResult DatabaseSqlConsoleService::redeem_launch_session(const std::string& launch_id, const std::string& launch_secret) {
    return sessions_.redeem(launch_id, launch_secret);
}

SqlConsoleOperationResult DatabaseSqlConsoleService::touch_launch_session(const std::string& launch_id, const std::string& launch_secret) {
    return sessions_.touch(launch_id, launch_secret);
}

SqlConsoleOperationResult DatabaseSqlConsoleService::revoke_launch_session(const std::string& launch_id) {
    return sessions_.revoke(launch_id);
}

std::size_t DatabaseSqlConsoleService::sweep_expired_sessions() {
    return sessions_.sweep_expired();
}

std::vector<SqlConsolePublicSession> DatabaseSqlConsoleService::list_sessions(uint64_t database_id) {
    return sessions_.list_public(database_id);
}

SqlConsoleSessionManager& DatabaseSqlConsoleService::sessions() {
    return sessions_;
}

const SqlConsoleSessionManager& DatabaseSqlConsoleService::sessions() const {
    return sessions_;
}

} // namespace containercp::sqlconsole
