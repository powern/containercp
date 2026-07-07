#ifndef CONTAINERCP_LOGGER_LOGGER_H
#define CONTAINERCP_LOGGER_LOGGER_H

#include <string>

namespace containercp::logger {

class Logger {
public:
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);
    static void debug(const std::string& message);

private:
    Logger() = default;
};

} // namespace containercp::logger

#endif // CONTAINERCP_LOGGER_LOGGER_H
