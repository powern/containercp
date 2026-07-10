#include "Logger.h"

#include <iostream>
#include <mutex>

namespace containercp::logger {

Logger& Logger::instance() {
    static Logger log;
    return log;
}

void Logger::info(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[INFO] " << message << std::endl;
}

void Logger::info(const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[INFO] [" << category << "] " << message << std::endl;
}

void Logger::warning(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[WARN] " << message << std::endl;
}

void Logger::warning(const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[WARN] [" << category << "] " << message << std::endl;
}

void Logger::error(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << "[ERROR] " << message << std::endl;
}

void Logger::error(const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << "[ERROR] [" << category << "] " << message << std::endl;
}

void Logger::debug(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[DEBUG] " << message << std::endl;
}

void Logger::debug(const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[DEBUG] [" << category << "] " << message << std::endl;
}

} // namespace containercp::logger
