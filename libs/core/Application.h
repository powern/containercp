#ifndef CONTAINERCP_CORE_APPLICATION_H
#define CONTAINERCP_CORE_APPLICATION_H

#include "core/ServiceRegistry.h"

namespace containercp::core {

class Application {
public:
    static Application& instance();

    ServiceRegistry& services();

private:
    Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    ServiceRegistry registry_;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_APPLICATION_H
