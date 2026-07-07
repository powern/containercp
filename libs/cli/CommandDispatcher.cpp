#include "CommandDispatcher.h"
#include "node/Node.h"

#include <iostream>
#include <string>

namespace {

constexpr const char* VERSION = "0.1.0";

void print_help() {
    std::cout
        << "ContainerCP CLI v" << VERSION << "\n\n"
        << "Usage:\n"
        << "  containercp [command]\n\n"
        << "Commands:\n"
        << "  --help       Show help\n"
        << "  --version    Show version\n"
        << "  node list    List nodes\n";
}

void print_version() {
    std::cout << "containercp " << VERSION << "\n";
}

} // anonymous namespace

namespace containercp::cli {

int CommandDispatcher::run(int argc, char* argv[]) {
    if (argc == 1) {
        print_help();
        return 0;
    }

    const std::string arg1 = argv[1];

    if (arg1 == "--help" || arg1 == "-h") {
        print_help();
        return 0;
    }

    if (arg1 == "--version" || arg1 == "-v") {
        print_version();
        return 0;
    }

    if (argc == 3 && arg1 == "node" && std::string(argv[2]) == "list") {
        auto node = node::get_default_node();
        std::cout << node.name << "\n";
        return 0;
    }

    std::cerr << "Error: unknown command\n\n";
    print_help();
    return 1;
}

} // namespace containercp::cli
