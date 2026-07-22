#ifndef CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_AUDIT_H
#define CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_AUDIT_H

#include <cstdint>
#include <string>

namespace containercp::sqlconsole {

struct SqlConsoleAuditEvent {
    enum class Level { Info, Warning, Error };

    std::string operation;
    std::string stage;
    std::string result;
    std::string error_code;
    std::string launch_id;
    uint64_t database_id = 0;
    uint64_t site_id = 0;
    std::string admin_username;
    std::string provider;
    std::string status;
    Level level = Level::Info;
};

class SqlConsoleAuditLogger {
public:
    static std::string format(const SqlConsoleAuditEvent& event);
    static void log(const SqlConsoleAuditEvent& event);
};

} // namespace containercp::sqlconsole

#endif // CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_AUDIT_H
