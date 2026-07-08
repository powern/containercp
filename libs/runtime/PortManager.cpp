#include "PortManager.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>

namespace containercp::runtime {

PortManager::PortManager(uint16_t start_port, uint16_t end_port)
    : start_port_(start_port)
    , next_candidate_(start_port)
    , end_port_(end_port)
    , used_(end_port + 1, false)
{
}

uint16_t PortManager::allocate() {
    uint16_t port = next_candidate_;
    while (port <= end_port_ && used_[port]) {
        ++port;
    }
    if (port > end_port_) {
        port = start_port_;
        while (port <= end_port_ && used_[port]) {
            ++port;
        }
    }
    if (port > end_port_) {
        return 0;
    }
    used_[port] = true;
    next_candidate_ = port + 1;
    return port;
}

void PortManager::release(uint16_t port) {
    if (port < used_.size()) {
        used_[port] = false;
        if (port < next_candidate_) {
            next_candidate_ = port;
        }
    }
}

void PortManager::scan_existing_sites(const std::string& sites_dir) {
    DIR* dir = ::opendir(sites_dir.c_str());
    if (!dir) return;

    struct ::dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        if (std::strcmp(entry->d_name, ".") == 0) continue;
        if (std::strcmp(entry->d_name, "..") == 0) continue;

        std::string env_path = sites_dir + entry->d_name + "/.env";
        std::ifstream env_file(env_path);
        if (!env_file.is_open()) continue;

        std::string line;
        while (std::getline(env_file, line)) {
            if (line.find("NGINX_PORT=") == 0) {
                try {
                    uint16_t port = static_cast<uint16_t>(
                        std::stoul(line.substr(11)));
                    if (port < used_.size()) {
                        used_[port] = true;
                    }
                } catch (...) {
                }
                break;
            }
        }
    }
    ::closedir(dir);
}

bool PortManager::is_allocated(uint16_t port) const {
    return port < used_.size() && used_[port];
}

std::vector<uint16_t> PortManager::allocated_ports() const {
    std::vector<uint16_t> result;
    for (uint16_t i = start_port_; i < used_.size(); ++i) {
        if (used_[i]) {
            result.push_back(i);
        }
    }
    return result;
}

} // namespace containercp::runtime
