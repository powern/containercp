#include "DaemonApp.h"
#include "daemon/CommandProtocol.h"
#include "core/Application.h"

#include <iostream>
#include <sstream>

namespace containercp::daemon {

DaemonApp::DaemonApp(core::ServiceRegistry& services)
    : services_(services)
{
}

std::string DaemonApp::handle_command(const std::string& command_line) {
    Command cmd = Command::decode(command_line);
    auto& s = services_;

    if (cmd.name == "node-list") {
        auto& nodes = s.nodes().list();
        std::ostringstream out;
        for (const auto& n : nodes) out << n.name << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "node-show" && cmd.args.size() >= 1) {
        auto* node = s.nodes().find(cmd.args[0]);
        if (!node) return Command::error("Node not found");
        std::ostringstream out;
        out << "Name: " << node->name << "\nType: " << node->type << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "user-list") {
        auto& users = s.users().list();
        std::ostringstream out;
        for (const auto& u : users) out << u.username << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "user-show" && cmd.args.size() >= 1) {
        auto* u = s.users().find(cmd.args[0]);
        if (!u) return Command::error("User not found");
        std::ostringstream out;
        out << "Username: " << u->username << "\n"
            << "UID: " << u->uid << "\n"
            << "Home: " << u->home_directory << "\n"
            << "Shell: " << u->shell << "\n"
            << "Enabled: " << (u->enabled ? "yes" : "no") << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "user-create" && cmd.args.size() >= 1) {
        const auto& username = cmd.args[0];
        if (s.users().find(username)) return Command::error("User already exists");
        std::string home = s.config().users_dir() + username;
        uint64_t uid = 1000 + s.users().list().size();
        s.users().create(username, uid, home, "/usr/sbin/nologin");
        s.filesystem().create_directory(home + "/sites/");
        s.filesystem().create_directory(home + "/logs/");
        s.filesystem().create_directory(home + "/tmp/");
        s.filesystem().create_directory(home + "/backups/");
        s.save();
        return Command::success("User created: " + username);
    }

    if (cmd.name == "user-remove" && cmd.args.size() >= 1) {
        auto* u = s.users().find(cmd.args[0]);
        if (!u) return Command::error("User not found");
        s.users().remove(u->id);
        s.save();
        return Command::success("User removed: " + cmd.args[0]);
    }

    if (cmd.name == "domain-list") {
        auto& domains = s.domains().list();
        std::ostringstream out;
        for (const auto& d : domains) out << d.fqdn << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "domain-show" && cmd.args.size() >= 1) {
        auto* d = s.domains().find(cmd.args[0]);
        if (!d) return Command::error("Domain not found");
        std::ostringstream out;
        out << "Domain: " << d->fqdn << "\n"
            << "Site ID: " << d->site_id << "\n"
            << "PHP: " << d->php_version << "\n"
            << "SSL: " << (d->ssl_enabled ? "yes" : "no") << "\n"
            << "Enabled: " << (d->enabled ? "yes" : "no") << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "domain-remove" && cmd.args.size() >= 1) {
        auto* d = s.domains().find(cmd.args[0]);
        if (!d) return Command::error("Domain not found");
        s.domains().remove(d->id);
        s.save();
        return Command::success("Domain removed: " + cmd.args[0]);
    }

    if (cmd.name == "php-list") {
        auto& versions = s.php_versions().list();
        std::ostringstream out;
        for (const auto& pv : versions) {
            out << pv.version;
            if (pv.default_version) out << " (default)";
            out << "\n";
        }
        return Command::success(out.str());
    }

    if (cmd.name == "php-show" && cmd.args.size() >= 1) {
        auto* pv = s.php_versions().find(cmd.args[0]);
        if (!pv) return Command::error("PHP version not found");
        std::ostringstream out;
        out << "Version: " << pv->version << "\n"
            << "Image: " << pv->image << "\n"
            << "Enabled: " << (pv->enabled ? "yes" : "no") << "\n"
            << "Default: " << (pv->default_version ? "yes" : "no") << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "php-default") {
        auto* pv = s.php_versions().get_default();
        if (!pv) return Command::error("No default PHP version");
        std::ostringstream out;
        out << "Version: " << pv->version << "\n"
            << "Image: " << pv->image << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "site-list") {
        auto& sites = s.sites().list();
        std::ostringstream out;
        for (const auto& site : sites) out << site.domain << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "database-list") {
        auto& databases = s.databases().list();
        std::ostringstream out;
        for (const auto& d : databases) out << d.db_name << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "database-show" && cmd.args.size() >= 1) {
        auto* db = s.databases().find(cmd.args[0]);
        if (!db) return Command::error("Database not found");
        std::ostringstream out;
        out << "Name: " << db->db_name << "\n"
            << "User: " << db->db_user << "\n"
            << "Engine: " << db->engine << "\n"
            << "Version: " << db->version << "\n"
            << "Site ID: " << db->site_id << "\n"
            << "Enabled: " << (db->enabled ? "yes" : "no") << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "database-remove" && cmd.args.size() >= 1) {
        auto* db = s.databases().find(cmd.args[0]);
        if (!db) return Command::error("Database not found");
        s.databases().remove(db->id);
        s.save();
        return Command::success("Database removed: " + cmd.args[0]);
    }

    if (cmd.name == "backup-list") {
        auto& backups = s.backups().list();
        std::ostringstream out;
        for (const auto& b : backups) {
            out << b.id << " " << b.filename << " " << b.status << "\n";
        }
        return Command::success(out.str());
    }

    if (cmd.name == "ssl-list") {
        auto& certs = s.ssl().list();
        std::ostringstream out;
        for (const auto& c : certs) out << c.domain << " [" << c.status << "]\n";
        return Command::success(out.str());
    }

    if (cmd.name == "ssl-show" && cmd.args.size() >= 1) {
        auto* c = s.ssl().find_by_domain(cmd.args[0]);
        if (!c) return Command::error("SSL not found");
        std::ostringstream out;
        out << "Domain: " << c->domain << "\n"
            << "Provider: " << c->provider << "\n"
            << "Status: " << c->status << "\n"
            << "Enabled: " << (c->enabled ? "yes" : "no") << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "mail-list") {
        auto& domains = s.mail().list();
        std::ostringstream out;
        for (const auto& m : domains) out << m.domain << " [" << m.status << "]\n";
        return Command::success(out.str());
    }

    if (cmd.name == "proxy-list") {
        auto& proxies = s.reverse_proxies().list();
        std::ostringstream out;
        for (const auto& p : proxies) {
            out << p.id << " " << p.domain << " " << p.status << "\n";
        }
        return Command::success(out.str());
    }

    if (cmd.name == "config-show") {
        auto& cfg = s.config();
        std::ostringstream out;
        out << "SourceRoot : " << cfg.source_root() << "\n"
            << "ConfigRoot : " << cfg.config_root() << "\n"
            << "DataRoot   : " << cfg.data_root() << "\n"
            << "LogRoot    : " << cfg.log_root() << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "access-user-list") {
        auto& users = s.access_users().list();
        std::ostringstream out;
        for (const auto& u : users) {
            out << u.id << " " << u.username
                << " " << (u.enabled ? "enabled" : "disabled") << "\n";
        }
        return Command::success(out.str());
    }

    if (cmd.name == "access-grant-list") {
        auto& grants = s.access_grants().list();
        std::ostringstream out;
        for (const auto& g : grants) {
            out << g.id << " user=" << g.access_user_id
                << " site=" << g.site_id << "\n";
        }
        return Command::success(out.str());
    }

    return Command::error("Unknown command: " + cmd.name);
}

} // namespace containercp::daemon
