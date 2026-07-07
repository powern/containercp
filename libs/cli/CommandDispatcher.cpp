#include "CommandDispatcher.h"
#include "core/Application.h"
#include "domain/DomainManager.h"
#include "node/Node.h"
#include "operations/SiteCreateOperation.h"
#include "utils/Validator.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
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
        << "  backup create <domain>  Create backup\n"
        << "  backup list            List backups\n"
        << "  backup show <id>       Show backup details\n"
        << "  backup remove <id>     Remove backup\n"
        << "  ssl list               List SSL certificates\n"
        << "  ssl show <domain>      Show SSL certificate\n"
        << "  ssl enable <domain>    Enable SSL for domain\n"
        << "  ssl disable <domain>   Disable SSL for domain\n"
        << "  mail list              List mail domains\n"
        << "  mail show <domain>     Show mail domain\n"
        << "  mail enable <domain>   Enable mail for domain\n"
        << "  mail disable <domain>  Disable mail for domain\n"
        << "  database list       List databases\n"
        << "  database show <name> Show database details\n"
        << "  database remove <name> Remove database\n"
        << "  site list       List sites\n"
        << "  site create <owner> <domain> Create site\n"
        << "  site start <domain>     Start site stack\n"
        << "  site stop <domain>      Stop site stack\n"
        << "  site status <domain>    Show site status\n";
}

void print_version() {
    std::cout << "containercp " << VERSION << "\n";
}

int handle_user_create(const std::string& username) {
    auto& services = containercp::core::Application::instance().services();

    if (!containercp::utils::Validator::is_valid_username(username)) {
        std::cout << "Invalid username.\n";
        return 1;
    }

    if (services.users().find(username) != nullptr) {
        std::cout << "User already exists: " << username << "\n";
        return 1;
    }

    std::string home = services.config().users_dir() + username;
    uint64_t uid = 1000 + services.users().list().size();
    services.users().create(username, uid, home, "/usr/sbin/nologin");
    containercp::core::Application::instance().save();

    services.filesystem().create_directory(home + "/sites/");
    services.filesystem().create_directory(home + "/logs/");
    services.filesystem().create_directory(home + "/tmp/");
    services.filesystem().create_directory(home + "/backups/");
    services.filesystem().create_file(home + "/sites/.gitkeep", "");
    services.filesystem().create_file(home + "/logs/.gitkeep", "");
    services.filesystem().create_file(home + "/tmp/.gitkeep", "");
    services.filesystem().create_file(home + "/backups/.gitkeep", "");

    std::cout << "User created:\n" << username << "\n";
    return 0;
}

int handle_user_list() {
    auto& services = containercp::core::Application::instance().services();
    auto& users = services.users().list();
    if (users.empty()) {
        std::cout << "No users.\n";
    } else {
        for (const auto& u : users) {
            std::cout << u.username << "\n";
        }
    }
    return 0;
}

int handle_user_show(const std::string& username) {
    auto& services = containercp::core::Application::instance().services();
    auto* user = services.users().find(username);
    if (user == nullptr) {
        std::cout << "User not found: " << username << "\n";
        return 1;
    }
    std::cout << "Username: " << user->username << "\n"
              << "UID: " << user->uid << "\n"
              << "Home: " << user->home_directory << "\n"
              << "Shell: " << user->shell << "\n"
              << "Enabled: " << (user->enabled ? "yes" : "no") << "\n";
    return 0;
}

int handle_user_remove(const std::string& username) {
    auto& services = containercp::core::Application::instance().services();
    auto* user = services.users().find(username);
    if (user == nullptr) {
        std::cout << "User not found: " << username << "\n";
        return 1;
    }
    services.users().remove(user->id);
    containercp::core::Application::instance().save();
    std::cout << "User removed:\n" << username << "\n";
    return 0;
}

int handle_site_create(const std::string& owner, const std::string& domain) {
    auto& services = containercp::core::Application::instance().services();

    auto* node = services.nodes().find("local");
    if (node == nullptr) {
        services.logger().error("no node available");
        return 1;
    }

    containercp::operations::SiteCreateOperation op(services.sites(), services.domains(), services.databases(), services.hosting_provider());
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

    if (argc == 4 && arg1 == "user" && std::string(argv[2]) == "create") {
        return handle_user_create(argv[3]);
    }

    if (argc == 3 && arg1 == "user" && std::string(argv[2]) == "list") {
        return handle_user_list();
    }

    if (argc == 4 && arg1 == "user" && std::string(argv[2]) == "show") {
        return handle_user_show(argv[3]);
    }

    if (argc == 4 && arg1 == "user" && std::string(argv[2]) == "remove") {
        return handle_user_remove(argv[3]);
    }

    if (argc == 3 && arg1 == "domain" && std::string(argv[2]) == "list") {
        auto& domains = services.domains().list();
        if (domains.empty()) {
            std::cout << "No domains.\n";
        } else {
            for (const auto& d : domains) {
                std::cout << d.fqdn << "\n";
            }
        }
        return 0;
    }

    if (argc == 4 && arg1 == "domain" && std::string(argv[2]) == "show") {
        auto* domain = services.domains().find(argv[3]);
        if (domain == nullptr) {
            std::cout << "Domain not found: " << argv[3] << "\n";
            return 1;
        }
        std::cout << "Domain: " << domain->fqdn << "\n"
                  << "Site ID: " << domain->site_id << "\n"
                  << "PHP: " << domain->php_version << "\n"
                  << "SSL: " << (domain->ssl_enabled ? "yes" : "no") << "\n"
                  << "Enabled: " << (domain->enabled ? "yes" : "no") << "\n";
        return 0;
    }

    if (argc == 3 && arg1 == "php" && std::string(argv[2]) == "list") {
        auto& versions = services.php_versions().list();
        if (versions.empty()) {
            std::cout << "No PHP versions.\n";
        } else {
            for (const auto& pv : versions) {
                std::cout << pv.version;
                if (pv.default_version) std::cout << " (default)";
                std::cout << "\n";
            }
        }
        return 0;
    }

    if (argc == 4 && arg1 == "php" && std::string(argv[2]) == "show") {
        auto* pv = services.php_versions().find(argv[3]);
        if (pv == nullptr) {
            std::cout << "PHP version not found: " << argv[3] << "\n";
            return 1;
        }
        std::cout << "Version: " << pv->version << "\n"
                  << "Image: " << pv->image << "\n"
                  << "Enabled: " << (pv->enabled ? "yes" : "no") << "\n"
                  << "Default: " << (pv->default_version ? "yes" : "no") << "\n";
        return 0;
    }

    if (argc == 3 && arg1 == "php" && std::string(argv[2]) == "default") {
        auto* pv = services.php_versions().get_default();
        if (pv == nullptr) {
            std::cout << "No default PHP version.\n";
            return 1;
        }
        std::cout << "Version: " << pv->version << "\n"
                  << "Image: " << pv->image << "\n";
        return 0;
    }

    if (argc == 4 && arg1 == "backup" && std::string(argv[2]) == "create") {
        auto* site = services.sites().find(argv[3]);
        if (site == nullptr) {
            std::cout << "Site not found: " << argv[3] << "\n";
            return 1;
        }
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ts;
        ts << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
        std::string created_at = ts.str();
        std::string backup_path = services.config().sites_dir() + site->domain + "/backups/" + created_at + ".backup";
        services.filesystem().create_file(backup_path, "");
        std::ifstream f(backup_path, std::ios::ate | std::ios::binary);
        uint64_t size = f.tellg();
        f.close();
        services.backups().create(site->id, 0, created_at + ".backup", size, created_at);
        containercp::core::Application::instance().save();
        std::cout << "Backup created:\n" << created_at << ".backup\n";
        return 0;
    }

    if (argc == 3 && arg1 == "backup" && std::string(argv[2]) == "list") {
        auto& backups = services.backups().list();
        if (backups.empty()) {
            std::cout << "No backups.\n";
        } else {
            for (const auto& b : backups) {
                std::cout << b.id << " " << b.filename << " " << b.status << "\n";
            }
        }
        return 0;
    }

    if (argc == 4 && arg1 == "backup" && std::string(argv[2]) == "show") {
        uint64_t id = std::stoull(argv[3]);
        auto* b = services.backups().find(id);
        if (b == nullptr) {
            std::cout << "Backup not found: " << id << "\n";
            return 1;
        }
        std::cout << "ID: " << b->id << "\n"
                  << "Site ID: " << b->site_id << "\n"
                  << "File: " << b->filename << "\n"
                  << "Type: " << b->type << "\n"
                  << "Size: " << b->size << "\n"
                  << "Created: " << b->created_at << "\n"
                  << "Status: " << b->status << "\n";
        return 0;
    }

    if (argc == 4 && arg1 == "backup" && std::string(argv[2]) == "remove") {
        uint64_t id = std::stoull(argv[3]);
        auto* b = services.backups().find(id);
        if (b == nullptr) {
            std::cout << "Backup not found: " << id << "\n";
            return 1;
        }
        services.backups().remove(b->id);
        containercp::core::Application::instance().save();
        std::cout << "Backup removed:\n" << id << "\n";
        return 0;
    }

    if (argc == 3 && arg1 == "ssl" && std::string(argv[2]) == "list") {
        auto& certs = services.ssl().list();
        if (certs.empty()) {
            std::cout << "No SSL certificates.\n";
        } else {
            for (const auto& c : certs) {
                std::cout << c.domain << " [" << c.status << "]\n";
            }
        }
        return 0;
    }

    if (argc == 4 && arg1 == "ssl" && std::string(argv[2]) == "show") {
        auto* c = services.ssl().find_by_domain(argv[3]);
        if (c == nullptr) {
            std::cout << "SSL not found: " << argv[3] << "\n";
            return 1;
        }
        std::cout << "Domain: " << c->domain << "\n"
                  << "Provider: " << c->provider << "\n"
                  << "Cert: " << c->certificate_path << "\n"
                  << "Key: " << c->key_path << "\n"
                  << "Expires: " << c->expires_at << "\n"
                  << "Status: " << c->status << "\n"
                  << "Enabled: " << (c->enabled ? "yes" : "no") << "\n";
        return 0;
    }

    if (argc == 4 && arg1 == "ssl" && std::string(argv[2]) == "enable") {
        if (services.ssl().find_by_domain(argv[3]) != nullptr) {
            std::cout << "SSL already enabled for " << argv[3] << "\n";
            return 1;
        }
        std::string cert = services.config().sites_dir() + argv[3] + "/ssl/fullchain.pem";
        std::string key = services.config().sites_dir() + argv[3] + "/ssl/privkey.pem";
        services.ssl().create(0, argv[3], cert, key);
        containercp::core::Application::instance().save();
        std::cout << "SSL enabled:\n" << argv[3] << "\n";
        return 0;
    }

    if (argc == 4 && arg1 == "ssl" && std::string(argv[2]) == "disable") {
        auto* c = services.ssl().find_by_domain(argv[3]);
        if (c == nullptr) {
            std::cout << "SSL not found: " << argv[3] << "\n";
            return 1;
        }
        services.ssl().remove(c->id);
        containercp::core::Application::instance().save();
        std::cout << "SSL disabled:\n" << argv[3] << "\n";
        return 0;
    }

    if (argc == 3 && arg1 == "mail" && std::string(argv[2]) == "list") {
        auto& domains = services.mail().list();
        if (domains.empty()) {
            std::cout << "No mail domains.\n";
        } else {
            for (const auto& m : domains) {
                std::cout << m.domain << " [" << m.status << "]\n";
            }
        }
        return 0;
    }

    if (argc == 4 && arg1 == "mail" && std::string(argv[2]) == "show") {
        auto* m = services.mail().find_by_domain(argv[3]);
        if (m == nullptr) {
            std::cout << "Mail domain not found: " << argv[3] << "\n";
            return 1;
        }
        std::cout << "Domain: " << m->domain << "\n"
                  << "Status: " << m->status << "\n"
                  << "Enabled: " << (m->enabled ? "yes" : "no") << "\n";
        return 0;
    }

    if (argc == 4 && arg1 == "mail" && std::string(argv[2]) == "enable") {
        if (services.mail().find_by_domain(argv[3]) != nullptr) {
            std::cout << "Mail already enabled for " << argv[3] << "\n";
            return 1;
        }
        services.mail().create(0, argv[3], 0);
        containercp::core::Application::instance().save();
        std::cout << "Mail enabled:\n" << argv[3] << "\n";
        return 0;
    }

    if (argc == 4 && arg1 == "mail" && std::string(argv[2]) == "disable") {
        auto* m = services.mail().find_by_domain(argv[3]);
        if (m == nullptr) {
            std::cout << "Mail domain not found: " << argv[3] << "\n";
            return 1;
        }
        services.mail().remove(m->id);
        containercp::core::Application::instance().save();
        std::cout << "Mail disabled:\n" << argv[3] << "\n";
        return 0;
    }

    if (argc == 3 && arg1 == "database" && std::string(argv[2]) == "list") {
        auto& databases = services.databases().list();
        if (databases.empty()) {
            std::cout << "No databases.\n";
        } else {
            for (const auto& d : databases) {
                std::cout << d.db_name << "\n";
            }
        }
        return 0;
    }

    if (argc == 4 && arg1 == "database" && std::string(argv[2]) == "show") {
        auto* db = services.databases().find(argv[3]);
        if (db == nullptr) {
            std::cout << "Database not found: " << argv[3] << "\n";
            return 1;
        }
        std::cout << "Name: " << db->db_name << "\n"
                  << "User: " << db->db_user << "\n"
                  << "Engine: " << db->engine << "\n"
                  << "Version: " << db->version << "\n"
                  << "Site ID: " << db->site_id << "\n"
                  << "Enabled: " << (db->enabled ? "yes" : "no") << "\n";
        return 0;
    }

    if (argc == 4 && arg1 == "database" && std::string(argv[2]) == "remove") {
        auto* db = services.databases().find(argv[3]);
        if (db == nullptr) {
            std::cout << "Database not found: " << argv[3] << "\n";
            return 1;
        }
        services.databases().remove(db->id);
        containercp::core::Application::instance().save();
        std::cout << "Database removed:\n" << argv[3] << "\n";
        return 0;
    }

    if (argc == 4 && arg1 == "domain" && std::string(argv[2]) == "remove") {
        auto* domain = services.domains().find(argv[3]);
        if (domain == nullptr) {
            std::cout << "Domain not found: " << argv[3] << "\n";
            return 1;
        }
        services.domains().remove(domain->id);
        containercp::core::Application::instance().save();
        std::cout << "Domain removed:\n" << argv[3] << "\n";
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
