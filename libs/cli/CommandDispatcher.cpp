#include "CommandDispatcher.h"
#include "core/Application.h"
#include "node/Node.h"
#include "operations/SiteCreateOperation.h"

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
        << "  node show <name> Show node details\n"
        << "  site list       List sites\n"
        << "  site create <owner> <domain> Create site\n"
        << "  site start <domain>     Start site stack\n"
        << "  site stop <domain>      Stop site stack\n"
        << "  site status <domain>    Show site status\n";
}

void print_version() {
    std::cout << "containercp " << VERSION << "\n";
}

int handle_site_create(const std::string& owner, const std::string& domain) {
    auto& services = containercp::core::Application::instance().services();

    auto* node = services.nodes().find("local");
    if (node == nullptr) {
        services.logger().error("no node available");
        return 1;
    }

    containercp::operations::SiteCreateOperation op(services.sites(), services.nodes(), services.hosting_provider());
    auto result = op.execute(owner, domain, *node);

    if (result.success) {
        containercp::core::Application::instance().save();
        std::cout << "Site created:\n" << domain << "\n";
    } else {
        std::cout << result.message << "\n";
    }
    return result.success ? 0 : 1;
}

int handle_site_start(const std::string& domain) {
    auto& services = containercp::core::Application::instance().services();
    auto* site = services.sites().find(domain);
    if (site == nullptr) {
        std::cout << "Site not found: " << domain << "\n";
        return 1;
    }
    auto result = services.hosting_provider().start_site(*site);
    if (result.success) {
        std::cout << "Site started:\n" << domain << "\n";
    } else {
        std::cout << result.message << "\n";
    }
    return result.success ? 0 : 1;
}

int handle_site_stop(const std::string& domain) {
    auto& services = containercp::core::Application::instance().services();
    auto* site = services.sites().find(domain);
    if (site == nullptr) {
        std::cout << "Site not found: " << domain << "\n";
        return 1;
    }
    auto result = services.hosting_provider().stop_site(*site);
    if (result.success) {
        std::cout << "Site stopped:\n" << domain << "\n";
    } else {
        std::cout << result.message << "\n";
    }
    return result.success ? 0 : 1;
}

int handle_site_status(const std::string& domain) {
    auto& services = containercp::core::Application::instance().services();
    auto* site = services.sites().find(domain);
    if (site == nullptr) {
        std::cout << "Site not found: " << domain << "\n";
        return 1;
    }
    auto result = services.hosting_provider().status(*site);
    if (!result.success) {
        std::cout << result.message << "\n";
    }
    return result.success ? 0 : 1;
}

} // anonymous namespace

namespace containercp::cli {

int CommandDispatcher::run(int argc, char* argv[]) {
    auto& services = core::Application::instance().services();

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
        for (const auto& node : services.nodes().list()) {
            std::cout << node.name << "\n";
        }
        return 0;
    }

    if (argc == 3 && arg1 == "config" && std::string(argv[2]) == "show") {
        auto& cfg = services.config();
        std::cout << "SourceRoot : " << cfg.source_root() << "\n"
                  << "ConfigRoot : " << cfg.config_root() << "\n"
                  << "DataRoot   : " << cfg.data_root() << "\n"
                  << "LogRoot    : " << cfg.log_root() << "\n";
        return 0;
    }

    if (argc == 4 && arg1 == "node" && std::string(argv[2]) == "show") {
        auto* node = services.nodes().find(argv[3]);
        if (node == nullptr) {
            services.logger().error("node \"" + std::string(argv[3]) + "\" not found");
            return 1;
        }
        std::cout << "Name: " << node->name << "\n"
                  << "Type: " << node->type << "\n";
        return 0;
    }

    if (argc == 5 && arg1 == "site" && std::string(argv[2]) == "create") {
        return handle_site_create(argv[3], argv[4]);
    }

    if (argc == 4 && arg1 == "site" && std::string(argv[2]) == "start") {
        return handle_site_start(argv[3]);
    }

    if (argc == 4 && arg1 == "site" && std::string(argv[2]) == "stop") {
        return handle_site_stop(argv[3]);
    }

    if (argc == 4 && arg1 == "site" && std::string(argv[2]) == "status") {
        return handle_site_status(argv[3]);
    }

    if (argc == 3 && arg1 == "site" && std::string(argv[2]) == "list") {
        auto& sites = services.sites().list();
        if (sites.empty()) {
            std::cout << "No sites.\n";
        } else {
            for (const auto& site : sites) {
                std::cout << site.domain << "\n";
            }
        }
        return 0;
    }

    services.logger().error("unknown command");
    std::cout << "\n";
    print_help();
    return 1;
}

} // namespace containercp::cli
