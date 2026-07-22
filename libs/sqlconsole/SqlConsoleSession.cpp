#include "sqlconsole/SqlConsoleSession.h"

#include "api/JsonFormatter.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace containercp::sqlconsole {

std::string sql_console_session_status_to_string(SqlConsoleSessionStatus status) {
    switch (status) {
    case SqlConsoleSessionStatus::Created:
        return "created";
    case SqlConsoleSessionStatus::Redeemed:
        return "redeemed";
    case SqlConsoleSessionStatus::Expired:
        return "expired";
    case SqlConsoleSessionStatus::Revoked:
        return "revoked";
    }
    return "unknown";
}

SqlConsoleSessionStatus sql_console_session_status_from_string(const std::string& value) {
    if (value == "redeemed") return SqlConsoleSessionStatus::Redeemed;
    if (value == "expired") return SqlConsoleSessionStatus::Expired;
    if (value == "revoked") return SqlConsoleSessionStatus::Revoked;
    return SqlConsoleSessionStatus::Created;
}

std::string sql_console_time_to_iso(std::chrono::system_clock::time_point value) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(value);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

SqlConsolePublicSession sql_console_public_session(const SqlConsoleSession& session) {
    SqlConsolePublicSession public_session;
    public_session.launch_id = session.launch_id;
    public_session.database_id = session.database_id;
    public_session.site_id = session.site_id;
    public_session.admin_username = session.admin_username;
    public_session.admin_role = session.admin_role;
    public_session.provider = session.provider;
    public_session.status = sql_console_session_status_to_string(session.status);
    public_session.created_at = sql_console_time_to_iso(session.created_at);
    public_session.redeemed_at = session.redeemed_at ? sql_console_time_to_iso(*session.redeemed_at) : "";
    public_session.expires_at = sql_console_time_to_iso(session.expires_at);
    public_session.idle_expires_at = sql_console_time_to_iso(session.idle_expires_at);
    public_session.revoked_at = session.revoked_at ? sql_console_time_to_iso(*session.revoked_at) : "";
    return public_session;
}

std::string sql_console_public_session_json(const SqlConsolePublicSession& session) {
    std::ostringstream json;
    json << "{\"launch_id\":\"" << api::JsonFormatter::escape(session.launch_id)
         << "\",\"database_id\":" << session.database_id
         << ",\"site_id\":" << session.site_id
         << ",\"admin_username\":\"" << api::JsonFormatter::escape(session.admin_username)
         << "\",\"admin_role\":\"" << api::JsonFormatter::escape(session.admin_role)
         << "\",\"provider\":\"" << api::JsonFormatter::escape(session.provider)
         << "\",\"status\":\"" << api::JsonFormatter::escape(session.status)
         << "\",\"created_at\":\"" << api::JsonFormatter::escape(session.created_at)
         << "\",\"redeemed_at\":\"" << api::JsonFormatter::escape(session.redeemed_at)
         << "\",\"expires_at\":\"" << api::JsonFormatter::escape(session.expires_at)
         << "\",\"idle_expires_at\":\"" << api::JsonFormatter::escape(session.idle_expires_at)
         << "\",\"revoked_at\":\"" << api::JsonFormatter::escape(session.revoked_at)
         << "\"}";
    return json.str();
}

} // namespace containercp::sqlconsole
