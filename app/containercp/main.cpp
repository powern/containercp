#include "core/Application.h"
#include "cli/CommandDispatcher.h"

int main(int argc, char* argv[]) {
    containercp::core::Application::instance();
    return containercp::cli::CommandDispatcher::run(argc, argv);
}
