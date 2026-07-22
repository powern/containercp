#ifndef CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_SESSION_STORE_H
#define CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_SESSION_STORE_H

#include "sqlconsole/SqlConsoleSession.h"

#include <filesystem>
#include <string>
#include <vector>

namespace containercp::sqlconsole {

struct SqlConsoleSessionStoreResult {
    bool success = false;
    std::string code;
    std::string message;
};

class SqlConsoleSessionStore {
public:
    explicit SqlConsoleSessionStore(std::filesystem::path path);

    const std::filesystem::path& path() const;
    SqlConsoleSessionStoreResult save(const std::vector<SqlConsoleSession>& sessions) const;
    std::vector<SqlConsoleSession> load() const;

private:
    std::filesystem::path path_;
};

std::string sql_console_session_store_encode(const std::string& value);
std::string sql_console_session_store_decode(const std::string& value);

} // namespace containercp::sqlconsole

#endif // CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_SESSION_STORE_H
