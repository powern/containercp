#ifndef CONTAINERCP_DAEMON_COMMAND_PROTOCOL_H
#define CONTAINERCP_DAEMON_COMMAND_PROTOCOL_H

#include <string>
#include <vector>

namespace containercp::daemon {

struct Command {
    std::string name;
    std::vector<std::string> args;

    std::string encode() const;
    static Command decode(const std::string& line);
    static std::string success(const std::string& message);
    static std::string error(const std::string& message);
    static bool is_success(const std::string& response);
    static std::string message(const std::string& response);
};

} // namespace containercp::daemon

#endif // CONTAINERCP_DAEMON_COMMAND_PROTOCOL_H
