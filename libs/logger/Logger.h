#ifndef CONTAINERCP_LOGGER_LOGGER_H
#define CONTAINERCP_LOGGER_LOGGER_H

#include <string>

namespace containercp::logger {

class Logger {
public:
    static Logger& instance();

    void info(const std::string& message);
    void info(const std::string& category, const std::string& message);

    void warning(const std::string& message);
    void warning(const std::string& category, const std::string& message);

    void error(const std::string& message);
    void error(const std::string& category, const std::string& message);

    void debug(const std::string& message);
    void debug(const std::string& category, const std::string& message);

private:
    Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
};

} // namespace containercp::logger

#endif // CONTAINERCP_LOGGER_LOGGER_H
