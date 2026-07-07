#include "cli/CommandDispatcher.h"

int main(int argc, char* argv[]) {
    return containercp::cli::CommandDispatcher::run(argc, argv);
}
