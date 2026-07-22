#ifndef CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_SESSION_H
#define CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_SESSION_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace containercp::sqlconsole {

enum class SqlConsoleSessionStatus {
    Created,
    Redeemed,
    Expired,
    Revoked,
};

struct SqlConsoleSession {
    std::string launch_id;
    std::string secret_digest;
    uint64_t database_id = 0;
    uint64_t site_id = 0;
    std::string admin_username;
    std::string admin_role;
    std::string provider = "adminer";
    std::string database_name;
    std::string temporary_user_name;
    std::string temporary_user_password;
    SqlConsoleSessionStatus status = SqlConsoleSessionStatus::Created;
    std::chrono::system_clock::time_point created_at{};
    std::chrono::system_clock::time_point expires_at{};
    std::chrono::system_clock::time_point idle_expires_at{};
    std::chrono::system_clock::time_point last_seen_at{};
    std::optional<std::chrono::system_clock::time_point> redeemed_at;
    std::optional<std::chrono::system_clock::time_point> revoked_at;
};

struct SqlConsolePublicSession {
    std::string launch_id;
    uint64_t database_id = 0;
    uint64_t site_id = 0;
    std::string admin_username;
    std::string admin_role;
    std::string provider;
    std::string status;
    std::string created_at;
    std::string redeemed_at;
    std::string expires_at;
    std::string idle_expires_at;
    std::string revoked_at;
};

std::string sql_console_session_status_to_string(SqlConsoleSessionStatus status);
SqlConsoleSessionStatus sql_console_session_status_from_string(const std::string& value);
std::string sql_console_time_to_iso(std::chrono::system_clock::time_point value);
SqlConsolePublicSession sql_console_public_session(const SqlConsoleSession& session);
std::string sql_console_public_session_json(const SqlConsolePublicSession& session);

} // namespace containercp::sqlconsole

#endif // CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_SESSION_H
