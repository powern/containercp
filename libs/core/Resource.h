#ifndef CONTAINERCP_CORE_RESOURCE_H
#define CONTAINERCP_CORE_RESOURCE_H

#include <cstdint>
#include <string>

namespace containercp::core {

struct Resource {
    uint64_t id = 0;
    std::string name;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_RESOURCE_H
