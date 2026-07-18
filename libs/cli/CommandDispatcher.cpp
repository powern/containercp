#include "CommandDispatcher.h"
#include "core/Version.h"
#include "daemon/CommandProtocol.h"
#include "daemon/UnixSocketClient.h"
#include "utils/Validator.h"

#include <iostream>
#include <string>

namespace {

constexpr const char* VERSION = containercp::core::VERSION;
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
        << "  backup create <domain> Create backup\n"
        << "  backup list            List backups\n"
        << "  backup show <id>       Show backup details\n"
        << "  backup remove <id>     Remove backup\n"
        << "  backup restore <id>    Restore backup\n"
        << "  ssl list               List SSL certificates\n"
        << "  ssl show <domain>      Show SSL certificate\n"
        << "  mail list              List mail domains\n"
        << "  proxy list          List proxy configs\n"
        << "  proxy show <domain> Show proxy config\n"
        << "  access user list                       List access users\n"
        << "  access grant list                      List grants\n"
        << "  site list       List sites\n"
        << "  site create <owner> <domain> Create site\n"
        << "  site remove <domain> [--force] Remove site\n"
        << "  site start <domain>     Start site stack\n"
        << "  site stop <domain>      Stop site stack\n"
        << "  site status <domain>    Show site status\n"
        << "  profile list            List profiles\n"
        << "  profile show <name>     Show profile details\n"
        << "  profile default         Show default profile\n"
        << "  template list           List templates\n"
        << "  template show <name>    Show template details\n"
        << "  template default        Show default template\n"
        << "  template path           Show template directory\n"
        << "  template validate <name> Validate template\n"
        << "  template reload         Reload templates from disk\n"
        << "  auth debug              Show auth user diagnostics\n"
        << "  storage migrate-to-sqlite  Migrate TXT storage to SQLite\n"
        << "    --source <dir>         Source TXT database directory\n"
        << "    --database <path>      Target SQLite database path\n"
        << "    --archive-root <dir>   Archive directory for legacy TXT data\n"
        << "    --source-version <ver> Source version (e.g. v0.6.0)\n"
        << "    --target-version <ver> Target version (e.g. v0.7.0)\n"
        << "    --confirm              Confirm migration intent\n";
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

    if (argc == 5 && arg1 == "site" && std::string(argv[2]) == "create") {
        return print_response(send_command("site-create|" + std::string(argv[3]) + "|" + std::string(argv[4])));
    }

    if (argc == 6 && arg1 == "site" && std::string(argv[2]) == "create" && std::string(argv[5]) == "--dry-run") {
        return print_response(send_command("site-create-dry-run|" + std::string(argv[3]) + "|" + std::string(argv[4])));
    }

    if (argc == 3 && arg1 == "site" && std::string(argv[2]) == "list") {
        return print_response(send_command("site-list"));
    }

    if (argc == 4 && arg1 == "site" && std::string(argv[2]) == "start") {
        return print_response(send_command("site-start|" + std::string(argv[3])));
    }

    if (argc == 4 && arg1 == "site" && std::string(argv[2]) == "stop") {
        return print_response(send_command("site-stop|" + std::string(argv[3])));
    }

    if (argc == 4 && arg1 == "site" && std::string(argv[2]) == "status") {
        return print_response(send_command("site-status|" + std::string(argv[3])));
    }

    if (argc == 4 && arg1 == "site" && std::string(argv[2]) == "remove") {
        return print_response(send_command("site-remove|" + std::string(argv[3])));
    }

    if (argc == 5 && arg1 == "site" && std::string(argv[2]) == "remove" && std::string(argv[4]) == "--force") {
        return print_response(send_command("site-remove-force|" + std::string(argv[3])));
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

    if (argc == 4 && arg1 == "backup" && std::string(argv[2]) == "create") {
        return print_response(send_command("backup-create|" + std::string(argv[3])));
    }

    if (argc == 3 && arg1 == "backup" && std::string(argv[2]) == "list") {
        return print_response(send_command("backup-list"));
    }

    if (argc == 4 && arg1 == "backup" && std::string(argv[2]) == "show") {
        return print_response(send_command("backup-show|" + std::string(argv[3])));
    }

    if (argc == 4 && arg1 == "backup" && std::string(argv[2]) == "remove") {
        return print_response(send_command("backup-remove|" + std::string(argv[3])));
    }

    if (argc == 4 && arg1 == "backup" && std::string(argv[2]) == "restore") {
        return print_response(send_command("backup-restore|" + std::string(argv[3])));
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

    if (argc == 3 && arg1 == "profile" && std::string(argv[2]) == "list") {
        return print_response(send_command("profile-list"));
    }

    if (argc == 4 && arg1 == "profile" && std::string(argv[2]) == "show") {
        return print_response(send_command("profile-show|" + std::string(argv[3])));
    }

    if (argc == 3 && arg1 == "profile" && std::string(argv[2]) == "default") {
        return print_response(send_command("profile-default"));
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

    if (argc == 3 && arg1 == "template" && std::string(argv[2]) == "path") {
        return print_response(send_command("template-path"));
    }

    if (argc == 3 && arg1 == "template" && std::string(argv[2]) == "reload") {
        return print_response(send_command("template-reload"));
    }

    if (argc == 4 && arg1 == "template" && std::string(argv[2]) == "validate") {
        return print_response(send_command("template-validate|" + std::string(argv[3])));
    }

    if (argc == 3 && arg1 == "auth" && std::string(argv[2]) == "debug") {
        return print_response(send_command("auth-debug"));
    }

    if (arg1 == "migrate-vesta-site") {
        std::string backup, domain, owner, database;
        bool dry_run = false, execute = false, import_files = false, import_sql = false, is_upgrade = false;
        bool skip_db = false, keep_staging = false, enable_mail = false;
        bool has_error = false;

        for (int i = 2; i < argc; ++i) {
            std::string arg(argv[i]);
            if (arg == "--backup" && i + 1 < argc) backup = argv[++i];
            else if (arg == "--domain" && i + 1 < argc) domain = argv[++i];
            else if (arg == "--owner" && i + 1 < argc) owner = argv[++i];
            else if (arg == "--database" && i + 1 < argc) database = argv[++i];
            else if (arg == "--dry-run") dry_run = true;
            else if (arg == "--execute") execute = true;
            else if (arg == "--import-files") import_files = true;
            else if (arg == "--import-sql") import_sql = true;
            else if (arg == "--upgrade") is_upgrade = true;
            else if (arg == "--enable-mail") enable_mail = true;
            else if (arg == "--skip-db") skip_db = true;
            else if (arg == "--keep-staging") keep_staging = true;
            else has_error = true;
        }

        // Detect conflicting modes
        int modes = (dry_run ? 1 : 0) + (execute ? 1 : 0) + (import_files ? 1 : 0) + (import_sql ? 1 : 0);
        if (modes > 1 || (is_upgrade && modes > 0)) {
            std::cout << "Error: conflicting modes. Choose one of: --dry-run, --execute, --import-files, --import-sql, --upgrade\n";
            return 1;
        }

        if (has_error || domain.empty() || owner.empty() || (backup.empty() && !is_upgrade)) {
            std::cout << "Usage: containercp migrate-vesta-site\n"
                      << "  --backup <file>     Path to myVestaCP backup archive\n"
                      << "  --domain <domain>   Domain to restore/upgrade\n"
                      << "  --owner <owner>     ContainerCP owner\n"
                      << "  --dry-run           Inspect without changes\n"
                      << "  --execute           Stage 1: create site\n"
                      << "  --import-files      Stage 2: import web files\n"
                      << "  --import-sql        Stage 3: import database\n"
                      << "  --upgrade           Upgrade existing site runtime\n"
              << "  [--database <name>] Force specific database name\n"
              << "  [--skip-db]         Skip database import\n"
              << "  [--keep-staging]    Keep temporary files\n"
              << "  [--enable-mail]     Enable mail after upgrade\n";
            return 1;
        }

        std::string cmd = "migrate-vesta-site|--backup|" + backup
                        + "|--domain|" + domain
                        + "|--owner|" + owner;
        if (!database.empty()) cmd += "|--database|" + database;
        if (is_upgrade) cmd += "|--upgrade";
        else if (dry_run) cmd += "|--dry-run";
        else if (execute) cmd += "|--execute";
        else if (import_files) cmd += "|--import-files";
        else if (import_sql) cmd += "|--import-sql";
        else cmd += "|--dry-run"; // default safe mode
        if (skip_db) cmd += "|--skip-db";
        if (keep_staging) cmd += "|--keep-staging";
        if (enable_mail) cmd += "|--enable-mail";

        return print_response(send_command(cmd));
    }

    if (arg1 == "storage" && argc >= 3 && std::string(argv[2]) == "migrate-to-sqlite") {
        std::string source, database, archive_root, source_version, target_version;
        bool confirm = false;

        for (int i = 3; i < argc; ++i) {
            std::string arg(argv[i]);
            if (arg == "--source" && i + 1 < argc) source = argv[++i];
            else if (arg == "--database" && i + 1 < argc) database = argv[++i];
            else if (arg == "--archive-root" && i + 1 < argc) archive_root = argv[++i];
            else if (arg == "--source-version" && i + 1 < argc) source_version = argv[++i];
            else if (arg == "--target-version" && i + 1 < argc) target_version = argv[++i];
            else if (arg == "--confirm") confirm = true;
        }

        if (source.empty() || database.empty() || archive_root.empty() ||
            source_version.empty() || target_version.empty() || !confirm) {
            std::cout << "Usage: containercp storage migrate-to-sqlite\n"
                      << "  --source <dir>         Source TXT database directory\n"
                      << "  --database <path>      Target SQLite database path\n"
                      << "  --archive-root <dir>   Archive directory for legacy TXT data\n"
                      << "  --source-version <ver> Source version (e.g. v0.6.0)\n"
                      << "  --target-version <ver> Target version (e.g. v0.7.0)\n"
                      << "  --confirm              Confirm migration intent\n";
            return 1;
        }

        std::string cmd = "migrate-to-sqlite|--source|" + source
                        + "|--database|" + database
                        + "|--archive-root|" + archive_root
                        + "|--source-version|" + source_version
                        + "|--target-version|" + target_version
                        + "|--confirm";
        return print_response(send_command(cmd), true);
    }

    std::cout << "Error: unknown command\n\n";
    print_help();
    return 1;
}

} // namespace containercp::cli
