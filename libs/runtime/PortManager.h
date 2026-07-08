#ifndef CONTAINERCP_RUNTIME_PORT_MANAGER_H
#define CONTAINERCP_RUNTIME_PORT_MANAGER_H

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::runtime {

class PortManager {
public:
    PortManager(uint16_t start_port = 9000, uint16_t end_port = 65535);

    uint16_t allocate();
    void release(uint16_t port);
    void scan_existing_sites(const std::string& sites_dir);
    bool is_allocated(uint16_t port) const;
    std::vector<uint16_t> allocated_ports() const;

private:
    uint16_t start_port_;
    uint16_t next_candidate_;
    uint16_t end_port_;
    std::vector<bool> used_;
};

} // namespace containercp::runtime
#endif
