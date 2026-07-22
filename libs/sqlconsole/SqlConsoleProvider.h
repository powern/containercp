#ifndef CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_PROVIDER_H
#define CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_PROVIDER_H

#include <cstdint>
#include <string>

namespace containercp::sqlconsole {

struct SqlConsoleProviderResult {
    bool success = false;
    std::string code;
    std::string message;
    std::string container_name;
    std::string upstream;
};

struct SqlConsoleProviderLaunchRequest {
    std::string launch_id;
    uint64_t site_id = 0;
    uint64_t database_id = 0;
    std::string site_domain;
    std::string site_root;
    std::string provider = "adminer";
    std::string adminer_sso_plugin_path;
    std::string internal_token_path;
};

class SqlConsoleProvider {
public:
    virtual ~SqlConsoleProvider() = default;

    virtual std::string name() const = 0;
    virtual SqlConsoleProviderResult start(const SqlConsoleProviderLaunchRequest& request) const = 0;
    virtual SqlConsoleProviderResult stop(const std::string& launch_id) const = 0;
    virtual SqlConsoleProviderResult status(const std::string& launch_id) const = 0;
};

} // namespace containercp::sqlconsole

#endif // CONTAINERCP_SQLCONSOLE_SQL_CONSOLE_PROVIDER_H
