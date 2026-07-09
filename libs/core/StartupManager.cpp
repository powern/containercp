#include "StartupManager.h"

#include <fstream>

namespace containercp::core {

static std::string flag_path(const std::string& data_root) {
    return data_root + "/setup_completed";
}

bool StartupManager::is_setup_completed(const std::string& data_root) {
    std::ifstream f(flag_path(data_root));
    bool completed = false;
    if (f.is_open()) {
        std::string line;
        std::getline(f, line);
        completed = (line == "1");
    }
    return completed;
}

void StartupManager::mark_setup_completed(const std::string& data_root) {
    std::ofstream f(flag_path(data_root));
    if (f.is_open()) {
        f << "1\n";
    }
}

void StartupManager::mark_setup_incomplete(const std::string& data_root) {
    std::ofstream f(flag_path(data_root));
    if (f.is_open()) {
        f << "0\n";
    }
}

bool StartupManager::needs_bootstrap(const std::string& data_root, const std::string& hostname) {
    if (hostname.empty()) return true;
    if (!is_setup_completed(data_root)) return true;
    return false;
}

} // namespace containercp::core
