#include "CommandDispatcher.h"
#include "daemon/CommandProtocol.h"
#include "daemon/UnixSocketClient.h"
#include "utils/Validator.h"

#include <iostream>
#include <string>

#include <iostream>
#include <string>

namespace {

constexpr const char* VERSION = "0.1.0";
constexpr const char* SOCKET_PATH = "/srv/containercp/containercpd.sock";

std::string send_command(const std::string& cmd_line) {
    containercp::daemon::UnixSocketClient client(SOCKET_PATH);
    if (!client.connect()) {
        return "ERROR|Cannot connect to ContainerCP daemon. Is containercpd running?";
    }
    return client.send_and_receive(cmd_line);
}

int print_response(const std::string& response, bool print_success_msg = false) {
    if (containercp::daemon::Command::is_success(response)) {
        std::string msg = containercp::daemon::Command::message(response);
        if (!msg.empty()) {
            if (print_success_msg) {
                std::cout << msg << "\n";
            } else {
                std::cout << msg;
                if (msg.back() != '\n') std::cout << "\n";
            }
        }
        return 0;
    }
    std::cout << containercp::daemon::Command::message(response) << "\n";
    return 1;
}

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
        << "  user create <username> Create user\n"
        << "  user list           List users\n"
        << "  user show <username> Show user details\n"
        << "  user remove <username> Remove user\n"
        << "  domain list         List domains\n"
        << "  domain show <fqdn>  Show domain details\n"
        << "  domain remove <fqdn> Remove domain\n"
        << "  php list            List PHP versions\n"
        << "  php show <version>  Show PHP version details\n"
        << "  php default         Show default PHP version\n"
        << "  database list       List databases\n"
        << "  database show <name> Show database details\n"
        << "  database remove <name> Remove database\n"
        << "  backup list            List backups\n"
        << "  ssl list               List SSL certificates\n"
        << "  ssl show <domain>      Show SSL certificate\n"
        << "  mail list              List mail domains\n"
        << "  proxy list          List proxy configs\n"
        << "  proxy show <domain> Show proxy config\n"
        << "  access user list                       List access users\n"
        << "  access grant list                      List grants\n"
        << "  site list       List sites\n"
        << "  site create <owner> <domain> Create site\n"
        << "  site remove <domain>     Remove site\n"
        << "  site start <domain>     Start site stack\n"
        << "  site stop <domain>      Stop site stack\n"
        << "  site status <domain>    Show site status\n"
        << "  template list           List templates\n"
        << "  template show <name>    Show template details\n"
        << "  template default        Show default template\n";
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
        return print_response(send_command("node-list"));
    }

    if (argc == 4 && arg1 == "node" && std::string(argv[2]) == "show") {
        return print_response(send_command("node-show|" + std::string(argv[3])));
    }

    if (argc == 3 && arg1 == "config" && std::string(argv[2]) == "show") {
        return print_response(send_command("config-show"));
    }

    if (argc == 4 && arg1 == "user" && std::string(argv[2]) == "create") {
        const std::string username = argv[3];
        std::string msg = utils::Validator::validate_username(username);
        if (!msg.empty()) {
            std::cout << msg << "\n";
            return 1;
        }
        return print_response(send_command("user-create|" + username));
    }

    if (argc == 3 && arg1 == "user" && std::string(argv[2]) == "list") {
        return print_response(send_command("user-list"));
    }

    if (argc == 4 && arg1 == "user" && std::string(argv[2]) == "show") {
        return print_response(send_command("user-show|" + std::string(argv[3])));
    }

    if (argc == 4 && arg1 == "user" && std::string(argv[2]) == "remove") {
        return print_response(send_command("user-remove|" + std::string(argv[3])));
    }

    if (argc == 3 && arg1 == "domain" && std::string(argv[2]) == "list") {
        return print_response(send_command("domain-list"));
    }

    if (argc == 4 && arg1 == "domain" && std::string(argv[2]) == "show") {
        return print_response(send_command("domain-show|" + std::string(argv[3])));
    }

    if (argc == 4 && arg1 == "domain" && std::string(argv[2]) == "remove") {
        return print_response(send_command("domain-remove|" + std::string(argv[3])));
    }

    if (argc == 3 && arg1 == "php" && std::string(argv[2]) == "list") {
        return print_response(send_command("php-list"));
    }

    if (argc == 4 && arg1 == "php" && std::string(argv[2]) == "show") {
        return print_response(send_command("php-show|" + std::string(argv[3])));
    }

    if (argc == 3 && arg1 == "php" && std::string(argv[2]) == "default") {
        return print_response(send_command("php-default"));
    }

    if (argc == 3 && arg1 == "site" && std::string(argv[2]) == "list") {
        return print_response(send_command("site-list"));
    }

    if (argc == 3 && arg1 == "database" && std::string(argv[2]) == "list") {
        return print_response(send_command("database-list"));
    }

    if (argc == 4 && arg1 == "database" && std::string(argv[2]) == "show") {
        return print_response(send_command("database-show|" + std::string(argv[3])));
    }

    if (argc == 4 && arg1 == "database" && std::string(argv[2]) == "remove") {
        return print_response(send_command("database-remove|" + std::string(argv[3])));
    }

    if (argc == 3 && arg1 == "backup" && std::string(argv[2]) == "list") {
        return print_response(send_command("backup-list"));
    }

    if (argc == 3 && arg1 == "ssl" && std::string(argv[2]) == "list") {
        return print_response(send_command("ssl-list"));
    }

    if (argc == 4 && arg1 == "ssl" && std::string(argv[2]) == "show") {
        return print_response(send_command("ssl-show|" + std::string(argv[3])));
    }

    if (argc == 3 && arg1 == "mail" && std::string(argv[2]) == "list") {
        return print_response(send_command("mail-list"));
    }

    if (argc == 3 && arg1 == "proxy" && std::string(argv[2]) == "list") {
        return print_response(send_command("proxy-list"));
    }

    if (argc == 4 && arg1 == "access" && std::string(argv[2]) == "user" && std::string(argv[3]) == "list") {
        return print_response(send_command("access-user-list"));
    }

    if (argc == 4 && arg1 == "access" && std::string(argv[2]) == "grant" && std::string(argv[3]) == "list") {
        return print_response(send_command("access-grant-list"));
    }

    if (argc == 3 && arg1 == "template" && std::string(argv[2]) == "list") {
        return print_response(send_command("template-list"));
    }

    if (argc == 4 && arg1 == "template" && std::string(argv[2]) == "show") {
        return print_response(send_command("template-show|" + std::string(argv[3])));
    }

    if (argc == 3 && arg1 == "template" && std::string(argv[2]) == "default") {
        return print_response(send_command("template-default"));
    }

    std::cout << "Error: unknown command\n\n";
    print_help();
    return 1;
}

} // namespace containercp::cli
