#ifndef CONTAINERCP_RUNTIME_RUNTIME_ACTION_EXECUTOR_H
#define CONTAINERCP_RUNTIME_RUNTIME_ACTION_EXECUTOR_H

#include "core/OperationResult.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <string>
#include <vector>

namespace containercp::runtime {

// Global runtime action executor.
//
// Knows HOW to execute Docker Compose actions (restart, stop, start, etc.)
// against a specific compose project directory.  Consumers (SiteRuntimeManager,
// future DatabaseManager, RedisManager, etc.) decide WHAT services to act on
// and pass the compose directory + action to this executor.
//
// Uses the safe CommandExecutor (fork/execvp with poll) — never std::system.
class RuntimeActionExecutor {
public:
    RuntimeActionExecutor(logger::Logger& logger);

    // Check that docker and docker compose are available.
    core::OperationResult check_available();

    // Run docker compose <subcommand> [services...] inside compose_dir.
    //   compose_dir   — full path to the site's compose project directory
    //   subcommand    — compose subcommand e.g. "restart", "stop", "up"
    //   services      — one or more compose service names (empty = all)
    core::OperationResult compose_action(const std::string& compose_dir,
                                          const std::string& subcommand,
                                          const std::vector<std::string>& services);

    // Convenience: docker compose restart one or more services.
    core::OperationResult restart_services(const std::string& compose_dir,
                                            const std::vector<std::string>& services);

private:
    logger::Logger& logger_;
    CommandExecutor executor_;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_RUNTIME_ACTION_EXECUTOR_H
