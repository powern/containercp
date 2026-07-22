#ifndef CONTAINERCP_SQLCONSOLE_ADMINER_SQL_CONSOLE_PROVIDER_H
#define CONTAINERCP_SQLCONSOLE_ADMINER_SQL_CONSOLE_PROVIDER_H

#include "runtime/CommandExecutor.h"
#include "sqlconsole/SqlConsoleProvider.h"

#include <vector>

namespace containercp::sqlconsole {

class SqlConsoleRuntimeRunner {
public:
    virtual ~SqlConsoleRuntimeRunner() = default;
    virtual runtime::CommandResult run(const std::vector<std::string>& args) const = 0;
};

class CommandExecutorSqlConsoleRuntimeRunner : public SqlConsoleRuntimeRunner {
public:
    explicit CommandExecutorSqlConsoleRuntimeRunner(const runtime::CommandExecutor& executor);
    runtime::CommandResult run(const std::vector<std::string>& args) const override;

private:
    const runtime::CommandExecutor& executor_;
};

class AdminerSqlConsoleProvider : public SqlConsoleProvider {
public:
    explicit AdminerSqlConsoleProvider(const SqlConsoleRuntimeRunner& runner, std::string image = "adminer:latest");

    std::string name() const override;
    SqlConsoleProviderResult start(const SqlConsoleProviderLaunchRequest& request) const override;
    SqlConsoleProviderResult stop(const std::string& launch_id) const override;
    SqlConsoleProviderResult status(const std::string& launch_id) const override;

    std::string container_name(const std::string& launch_id) const;
    std::string site_network_name(const SqlConsoleProviderLaunchRequest& request) const;
    std::vector<std::string> start_args(const SqlConsoleProviderLaunchRequest& request) const;

private:
    const SqlConsoleRuntimeRunner& runner_;
    std::string image_;
};

} // namespace containercp::sqlconsole

#endif // CONTAINERCP_SQLCONSOLE_ADMINER_SQL_CONSOLE_PROVIDER_H
