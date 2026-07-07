#include "Application.h"

namespace containercp::core {

Application& Application::instance() {
    static Application app;
    return app;
}

Application::Application()
    : config_(config::Config::instance())
    , logger_(logger::Logger::instance())
{
}

config::Config& Application::config() {
    return config_;
}

logger::Logger& Application::logger() {
    return logger_;
}

} // namespace containercp::core
