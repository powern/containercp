#ifndef CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_SESSION_MANAGER_H
#define CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_SESSION_MANAGER_H

#include "sqlconsole/SqlConsoleSession.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace containercp::sqlconsole {

struct SqlConsoleSessionPolicy {
    std::chrono::seconds absolute_ttl = std::chrono::minutes(30);
    std::chrono::seconds idle_ttl = std::chrono::minutes(10);
    bool single_use_redemption = true;
};

struct SqlConsoleCreateRequest {
    uint64_t database_id = 0;
    uint64_t site_id = 0;
    std::string admin_username;
    std::string admin_role;
    std::string provider = "adminer";
};

struct SqlConsoleCreateResult {
    bool success = false;
    std::string code;
    std::string message;
    std::string launch_id;
    std::string launch_secret;
    SqlConsolePublicSession session;
};

struct SqlConsoleOperationResult {
    bool success = false;
    std::string code;
    std::string message;
    SqlConsolePublicSession session;
};

class SqlConsoleSessionManager {
public:
    using Clock = std::function<std::chrono::system_clock::time_point()>;

    explicit SqlConsoleSessionManager(SqlConsoleSessionPolicy policy = {});

    SqlConsoleCreateResult create(const SqlConsoleCreateRequest& request);
    const SqlConsoleSession* find(const std::string& launch_id);
    SqlConsoleOperationResult redeem(const std::string& launch_id, const std::string& launch_secret);
    SqlConsoleOperationResult touch(const std::string& launch_id, const std::string& launch_secret);
    SqlConsoleOperationResult revoke(const std::string& launch_id);
    SqlConsoleOperationResult attach_temporary_database_user(const std::string& launch_id,
                                                             const std::string& database_name,
                                                             const std::string& user_name,
                                                             const std::string& password);
    SqlConsoleOperationResult clear_temporary_database_user(const std::string& launch_id);
    std::size_t sweep_expired();
    std::vector<SqlConsolePublicSession> list_public(uint64_t database_id = 0);
    std::vector<SqlConsoleSession> list_internal() const;
    void set_clock_for_tests(Clock clock);

private:
    static std::string secret_digest(const std::string& launch_secret);
    bool expired(const SqlConsoleSession& session, std::chrono::system_clock::time_point now) const;
    bool expire_if_needed(SqlConsoleSession& session, std::chrono::system_clock::time_point now);
    SqlConsoleOperationResult failure(const std::string& code, const std::string& message, const SqlConsoleSession* session = nullptr) const;
    SqlConsoleOperationResult success(const std::string& code, const std::string& message, const SqlConsoleSession& session) const;

    SqlConsoleSessionPolicy policy_;
    Clock clock_;
    std::unordered_map<std::string, SqlConsoleSession> sessions_;
};

std::string sql_console_public_sessions_json(const std::vector<SqlConsolePublicSession>& sessions);

} // namespace containercp::sqlconsole

#endif // CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_SESSION_MANAGER_H
