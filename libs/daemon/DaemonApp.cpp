#include "DaemonApp.h"
#include "daemon/CommandProtocol.h"
#include "core/Application.h"
#include "migration/VestaSiteImporter.h"
#include "operations/SiteCreateOperation.h"
#include "operations/SiteRemoveOperation.h"
#include "runtime/CommandExecutor.h"
#include "utils/StringUtils.h"
#include "utils/Validator.h"

#include <chrono>
#include <fstream>
#include <iomanip>
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

    if (cmd.name == "site-remove" && cmd.args.size() >= 1) {
        operations::SiteRemoveOperation op(
            s.sites(), s.domains(), s.databases(),
            s.backups(), s.ssl(), s.mail(),
            s.reverse_proxies(), s.proxy_provider(),
            s.filesystem(), s.config(), s.runtime());
        auto result = op.execute(cmd.args[0]);
        if (result.success) { s.save(); return Command::success("Site removed: " + cmd.args[0]); }
        return Command::error(result.message);
    }

    if (cmd.name == "site-remove-force" && cmd.args.size() >= 1) {
        // Same as site-remove but no confirmation (already handled client-side)
        operations::SiteRemoveOperation op(
            s.sites(), s.domains(), s.databases(),
            s.backups(), s.ssl(), s.mail(),
            s.reverse_proxies(), s.proxy_provider(),
            s.filesystem(), s.config(), s.runtime());
        auto result = op.execute(cmd.args[0]);
        if (result.success) { s.save(); return Command::success("Site removed: " + cmd.args[0]); }
        return Command::error(result.message);
    }

    if (cmd.name == "site-create" && cmd.args.size() >= 2) {
        const std::string& owner = cmd.args[0];
        const std::string& domain = cmd.args[1];
        {
            std::string msg = utils::Validator::validate_username(owner);
            if (!msg.empty()) return Command::error(msg);
        }
        {
            std::string msg = utils::Validator::validate_hostname(domain);
            if (!msg.empty()) return Command::error(msg);
        }
        auto* node = s.nodes().find("local");
        if (!node) return Command::error("No node available");

        // Create a job for progress tracking
        uint64_t job_id = s.jobs().create("site_create", {
            "Validating parameters", "Creating site record",
            "Creating domain", "Creating database",
            "Generating configuration", "Starting containers",
            "Deployment completed"
        });
        s.jobs().update(job_id, "running", 0, "Starting...");

        operations::SiteCreateOperation op(s.sites(), s.domains(),
            s.databases(), s.reverse_proxies(),
            s.proxy_provider(),
            s.filesystem(), s.config(), s.hosting_provider());
        auto result = op.execute(owner, domain, *node, false, "", &s.jobs(), job_id);
        if (result.success) {
            s.save();
            s.jobs().update(job_id, "completed", 100, "Site created: " + domain);
            return Command::success("Site created: " + domain);
        }
        // Save after rollback to persist cleaned state
        s.save();
        s.jobs().update(job_id, "failed", 0, result.message);
        return Command::error(result.message);
    }

    if (cmd.name == "site-create-dry-run" && cmd.args.size() >= 2) {
        auto* node = s.nodes().find("local");
        if (!node) return Command::error("No node available");
        operations::SiteCreateOperation op(s.sites(), s.domains(),
            s.databases(), s.reverse_proxies(),
            s.proxy_provider(),
            s.filesystem(), s.config(), s.hosting_provider());
        auto result = op.execute(cmd.args[0], cmd.args[1], *node, true);
        return result.success ? Command::success("") : Command::error(result.message);
    }

    if (cmd.name == "site-list") {
        auto& sites = s.sites().list();
        std::ostringstream out;
        for (const auto& site : sites) out << site.domain << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "site-start" && cmd.args.size() >= 1) {
        auto* site = s.sites().find(cmd.args[0]);
        if (!site) return Command::error("Site not found");
        auto result = s.hosting_provider().start_site(*site);
        s.save();
        return result.success ? Command::success("Site started") : Command::error(result.message);
    }

    if (cmd.name == "site-stop" && cmd.args.size() >= 1) {
        auto* site = s.sites().find(cmd.args[0]);
        if (!site) return Command::error("Site not found");
        auto result = s.hosting_provider().stop_site(*site);
        s.save();
        return result.success ? Command::success("Site stopped") : Command::error(result.message);
    }

    if (cmd.name == "site-status" && cmd.args.size() >= 1) {
        auto* site = s.sites().find(cmd.args[0]);
        if (!site) return Command::error("Site not found");
        auto result = s.hosting_provider().status(*site);
        return result.success ? Command::success("Status OK") : Command::error(result.message);
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
            << "HTTPS: " << (c->https_enabled ? "enabled" : "disabled") << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "mail-list") {
        auto& domains = s.mail().list();
        std::ostringstream out;
        for (const auto& m : domains) out << m.domain_name << " [" << mail::mail_domain_mode_to_string(m.mode) << "]\n";
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

    if (cmd.name == "template-list") {
        auto by_type = s.profiles().list_by_type(profile::ProfileType::WEB_SERVER);
        std::ostringstream out;
        for (const auto* p : by_type) {
            out << p->profile_name;
            if (p->default_profile) out << " (default)";
            out << "\n";
        }
        return Command::success(out.str());
    }

    if (cmd.name == "template-show" && cmd.args.size() >= 1) {
        auto* p = s.profiles().find(cmd.args[0]);
        if (!p) return Command::error("Template not found");
        std::ostringstream out;
        out << "Name: " << p->profile_name << "\n"
            << "Web Server: " << p->web_server << "\n"
            << "Runtime: " << p->runtime << "\n"
            << "Description: " << p->description << "\n"
            << "Enabled: " << (p->enabled ? "yes" : "no") << "\n"
            << "Default: " << (p->default_profile ? "yes" : "no") << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "template-default") {
        auto* p = s.profiles().get_default(profile::ProfileType::WEB_SERVER);
        if (!p) return Command::error("No default template");
        std::ostringstream out;
        out << "Name: " << p->profile_name << "\n"
            << "Web Server: " << p->web_server << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "template-path") {
        return Command::success(s.config().web_templates_dir());
    }

    if (cmd.name == "template-reload") {
        s.reload_profiles();
        return Command::success("Templates reloaded");
    }

    if (cmd.name == "template-validate" && cmd.args.size() >= 1) {
        auto* profile = s.profiles().find(cmd.args[0]);
        if (!profile) return Command::error("Template profile not found: " + cmd.args[0]);

        if (!s.filesystem().exists(profile->template_path)) {
            return Command::error("Template file not found: " + profile->template_path);
        }

        std::string content = s.filesystem().read_file(profile->template_path);
        if (content.empty()) return Command::error("Template file is empty");

        const char* required_vars[] = {
            "{{DOMAIN}}", "{{PUBLIC_ROOT}}", "{{PHP_UPSTREAM}}",
            "{{LOG_ROOT}}", "{{SSL_ENABLED}}"
        };

        std::string missing;
        for (const auto& var : required_vars) {
            if (content.find(var) == std::string::npos) {
                if (!missing.empty()) missing += ", ";
                missing += var;
            }
        }

        if (!missing.empty()) {
            return Command::error("Missing required variables: " + missing);
        }

        return Command::success("Template is valid");
    }

    if (cmd.name == "backup-list") {
        auto& backups = s.backups().list();
        std::ostringstream out;
        for (const auto& b : backups) {
            out << b.id << " " << b.filename << " " << b.status << " " << b.size << "\n";
        }
        return Command::success(out.str());
    }

    if (cmd.name == "backup-show" && cmd.args.size() >= 1) {
        uint64_t id = std::stoull(cmd.args[0]);
        auto* b = s.backups().find(id);
        if (!b) return Command::error("Backup not found");
        std::ostringstream out;
        out << "ID: " << b->id << "\n"
            << "Filename: " << b->filename << "\n"
            << "Type: " << b->type << "\n"
            << "Size: " << b->size << "\n"
            << "Created: " << b->created_at << "\n"
            << "Status: " << b->status << "\n"
            << "Path: " << b->file_path << "\n"
            << "Compression: " << b->compression << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "backup-remove" && cmd.args.size() >= 1) {
        uint64_t id = std::stoull(cmd.args[0]);
        auto* b = s.backups().find(id);
        if (!b) return Command::error("Backup not found");
        if (!b->file_path.empty()) {
            s.backup_provider().remove_backup(b->file_path);
        }
        s.backups().remove(id);
        s.save();
        return Command::success("Backup removed: " + std::to_string(id));
    }

    if (cmd.name == "backup-create" && cmd.args.size() >= 1) {
        auto* site = s.sites().find(cmd.args[0]);
        if (!site) return Command::error("Site not found");
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ts;
        ts << std::put_time(std::gmtime(&tt), "%Y%m%dT%H%M%SZ");
        std::string timestamp = ts.str();
        std::string filename = site->domain + "-" + timestamp + ".tar.gz";
        std::string file_path = s.config().data_root() + "/backups/" + filename;
        std::string site_dir = s.config().sites_dir() + site->domain + "/";

        // Ensure backup directory exists
        s.filesystem().create_directory(s.config().data_root() + "/backups/");

        auto result = s.backup_provider().create_backup(site_dir, file_path);
        if (!result.success) return Command::error(result.message);

        // Get file size
        std::ifstream f(file_path, std::ios::ate | std::ios::binary);
        uint64_t size = f.tellg();
        f.close();

        s.backups().create(site->id, 0, filename, size, timestamp, file_path, "gzip");
        s.save();
        std::ostringstream out;
        out << "Backup created: " << filename << " (" << size << " bytes)";
        return Command::success(out.str());
    }

    if (cmd.name == "backup-restore" && cmd.args.size() >= 1) {
        uint64_t id = std::stoull(cmd.args[0]);
        auto* b = s.backups().find(id);
        if (!b) return Command::error("Backup not found");
        // Find site by iterating
        std::string site_domain;
        for (const auto& site : s.sites().list()) {
            if (site.id == b->site_id) {
                site_domain = site.domain;
                break;
            }
        }
        if (site_domain.empty()) return Command::error("Site not found");
        std::string site_dir = s.config().sites_dir() + site_domain + "/";
        auto result = s.backup_provider().restore_backup(b->file_path, site_dir);
        if (!result.success) return Command::error(result.message);
        return Command::success("Backup restored: " + b->filename);
    }

    if (cmd.name == "profile-list") {
        auto& profiles = s.profiles().list();
        std::ostringstream out;
        for (const auto& p : profiles) {
            out << p.profile_name;
            if (p.default_profile) out << " (default)";
            out << " [" << profile::profile_type_to_string(p.type) << "]\n";
        }
        return Command::success(out.str());
    }

    if (cmd.name == "profile-show" && cmd.args.size() >= 1) {
        auto* p = s.profiles().find(cmd.args[0]);
        if (!p) return Command::error("Profile not found");
        std::ostringstream out;
        out << "Name: " << p->profile_name << "\n"
            << "Type: " << profile::profile_type_to_string(p->type) << "\n"
            << "Web Server: " << p->web_server << "\n"
            << "Runtime: " << p->runtime << "\n"
            << "Description: " << p->description << "\n"
            << "Enabled: " << (p->enabled ? "yes" : "no") << "\n"
            << "Default: " << (p->default_profile ? "yes" : "no") << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "profile-default") {
        auto* p = s.profiles().get_default(profile::ProfileType::WEB_SERVER);
        if (!p) return Command::error("No default profile");
        std::ostringstream out;
        out << "Name: " << p->profile_name << "\n"
            << "Web Server: " << p->web_server << "\n";
        return Command::success(out.str());
    }

    if (cmd.name == "migrate-vesta-site") {
        migration::Options opts;
        bool is_import_files = false;

        for (size_t i = 0; i < cmd.args.size(); ++i) {
            const auto& arg = cmd.args[i];
            if (arg == "--backup" && i + 1 < cmd.args.size()) opts.backup_path = cmd.args[++i];
            else if (arg == "--domain" && i + 1 < cmd.args.size()) opts.domain = cmd.args[++i];
            else if (arg == "--owner" && i + 1 < cmd.args.size()) opts.owner = cmd.args[++i];
            else if (arg == "--database" && i + 1 < cmd.args.size()) opts.database = cmd.args[++i];
            else if (arg == "--dry-run") opts.dry_run = true;
            else if (arg == "--keep-staging") opts.keep_staging = true;
            else if (arg == "--skip-db") opts.skip_db = true;
            else if (arg == "--import-files") is_import_files = true;
        }

        if (opts.backup_path.empty() || opts.domain.empty() || opts.owner.empty()) {
            return Command::error("Usage: migrate-vesta-site --backup <file> --domain <domain> --owner <owner> [--dry-run] [--skip-db] [--keep-staging] [--database <name>] [--import-files]");
        }

        runtime::CommandExecutor exec;
        migration::VestaSiteImporter importer(exec, s.filesystem(), s.config(), s.logger(),
                                              &s.sites(), &s.domains());

        // ── Stage 2: import files ──
        if (is_import_files) {
            auto import_result = importer.import_files(opts);

            if (!import_result.success) {
                std::string err_msg;
                for (const auto& e : import_result.errors) err_msg += e + "; ";
                return Command::error("Import files failed: " + err_msg);
            }

            std::ostringstream out;
            out << "Stage 2 completed — files imported\n"
                << "Web root:      " << import_result.web_root_type << "\n"
                << "Destination:   " << s.config().sites_dir() + opts.domain + "/public" << "\n"
                << "Files copied:  " << import_result.files_count << "\n"
                << "Size:          " << (import_result.bytes_copied / 1024) << " KB\n"
                << "\n"
                << "Status:\n"
                << "  ✅ Files: imported\n"
                << "  ⏳ SQL import: pending\n"
                << "  ⏳ wp-config update: pending\n";
            for (const auto& w : import_result.warnings) out << "\nWarning: " << w;
            return Command::success(out.str());
        }

        // ── Stage 1: inspect + create site ──
        auto manifest = importer.inspect(opts);
        std::string report = importer.format_dry_run(manifest, opts);

        if (!manifest.errors.empty()) {
            return Command::error(report);
        }

        if (opts.dry_run) {
            return Command::success(report);
        }

        // --execute mode: create the site
        if (manifest.site_exists) {
            return Command::error("Site already exists. Import not possible.");
        }

        auto* node = s.nodes().find("local");
        if (!node) {
            return Command::error("No node available");
        }

        operations::SiteCreateOperation site_op(s.sites(), s.domains(),
            s.databases(), s.reverse_proxies(),
            s.proxy_provider(),
            s.filesystem(), s.config(), s.hosting_provider());

        auto result = site_op.execute(opts.owner, opts.domain, *node, false, "", nullptr, 0);

        if (!result.success) {
            s.save();
            return Command::error("Create site failed: " + result.message);
        }

        s.save();

        // Find created site BEFORE writing marker (need real site_id)
        auto* created_site = s.sites().find(opts.domain);

        // Create migration marker for Stage 2 validation with real site_id
        if (created_site) {
            std::string marker_path = s.config().sites_dir() + opts.domain + "/.containercp-migration.json";
            std::string marker = "{\"domain\":\"" + opts.domain
                + "\",\"owner\":\"" + opts.owner
                + "\",\"site_id\":" + std::to_string(created_site->id)
                + ",\"stage\":1,\"files_pending\":true,\"files_imported\":false,\"sql_pending\":true}";
            s.filesystem().create_file(marker_path, marker);
        } else {
            return Command::error("Site created but SiteRecord not found — migration marker not written");
        }
        std::string sid = created_site ? std::to_string(created_site->id) : "?";
        std::string db_name, db_user;
        for (const auto& d : s.databases().list()) {
            if (created_site && d.site_id == created_site->id) {
                db_name = d.db_name;
                db_user = d.db_user;
                break;
            }
        }

        std::ostringstream out;
        out << "Stage 1 completed — site created\n"
            << "Site ID:      " << sid << "\n"
            << "Domain:       " << opts.domain << "\n"
            << "Database:     " << db_name << " / " << db_user << "\n"
            << "Document root: " << s.config().sites_dir() + opts.domain + "/public" << "\n"
            << "\n"
            << "Status:\n"
            << "  ✅ Site: created\n"
            << "  ✅ Database: created\n"
            << "  ✅ Docker stack: created\n"
            << "  ⏳ Files import: pending\n"
            << "  ⏳ SQL import: pending\n";
        return Command::success(out.str());
    }

    if (cmd.name == "auth-debug") {
        auto users = s.auth_users().list();
        if (users.empty()) {
            return Command::success("Auth users: none\n");
        }
        std::ostringstream out;
        out << "Auth users: " << users.size() << "\n";
        for (const auto& u : users) {
            out << "  username=" << u.username
                << " enabled=" << (u.enabled ? "1" : "0")
                << " must_change=" << (u.must_change_password ? "1" : "0")
                << " hash_present=" << (u.password_hash.empty() ? "no" : "yes")
                << " role=" << u.role
                << "\n";
        }
        return Command::success(out.str());
    }

    return Command::error("Unknown command: " + cmd.name);
}

} // namespace containercp::daemon
