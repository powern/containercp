#include "Logger.h"

#include <iostream>

namespace containercp::logger {

Logger& Logger::instance() {
    static Logger log;
    return log;
}

void Logger::info(const std::string& message) {
    std::cout << "[INFO] " << message << "\n";
}

void Logger::warning(const std::string& message) {
    std::cout << "[WARN] " << message << "\n";
}

void Logger::error(const std::string& message) {
    std::cerr << "[ERROR] " << message << "\n";
}

void Logger::debug(const std::string& message) {
    std::cout << "[DEBUG] " << message << "\n";
}

} // namespace containercp::logger
