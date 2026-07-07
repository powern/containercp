#ifndef CONTAINERCP_CORE_APPLICATION_H
#define CONTAINERCP_CORE_APPLICATION_H

#include "config/Config.h"
#include "logger/Logger.h"

namespace containercp::core {

class Application {
public:
    static Application& instance();

    config::Config& config();
    logger::Logger& logger();

private:
    Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    config::Config& config_;
    logger::Logger& logger_;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_APPLICATION_H
