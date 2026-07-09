#ifndef CONTAINERCP_RUNTIME_RUNTIME_ACTION_EXECUTOR_H
#define CONTAINERCP_RUNTIME_RUNTIME_ACTION_EXECUTOR_H

#include "core/OperationResult.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::runtime {

// Status of a single compose service container.
struct ContainerStatus {
    std::string name;
    std::string status;   // Running, Stopped, Unhealthy, Starting, Unknown, Error
    std::string health;   // healthy, unhealthy, starting, (empty)
};

// Global runtime action executor.
//
// Knows HOW to execute Docker Compose actions (restart, stop, start, etc.)
// and HOW to inspect service status against a specific compose project
// directory.  Consumers (SiteRuntimeManager, future DatabaseManager,
// RedisManager, etc.) decide WHAT services to act on and pass the compose
// directory + action to this executor.
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

    // List all services defined in a compose project.
    // Returns empty vector on error.
    std::vector<std::string> list_services(const std::string& compose_dir);

    // Convenience: docker compose restart one or more services.
    core::OperationResult restart_services(const std::string& compose_dir,
                                            const std::vector<std::string>& services);

    // Inspect status of a single compose service.
    // Returns ContainerStatus with status, health, and container name.
    ContainerStatus service_status(const std::string& compose_dir,
                                    const std::string& service) const;

private:
    logger::Logger& logger_;
    CommandExecutor executor_;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_RUNTIME_ACTION_EXECUTOR_H
