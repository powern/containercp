#ifndef CONTAINERCP_DAEMON_DAEMON_APP_H
#define CONTAINERCP_DAEMON_DAEMON_APP_H

#include "core/ServiceRegistry.h"

#include <string>

namespace containercp::daemon {

class DaemonApp {
public:
    explicit DaemonApp(core::ServiceRegistry& services);

    std::string handle_command(const std::string& command_line);

private:
    core::ServiceRegistry& services_;
};

} // namespace containercp::daemon

#endif // CONTAINERCP_DAEMON_DAEMON_APP_H
