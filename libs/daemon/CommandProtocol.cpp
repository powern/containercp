#include "CommandProtocol.h"

#include <sstream>

namespace containercp::daemon {

std::string Command::encode() const {
    std::string result = name;
    for (const auto& arg : args) {
        result += "|" + arg;
    }
    return result;
}

Command Command::decode(const std::string& line) {
    Command cmd;
    std::istringstream ss(line);
    std::string token;
    if (std::getline(ss, token, '|')) {
        cmd.name = token;
    }
    while (std::getline(ss, token, '|')) {
        cmd.args.push_back(token);
    }
    return cmd;
}

std::string Command::success(const std::string& message) {
    return "SUCCESS|" + message;
}

std::string Command::error(const std::string& message) {
    return "ERROR|" + message;
}

bool Command::is_success(const std::string& response) {
    return response.compare(0, 7, "SUCCESS") == 0;
}

std::string Command::message(const std::string& response) {
    auto pos = response.find('|');
    if (pos != std::string::npos) {
        return response.substr(pos + 1);
    }
    return response;
}

} // namespace containercp::daemon
