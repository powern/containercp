#include "Application.h"

namespace containercp::core {

Application& Application::instance() {
    static Application app;
    return app;
}

Application::Application() = default;

ServiceRegistry& Application::services() {
    return registry_;
}

} // namespace containercp::core
