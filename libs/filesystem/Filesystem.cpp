#include "Filesystem.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace containercp::filesystem {

bool Filesystem::create_directory(const std::string& path) {
    return std::filesystem::create_directories(path);
}

bool Filesystem::remove_directory(const std::string& path) {
    return std::filesystem::remove_all(path) > 0;
}

bool Filesystem::exists(const std::string& path) {
    return std::filesystem::exists(path);
}

bool Filesystem::create_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    return file.good();
}

std::string Filesystem::read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace containercp::filesystem
