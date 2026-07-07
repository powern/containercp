#include "CommandDispatcher.h"
#include "config/Config.h"
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
        << "  config show     Show configuration\n"
        << "  node list       List nodes\n"
        << "  node show <name> Show node details\n";
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

    if (argc == 3 && arg1 == "config" && std::string(argv[2]) == "show") {
        auto& cfg = config::Config::instance();
        std::cout << "SourceRoot : " << cfg.source_root() << "\n"
                  << "ConfigRoot : " << cfg.config_root() << "\n"
                  << "DataRoot   : " << cfg.data_root() << "\n"
                  << "LogRoot    : " << cfg.log_root() << "\n";
        return 0;
    }

    if (argc == 4 && arg1 == "node" && std::string(argv[2]) == "show") {
        auto node = node::find_node(argv[3]);
        if (node.name.empty()) {
            std::cerr << "Error: node \"" << argv[3] << "\" not found\n";
            return 1;
        }
        std::cout << "Name: " << node.name << "\n"
                  << "Type: " << node.type << "\n";
        return 0;
    }

    std::cerr << "Error: unknown command\n\n";
    print_help();
    return 1;
}

} // namespace containercp::cli
