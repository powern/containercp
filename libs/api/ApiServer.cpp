#include "ApiServer.h"
#include "api/JsonFormatter.h"
#include "core/Version.h"
#include "jobs/JobManager.h"
#include "operations/SiteCreateOperation.h"
#include "operations/SiteRemoveOperation.h"

#include "mail/DkimManager.h"
#include "mail/MailDomain.h"
#include "mail/MailDomainManager.h"
#include "mail/MailPasswordHasher.h"
#include "mail/MailboxManager.h"

#include "ssl/CertificateStore.h"
#include "ssl/CertificateProvider.h"
#include "migration/VestaSiteImporter.h"
#include "utils/Validator.h"

#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstring>
#include <sys/stat.h>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

#include <mutex>

namespace containercp::api {

// Serialize conflicting proxy management operations (reload, sync, recover)
static std::mutex proxy_op_mutex;

// Simple JSON value extraction
static std::string json_extract(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\":";
        pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos += search.size();
        auto end = json.find_first_of(",}", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }
    pos += search.size();
    auto end = json.find("\"", pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

// Check if a key exists in JSON (for PATCH field presence detection).
// Works with any value type: string, number, boolean, null, object, array.
static bool json_has_key(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    return json.find(search) != std::string::npos;
}

// Trim leading and trailing whitespace.
static std::string trim(const std::string& s) {
    size_t start = 0, end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) ++start;
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\n' || s[end-1] == '\r')) --end;
    return s.substr(start, end - start);
}

// Normalize a domain name: lowercase, trim whitespace, remove trailing dots.
// Does NOT remove internal spaces — those will be rejected by hostname validation.
// Normalization happens in the API layer before calling the manager.
// The manager accepts data as-is; the API is responsible for pre-processing.
static std::string normalize_domain(const std::string& raw) {
    std::string d = trim(raw);
    for (auto& c : d) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!d.empty() && d.back() == '.') d.pop_back();
    return d;
}

ApiServer::ApiServer(core::ServiceRegistry& services, int port)
    : port_(port)
    , services_(services)
    , auth_(std::make_unique<AllowAllAuth>())
{
}

void ApiServer::handle_client(int client_fd, ApiServer* server) {
    Request req = server->parse_request(client_fd);

    Response resp;
    if (!server->auth_->authenticate(req, resp)) {
        std::string resp_str = resp.to_string();
        ::write(client_fd, resp_str.data(), resp_str.size());
        ::close(client_fd);
        return;
    }

    if (req.path.find("/api/") != 0) {
        resp = server->serve_static(req.path);
    } else {
        resp = server->router_.dispatch(req);
    }

    std::string resp_str = resp.to_string();
    ::write(client_fd, resp_str.data(), resp_str.size());
    ::close(client_fd);
}

Response ApiServer::serve_static(const std::string& path) const {
    std::string file_path = services_.config().source_root() + "/web";
    if (path == "/" || path.empty()) {
        file_path += "/index.html";
    } else {
        std::string clean = path.substr(1);
        if (clean.find("..") != std::string::npos) {
            Response r;
            r.status_code = 403;
            r.body = "Forbidden";
            return r;
        }
        file_path += "/" + clean;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        Response r;
        r.status_code = 404;
        r.body = "Not found";
        return r;
    }

    std::string content((std::istreambuf_iterator<char>(file)), {});

    std::string ext;
    auto dot = file_path.find_last_of('.');
    if (dot != std::string::npos) ext = file_path.substr(dot);

    Response r;
    r.status_code = 200;
    r.body = content;
    if (ext == ".css") r.content_type = "text/css";
    else if (ext == ".js") r.content_type = "application/javascript";
    else if (ext == ".png") r.content_type = "image/png";
    else if (ext == ".ico") r.content_type = "image/x-icon";
    else r.content_type = "text/html";

    return r;
}

Request ApiServer::parse_request(int client_fd) const {
    Request req;
    char buf[24576];  // 24KB buffer for larger POST bodies
    ssize_t n = ::read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return req;
    buf[n] = '\0';

    std::istringstream stream(buf);
    std::string line;

    if (!std::getline(stream, line)) return req;
    {
        std::istringstream line_stream(line);
        line_stream >> req.method >> req.path;
    }

    bool chunked = false;
    int content_length = 0;

    while (std::getline(stream, line)) {
        if (line.empty() || line == "\r") break;
        if (line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            if (!val.empty() && val[0] == ' ') val = val.substr(1);
            req.headers[key] = val;
            if (key == "Content-Length") content_length = std::stoi(val);
            if (key == "Transfer-Encoding" && val == "chunked") chunked = true;
        }
    }

    auto qpos = req.path.find('?');
    if (qpos != std::string::npos) {
        std::string qs = req.path.substr(qpos + 1);
        req.path = req.path.substr(0, qpos);
        std::istringstream qs_stream(qs);
        while (std::getline(qs_stream, line, '&')) {
            auto eq = line.find('=');
            if (eq != std::string::npos) {
                req.query[line.substr(0, eq)] = line.substr(eq + 1);
            }
        }
    }

    if (chunked) {
        // Simple chunked body reading
        std::string body;
        std::string chunk_line;
        while (std::getline(stream, chunk_line)) {
            if (chunk_line.back() == '\r') chunk_line.pop_back();
            if (chunk_line.empty()) continue;
            int chunk_size = std::stoi(chunk_line, nullptr, 16);
            if (chunk_size == 0) break;
            char chunk[8192];
            stream.read(chunk, chunk_size);
            body.append(chunk, chunk_size);
            stream.ignore(2); // \r\n
        }
        req.body = body;
    } else if (content_length > 0) {
        req.body = std::string(buf + n - content_length, content_length);
    }

    return req;
}

bool ApiServer::start() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        services_.logger().error("ApiServer: Failed to create socket");
        return false;
    }

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = ::htons(port_);

    if (::bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        services_.logger().error("ApiServer: Failed to bind to port " + std::to_string(port_));
        ::close(server_fd_);
        return false;
    }

    if (::listen(server_fd_, 5) < 0) {
        services_.logger().error("ApiServer: Failed to listen");
        ::close(server_fd_);
        return false;
    }

    running_ = true;
    services_.logger().info("ApiServer: Listening on 127.0.0.1:" + std::to_string(port_));

    auto& s = services_;

    // Helper: extract domain from path after /api/ssl/
    auto ssl_domain_from_path = [](const std::string& path) -> std::string {
        // path is like /api/ssl/example.com or /api/ssl/example.com/issue
        std::string prefix = "/api/ssl/";
        if (path.compare(0, prefix.size(), prefix) != 0) return "";
        std::string rest = path.substr(prefix.size());
        auto slash = rest.find('/');
        if (slash == std::string::npos) return rest;
        return rest.substr(0, slash);
    };

    // Helper: build standard JSON error response
    auto json_error = [](const std::string& code, const std::string& message) -> std::string {
        std::ostringstream json;
        json << "{\"success\":false,\"error\":{\"code\":\""
             << JsonFormatter::escape(code) << "\",\"message\":\""
             << JsonFormatter::escape(message) << "\",\"details\":{}}}";
        return json.str();
    };

    // GET endpoints
    router_.add("GET", "/api/version", [](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::version(containercp::core::VERSION));
        return r;
    });

    router_.add("GET", "/api/health", [&s](const Request&) {
        Response r;
        auto reports = s.health().check_all();
        std::string modules_json;
        if (!reports.empty()) {
            modules_json = ",\"modules\":{";
            bool first_module = true;
            for (const auto& [name, report] : reports) {
                if (!first_module) modules_json += ",";
                first_module = false;
                modules_json += "\"" + JsonFormatter::escape(name) + "\":{";
                modules_json += "\"status\":\"" + JsonFormatter::escape(report.status) + "\"";

                // Services array
                if (!report.services.empty()) {
                    modules_json += ",\"services\":[";
                    bool first_svc = true;
                    for (const auto& svc : report.services) {
                        if (!first_svc) modules_json += ",";
                        first_svc = false;
                        modules_json += "{\"name\":\"" + JsonFormatter::escape(svc.name) + "\""
                            + ",\"status\":\"" + JsonFormatter::escape(svc.status) + "\""
                            + ",\"message\":\"" + JsonFormatter::escape(svc.message) + "\"}";
                    }
                    modules_json += "]";
                }

                // Optional details (JSON object)
                if (!report.details.empty()) {
                    modules_json += ",\"details\":" + report.details;
                }

                modules_json += "}";
            }
            modules_json += "}";
        }

        bool overall_ok = true;
        for (const auto& [name, report] : reports) {
            if (report.status != "ok") { overall_ok = false; break; }
        }
        r.body = "{\"success\":true,\"data\":{\"status\":\""
            + std::string(overall_ok ? "ok" : "degraded") + "\""
            + modules_json + "}}";
        return r;
    });

    router_.add("GET", "/api/sites", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::sites(s.sites().list()));
        return r;
    });

    router_.add("GET", "/api/users", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::users(s.users().list()));
        return r;
    });

    router_.add("GET", "/api/domains", [&s](const Request&) {
        Response r;
        r.body = "{\"success\":true,\"data\":" + s.domain_view().build_enriched_json() + "}";
        return r;
    });

    // GET /api/proxy — enriched proxy entry list
    router_.add("GET", "/api/proxy", [&s](const Request&) {
        Response r;
        r.body = "{\"success\":true,\"data\":" + s.proxy_view().build_enriched_json() + "}";
        return r;
    });

    // GET /api/proxy/health — global proxy health status (lightweight, no docker exec)
    router_.add("GET", "/api/proxy/health", [&s](const Request&) {
        Response r;
        auto rec_status = s.recovery().status();
        r.body = "{\"success\":true,\"data\":"
                 + s.proxy_view().build_health_json(
                     rec_status.manager_running,
                     rec_status.recovery_in_progress,
                     rec_status.last_recovery_at,
                     rec_status.last_recovery_result)
                 + "}";
        return r;
    });

    // POST /api/proxy/test — validate nginx configuration
    router_.add("POST", "/api/proxy/test", [&s](const Request&) {
        Response r;
        if (!proxy_op_mutex.try_lock()) {
            r.status_code = 409;
            r.body = "{\"success\":false,\"error\":\"Another proxy operation is in progress\"}";
            return r;
        }
        std::lock_guard<std::mutex> lock(proxy_op_mutex, std::adopt_lock);
        auto result = s.proxy_provider().test_config();
        if (result.success) {
            r.body = "{\"success\":true,\"data\":{\"message\":\"" +
                     JsonFormatter::escape(result.message) + "\"}}";
        } else {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"" +
                     JsonFormatter::escape(result.message) + "\"}";
        }
        return r;
    });

    // POST /api/proxy/reload — reload nginx configuration
    router_.add("POST", "/api/proxy/reload", [&s](const Request&) {
        Response r;
        if (!proxy_op_mutex.try_lock()) {
            r.status_code = 409;
            r.body = "{\"success\":false,\"error\":\"Another proxy operation is in progress\"}";
            return r;
        }
        std::lock_guard<std::mutex> lock(proxy_op_mutex, std::adopt_lock);
        // Validate before reload
        auto test = s.proxy_provider().test_config();
        if (!test.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Config test failed: " +
                     JsonFormatter::escape(test.message) + "\"}";
            return r;
        }
        auto result = s.proxy_provider().reload();
        if (result.success) {
            r.body = "{\"success\":true,\"data\":{\"message\":\"nginx reloaded\"}}";
        } else {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"" +
                     JsonFormatter::escape(result.message) + "\"}";
        }
        return r;
    });

    // POST /api/proxy/sync — regenerate all HTTPS proxy configs
    router_.add("POST", "/api/proxy/sync", [&s](const Request&) {
        Response r;
        if (!proxy_op_mutex.try_lock()) {
            r.status_code = 409;
            r.body = "{\"success\":false,\"error\":\"Another proxy operation is in progress\"}";
            return r;
        }
        std::lock_guard<std::mutex> lock(proxy_op_mutex, std::adopt_lock);
        s.sync_all_https_configs();
        r.body = "{\"success\":true,\"data\":{\"message\":\"HTTPS configs synced\"}}";
        return r;
    });

    // POST /api/proxy/recover — full proxy self-healing
    router_.add("POST", "/api/proxy/recover", [&s](const Request&) {
        Response r;
        auto result = s.recovery().recover_now();
        if (result.success) {
            r.body = "{\"success\":true,\"data\":{\"message\":\"" +
                     JsonFormatter::escape(result.message) + "\"}}";
        } else {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"" +
                     JsonFormatter::escape(result.message) + "\"}";
        }
        return r;
    });

    // GET /api/runtime/<site_id> — container status (web/PHP) + HTTPS summary
    //
    // HTTPS status is derived from CertificateStore (single source of truth).
    // SiteRuntimeManager only handles container runtime — no SSL duplication.
    router_.add_prefix("GET", "/api/runtime/", [&s](const Request& req) {
        Response r;
        std::string id_str = req.path.substr(std::string("/api/runtime/").size());
        uint64_t site_id = 0;
        try { site_id = std::stoull(id_str); } catch (...) {}
        if (site_id == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid site ID\"}";
            return r;
        }
        auto* site = s.sites().find_by_id(site_id);
        if (!site) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Site not found\"}";
            return r;
        }
        auto status = s.site_runtime().get_status(site_id, site->domain);

        // HTTPS status from CertificateStore (single source of truth)
        std::string https_status = "Disabled";
        auto meta = s.cert_store().load_metadata(site_id);
        if (meta.success) {
            https_status = s.cert_store().https_display_status(meta.metadata);
        }

        std::ostringstream json;
        json << "{\"success\":true,\"data\":{"
             << "\"web\":\"" << status.web.status
             << "\",\"php\":\"" << status.php.status
             << "\",\"db\":\"" << status.db.status
             << "\",\"cache\":\"" << status.cache.status
             << "\",\"https\":\"" << https_status
             << "\"}}";
        r.body = json.str();
        return r;
    });

    // POST /api/runtime/<site_id>/<action> — execute runtime action async
    // Actions: restart-web, restart-php, restart-all
    router_.add_prefix("POST", "/api/runtime/", [&s](const Request& req) {
        Response r;
        std::string remaining = req.path.substr(std::string("/api/runtime/").size());
        size_t slash = remaining.find('/');
        std::string id_str = (slash != std::string::npos) ? remaining.substr(0, slash) : remaining;
        std::string action = (slash != std::string::npos) ? remaining.substr(slash + 1) : "";

        if (id_str.empty() || action.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Usage: POST /api/runtime/<site_id>/<action>\"}";
            return r;
        }

        uint64_t site_id = 0;
        try { site_id = std::stoull(id_str); } catch (...) {}
        if (site_id == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid site ID\"}";
            return r;
        }

        auto* site = s.sites().find_by_id(site_id);
        if (!site) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Site not found\"}";
            return r;
        }

        // Validate action against known list
        bool valid = false;
        for (const auto& a : s.site_runtime().valid_actions()) {
            if (a == action) { valid = true; break; }
        }
        if (!valid) {
            std::string valid_list;
            for (const auto& a : s.site_runtime().valid_actions()) {
                if (!valid_list.empty()) valid_list += ", ";
                valid_list += a;
            }
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid action '" +
                     JsonFormatter::escape(action) + "'. Valid: " + valid_list + "\"}";
            return r;
        }

        // Resolve to compose services (empty = all services in project)
        auto services = s.site_runtime().services_for_action(action);

        // Create async job
        std::string action_label = action;
        if (action == "restart-all") action_label = "all services";
        else if (action == "restart-web") action_label = "web";
        else if (action == "restart-php") action_label = "PHP";
        else if (action == "restart-db") action_label = "database";
        else if (action == "restart-redis") action_label = "Redis";
        std::vector<std::string> steps = {"Preparing restart...",
                                           "Restarting " + action_label + "...",
                                           "Verifying..."};
        uint64_t job_id = s.jobs().create("runtime-" + action, steps);
        s.jobs().update(job_id, "pending", 0, "Queued");

        bool submitted = s.job_executor().submit(job_id,
            [&s, domain = site->domain, services, action, site_id]
            (jobs::JobManager& jm, uint64_t jid) {
                jm.update(jid, "running", 10, "Restarting...");

                std::string compose_dir = s.config().sites_dir() + domain;
                // Remove trailing slash if present
                if (!compose_dir.empty() && compose_dir.back() == '/') {
                    compose_dir.pop_back();
                }

                auto result = s.runtime_executor().restart_services(compose_dir, services);
                if (result.success) {
                    jm.update(jid, "completed", 100, "Restart completed");
                } else {
                    jm.update(jid, "failed", 100, result.message);
                }
            });

        if (!submitted) {
            s.jobs().update(job_id, "failed", 0, "Task queue full");
            r.status_code = 503;
            r.body = "{\"success\":false,\"error\":\"Task queue full\"}";
            return r;
        }

        r.status_code = 202;
        std::ostringstream json;
        json << "{\"success\":true,\"data\":{"
             << "\"job_id\":" << job_id
             << ",\"status\":\"pending\""
             << ",\"message\":\"Action " << JsonFormatter::escape(action)
             << " queued for site " << site_id << "\""
             << "}}";
        r.body = json.str();
        return r;
    });

    router_.add("GET", "/api/settings", [&s](const Request&) {
        Response r;
        std::ostringstream json;
        json << "{\"success\":true,\"data\":{"
             << "\"version\":\"" << containercp::core::VERSION
             << "\",\"server_hostname\":\"" << JsonFormatter::escape(s.config().server_hostname())
             << "\"}}";
        r.body = json.str();
        return r;
    });

    router_.add("POST", "/api/settings", [&s](const Request& req) {
        Response r;
        std::string hostname = json_extract(req.body, "server_hostname");
        // Basic validation
        if (!hostname.empty() && hostname.find("..") != std::string::npos) {
            r.body = "{\"success\":false,\"error\":\"Invalid hostname\"}";
            return r;
        }
        s.config().set_server_hostname(hostname);
        s.logger().info("SYSTEM", "Server hostname set to: " + hostname);
        r.body = "{\"success\":true,\"data\":{\"server_hostname\":\"" + JsonFormatter::escape(hostname) + "\"}}";
        return r;
    });

    // GET /api/ssl — list all sites with SSL state (including HTTP_ONLY)
    router_.add("GET", "/api/ssl", [&s](const Request&) {
        Response r;
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;
        for (const auto& site : s.sites().list()) {
            if (!first) json << ",";
            first = false;

            std::string domain = site.name;
            auto load_result = s.cert_store().load_metadata(site.id);

            json << "{\"domain\":\"" << JsonFormatter::escape(domain) << "\"";

            if (load_result.success) {
                const auto& meta = load_result.metadata;
                json << ",\"site_id\":" << meta.site_id
                     << ",\"provider_id\":\"" << JsonFormatter::escape(meta.provider_id)
                     << "\",\"environment\":\"" << JsonFormatter::escape(meta.environment)
                     << "\",\"status\":\"" << JsonFormatter::escape(meta.status)
                     << "\",\"https_enabled\":" << (meta.https_enabled ? "true" : "false")
                     << ",\"redirect_enabled\":" << (meta.redirect_enabled ? "true" : "false")
                     << ",\"auto_renew\":" << (meta.auto_renew ? "true" : "false")
                     << ",\"expires_at\":\"" << JsonFormatter::escape(meta.expires_at)
                     << "\",\"last_error\":\"" << JsonFormatter::escape(meta.last_error)
                     << "\"";
            } else {
                json << ",\"site_id\":" << site.id
                     << ",\"provider_id\":\"\""
                     << ",\"status\":\"HTTP_ONLY\""
                     << ",\"https_enabled\":false"
                     << ",\"redirect_enabled\":false"
                     << ",\"auto_renew\":false"
                     << ",\"expires_at\":\"\""
                     << ",\"last_error\":\"\"";
            }
            json << "}";
        }
        json << "]}";
        r.body = json.str();
        return r;
    });

    router_.add("GET", "/api/backups", [&s](const Request&) {
        Response r;
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;
        for (const auto& b : s.backups().list()) {
            if (!first) json << ",";
            first = false;
            json << "{\"id\":" << b.id
                 << ",\"site_id\":" << b.site_id
                 << ",\"filename\":\"" << JsonFormatter::escape(b.filename)
                 << "\",\"type\":\"" << JsonFormatter::escape(b.type)
                 << "\",\"size\":" << b.size
                 << ",\"created_at\":\"" << JsonFormatter::escape(b.created_at)
                 << "\",\"status\":\"" << JsonFormatter::escape(b.status)
                 << "\",\"file_path\":\"" << JsonFormatter::escape(b.file_path)
                 << "\",\"compression\":\"" << JsonFormatter::escape(b.compression)
                 << "\"}";
        }
        json << "]}";
        r.body = json.str();
        return r;
    });

    router_.add("GET", "/api/databases", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::databases(s.databases().list()));
        return r;
    });

    router_.add("GET", "/api/access-users", [&s](const Request&) {
        Response r;
        auto& users = s.access_users().list();
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;
        for (const auto& u : users) {
            if (!first) json << ",";
            first = false;
            json << "{\"id\":" << u.id
                 << ",\"username\":\"" << u.username
                 << "\",\"enabled\":" << (u.enabled ? "true" : "false")
                 << "}";
        }
        json << "]}";
        r.body = json.str();
        return r;
    });

    router_.add("GET", "/api/profiles", [&s](const Request&) {
        Response r;
        auto& profiles = s.profiles().list();
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;
        for (const auto& p : profiles) {
            if (!first) json << ",";
            first = false;
            json << "{\"id\":" << p.id
                 << ",\"name\":\"" << JsonFormatter::escape(p.profile_name)
                 << "\",\"type\":\"" << profile::profile_type_to_string(p.type)
                 << "\",\"web_server\":\"" << JsonFormatter::escape(p.web_server)
                 << "\",\"description\":\"" << JsonFormatter::escape(p.description)
                 << "\",\"enabled\":" << (p.enabled ? "true" : "false")
                 << ",\"default\":" << (p.default_profile ? "true" : "false")
                 << "}";
        }
        json << "]}";
        r.body = json.str();
        return r;
    });

    router_.add("GET", "/api/nodes", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::nodes(s.nodes().list()));
        return r;
    });

    router_.add("GET", "/api/logs", [&s](const Request&) {
        std::string ts = "2024-01-01T00:00:00Z";
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        json << "{\"time\":\"" << ts << "\",\"level\":\"info\",\"message\":\"Daemon started\"},";
        json << "{\"time\":\"" << ts << "\",\"level\":\"info\",\"message\":\"Storage loaded\"},";
        json << "{\"time\":\"" << ts << "\",\"level\":\"info\",\"message\":\"REST API listening\"}";
        json << "]}";
        Response r;
        r.body = json.str();
        return r;
    });

    // Job listing: GET /api/jobs  and  GET /api/jobs?id=123
    auto job_handler = [&s](const Request& req) {
        Response r;
        // Check if id query param is present
        auto it = req.query.find("id");
        if (it != req.query.end()) {
            uint64_t id = std::stoull(it->second);
            auto* job = s.jobs().find(id);
            if (!job) {
                r.status_code = 404;
                r.body = "{\"success\":false,\"error\":\"Job not found\"}";
                return r;
            }
            std::ostringstream json;
            json << "{\"success\":true,\"data\":{"
                 << "\"id\":" << job->id
                 << ",\"type\":\"" << JsonFormatter::escape(job->type)
                 << "\",\"status\":\"" << JsonFormatter::escape(job->status)
                 << "\",\"progress\":" << job->progress
                 << ",\"current_step\":" << job->current_step
                 << ",\"message\":\"" << JsonFormatter::escape(job->message)
                 << "\",\"created_at\":\"" << JsonFormatter::escape(job->created_at)
                 << "\"}}";
            r.body = json.str();
            return r;
        }
        // List all jobs
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;
        for (const auto& j : s.jobs().list()) {
            if (!first) json << ",";
            first = false;
            json << "{\"id\":" << j.id
                 << ",\"type\":\"" << JsonFormatter::escape(j.type)
                 << "\",\"status\":\"" << JsonFormatter::escape(j.status)
                 << "\",\"progress\":" << j.progress
                 << ",\"message\":\"" << JsonFormatter::escape(j.message)
                 << "\",\"created_at\":\"" << JsonFormatter::escape(j.created_at)
                 << "\"}";
        }
        json << "]}";
        r.body = json.str();
        return r;
    };
    router_.add("GET", "/api/jobs", job_handler);

    // POST endpoints
    router_.add("POST", "/api/sites/create", [&s](const Request& req) {
        Response r;
        std::string owner = json_extract(req.body, "owner");
        std::string domain = json_extract(req.body, "domain");
        std::string profile = json_extract(req.body, "profile");
        if (owner.empty() || domain.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"owner and domain required\"}";
            return r;
        }

        // Create job
        auto& jobs = s.jobs();
        auto& nodes = s.nodes();
        auto* node = nodes.find("local");
        if (!node) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"No node available\"}";
            return r;
        }

        uint64_t job_id = jobs.create("site_create", {
            "Validating parameters", "Creating site record",
            "Creating domain", "Creating database",
            "Generating configuration", "Starting MariaDB",
            "Starting PHP", "Starting Web server",
            "Creating proxy configuration", "Reloading proxy",
            "Deployment completed"
        });
        jobs.update(job_id, "running", 0);

        operations::SiteCreateOperation op(s.sites(), s.domains(),
            s.databases(), s.reverse_proxies(),
            s.proxy_provider(),
            s.filesystem(), s.config(), s.hosting_provider());
        auto result = op.execute(owner, domain, *node, false, profile, &s.jobs(), job_id);

        if (result.success) {
            s.save();
            jobs.update(job_id, "completed", 100, "Site created successfully");
            std::ostringstream json;
            json << "{\"success\":true,\"data\":{\"domain\":\"" << domain << "\",\"job_id\":" << job_id << ",\"message\":\"Site created\"}}";
            r.body = json.str();
        } else {
            // Save after rollback to persist cleaned state
            s.save();
            jobs.update(job_id, "failed", 0, result.message);
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(result.message) + "\"}";
        }
        return r;
    });

    router_.add("POST", "/api/sites/remove", [&s](const Request& req) {
        Response r;
        std::string domain = json_extract(req.body, "domain");
        if (domain.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"domain required\"}";
            return r;
        }

        operations::SiteRemoveOperation op(
            s.sites(), s.domains(), s.databases(),
            s.backups(), s.ssl(), s.mail(),
            s.reverse_proxies(), s.proxy_provider(),
            s.filesystem(), s.config(), s.runtime());

        auto result = op.execute(domain);
        if (result.success) {
            s.save();
            r.body = "{\"success\":true,\"data\":{\"message\":\"Site removed\"}}";
        } else {
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(result.message) + "\"}";
        }
        return r;
    });

    router_.add("POST", "/api/backups/create", [&s](const Request& req) {
        Response r;
        std::string domain = json_extract(req.body, "domain");
        if (domain.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"domain required\"}";
            return r;
        }

        auto* site = s.sites().find(domain);
        if (!site) {
            r.body = "{\"success\":false,\"error\":\"Site not found\"}";
            return r;
        }

        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        std::ostringstream ts;
        ts << std::put_time(std::gmtime(&tt), "%Y%m%dT%H%M%SZ");
        std::string timestamp = ts.str();
        std::string filename = domain + "-" + timestamp + ".tar.gz";
        std::string file_path = s.config().data_root() + "/backups/" + filename;
        std::string site_dir = s.config().sites_dir() + domain + "/";

        s.filesystem().create_directory(s.config().data_root() + "/backups/");
        auto result = s.backup_provider().create_backup(site_dir, file_path);
        if (!result.success) {
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(result.message) + "\"}";
            return r;
        }

        std::ifstream f(file_path, std::ios::ate | std::ios::binary);
        uint64_t size = f.tellg();
        f.close();

        s.backups().create(site->id, 0, filename, size, timestamp, file_path, "gzip");
        s.save();

        std::ostringstream json;
        json << "{\"success\":true,\"data\":{\"filename\":\"" << filename << "\",\"size\":" << size << "}}";
        r.body = json.str();
        return r;
    });

    router_.add("POST", "/api/backups/remove", [&s](const Request& req) {
        Response r;
        std::string id_str = json_extract(req.body, "id");
        if (id_str.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"id required\"}";
            return r;
        }
        uint64_t id = std::stoull(id_str);
        auto* b = s.backups().find(id);
        if (!b) {
            r.body = "{\"success\":false,\"error\":\"Backup not found\"}";
            return r;
        }
        if (!b->file_path.empty()) {
            s.backup_provider().remove_backup(b->file_path);
        }
        s.backups().remove(id);
        s.save();
        r.body = "{\"success\":true,\"data\":{\"message\":\"Backup removed\"}}";
        return r;
    });

    router_.add("POST", "/api/backups/restore", [&s](const Request& req) {
        Response r;
        std::string id_str = json_extract(req.body, "id");
        if (id_str.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"id required\"}";
            return r;
        }
        uint64_t id = std::stoull(id_str);
        auto* b = s.backups().find(id);
        if (!b) {
            r.body = "{\"success\":false,\"error\":\"Backup not found\"}";
            return r;
        }
        std::string site_domain;
        for (const auto& site : s.sites().list()) {
            if (site.id == b->site_id) {
                site_domain = site.domain;
                break;
            }
        }
        if (site_domain.empty()) {
            r.body = "{\"success\":false,\"error\":\"Site not found\"}";
            return r;
        }
        std::string site_dir = s.config().sites_dir() + site_domain + "/";
        auto result = s.backup_provider().restore_backup(b->file_path, site_dir);
        if (!result.success) {
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(result.message) + "\"}";
            return r;
        }
        r.body = "{\"success\":true,\"data\":{\"message\":\"Backup restored: " + JsonFormatter::escape(b->filename) + "\"}}";
        return r;
    });

    // GET /api/ssl/providers — list available providers
    router_.add("GET", "/api/ssl/providers", [&s](const Request&) {
        Response r;
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;
        for (const auto& [id, provider] : s.certificate_providers()) {
            if (!first) json << ",";
            first = false;
            json << "{\"id\":\"" << JsonFormatter::escape(id)
                 << "\",\"name\":\"" << JsonFormatter::escape(provider->provider_name())
                 << "\",\"supports_auto_renew\":" << (provider->supports_auto_renew() ? "true" : "false")
                 << ",\"supports_dns\":" << (provider->supports_dns_challenge() ? "true" : "false")
                 << "}";
        }
        json << "]}";
        r.body = json.str();
        return r;
    });

    // GET /api/ssl/<domain> — certificate details
    router_.add_prefix("GET", "/api/ssl/", [&s, &ssl_domain_from_path, &json_error](const Request& req) {
        Response r;
        std::string domain = ssl_domain_from_path(req.path);
        if (domain.empty()) {
            r.status_code = 400;
            r.body = json_error("INVALID_DOMAIN", "Domain is required");
            return r;
        }

        // Check if domain resolves to a known site
        uint64_t site_id = 0;
        std::string site_domain;
        for (const auto& site : s.sites().list()) {
            if (site.name == domain) {
                site_id = site.id;
                site_domain = site.name;
                break;
            }
        }
        if (site_id == 0) {
            // Check if this is the admin panel hostname
            if (domain == s.config().server_hostname()) {
                site_id = 0;
                site_domain = domain;
            } else {
                r.status_code = 404;
                r.body = json_error("NOT_FOUND", "Site not found: " + domain);
                return r;
            }
        }

        auto load_result = s.cert_store().load_metadata(site_id);
        if (!load_result.success && load_result.error != containercp::ssl::CertificateStore::LoadError::NOT_FOUND) {
            // Corrupted metadata — return ERROR state
            std::ostringstream json;
            json << "{\"success\":true,\"data\":{"
                 << "\"site_id\":" << site_id
                 << ",\"domain\":\"" << JsonFormatter::escape(site_domain)
                 << "\",\"status\":\"ERROR\""
                 << ",\"last_error\":\"" << JsonFormatter::escape(load_result.message)
                 << "\",\"https_enabled\":false"
                 << ",\"redirect_enabled\":false"
                 << "}}";
            r.body = json.str();
            return r;
        }

        std::ostringstream json;
        json << "{\"success\":true,\"data\":{"
             << "\"site_id\":" << site_id
             << ",\"domain\":\"" << JsonFormatter::escape(site_domain) << "\"";

        if (load_result.success) {
            const auto& meta = load_result.metadata;
            json << ",\"provider_id\":\"" << JsonFormatter::escape(meta.provider_id)
                 << "\",\"environment\":\"" << JsonFormatter::escape(meta.environment)
                 << "\",\"certificate_type\":\"" << JsonFormatter::escape(meta.certificate_type)
                 << "\",\"status\":\"" << JsonFormatter::escape(meta.status)
                 << "\",\"issued_at\":\"" << JsonFormatter::escape(meta.issued_at)
                 << "\",\"expires_at\":\"" << JsonFormatter::escape(meta.expires_at)
                 << "\",\"renew_after\":\"" << JsonFormatter::escape(meta.renew_after)
                 << "\",\"https_enabled\":" << (meta.https_enabled ? "true" : "false")
                 << ",\"redirect_enabled\":" << (meta.redirect_enabled ? "true" : "false")
                 << ",\"auto_renew\":" << (meta.auto_renew ? "true" : "false")
                 << ",\"challenge_type\":\"" << JsonFormatter::escape(meta.challenge_type)
                 << "\",\"issuer\":\"" << JsonFormatter::escape(meta.issuer)
                 << "\",\"subject\":\"" << JsonFormatter::escape(meta.subject)
                 << "\",\"fingerprint_sha256\":\"" << JsonFormatter::escape(meta.fingerprint_sha256)
                 << "\",\"last_validation\":\"" << JsonFormatter::escape(meta.last_validation)
                 << "\",\"last_error\":\"" << JsonFormatter::escape(meta.last_error)
                 << "\",\"renew_attempts\":" << meta.renew_attempts;
        } else {
            json << ",\"status\":\"HTTP_ONLY\""
                 << ",\"https_enabled\":false"
                 << ",\"redirect_enabled\":false"
                 << ",\"auto_renew\":false";
        }
        json << "}}";
        r.body = json.str();
        return r;
    });

    // POST /api/ssl/<domain>/<action> — SSL mutations
    router_.add_prefix("POST", "/api/ssl/", [&s, &ssl_domain_from_path, &json_error](const Request& req) {
        Response r;
        std::string domain = ssl_domain_from_path(req.path);
        if (domain.empty()) {
            r.status_code = 400;
            r.body = json_error("INVALID_DOMAIN", "Domain is required");
            return r;
        }

        // Extract action from path: /api/ssl/<domain>/<action> or /api/ssl/<domain>/redirect/<subaction>
        std::string prefix = "/api/ssl/" + domain + "/";
        std::string action = req.path.substr(prefix.size());

        // Find site (or admin panel hostname with site_id=0)
        uint64_t site_id = 0;
        for (const auto& site : s.sites().list()) {
            if (site.name == domain) {
                site_id = site.id;
                break;
            }
        }
        // Check if this is the admin panel hostname
        if (site_id == 0 && domain == s.config().server_hostname()) {
            site_id = 0; // admin panel virtual site
        }
        // If not found as regular site or admin hostname, return error
        if (site_id == 0 && domain != s.config().server_hostname()) {
            r.status_code = 404;
            r.body = json_error("NOT_FOUND", "Site not found: " + domain);
            return r;
        }

        if (action == "issue") {
            std::string provider_id = json_extract(req.body, "provider_id");
            if (provider_id.empty()) provider_id = "letsencrypt";

            // Validate provider_id exists before creating job
            if (s.certificate_providers().find(provider_id) == s.certificate_providers().end()) {
                r.status_code = 400;
                r.body = json_error("INVALID_PROVIDER", "Unknown provider: " + provider_id);
                return r;
            }

            // Save placeholder metadata so the provider can resolve site_id
            containercp::ssl::CertificateStore::Metadata meta;
            meta.site_id = site_id;
            meta.provider_id = provider_id;
            meta.status = "issuing";
            meta.domains = {domain};
            meta.auto_renew = true;
            meta.challenge_type = "http-01";
            meta.created_at = containercp::ssl::CertificateStore::timestamp_utc();
            meta.updated_at = meta.created_at;
            s.cert_store().save_metadata(site_id, meta);

            std::vector<std::string> steps = {"Validating domain...", "Requesting certificate...",
                                               "Waiting for ACME validation...", "Finalizing..."};
            uint64_t job_id = s.jobs().create("ssl-issue", steps);
            s.jobs().update(job_id, "pending", 0, "Queued");

            // Enqueue async job execution via JobExecutor
            bool submitted = s.job_executor().submit(job_id,
                [&s, provider_id, domain](jobs::JobManager& jm, uint64_t jid) {
                    auto& provider = *s.certificate_providers()[provider_id];
                    jm.update(jid, "running", 10, "Requesting certificate...");
                    auto result = provider.request(domain);
                    if (result.success) {
                        // Reload nginx so it picks up the new certificate via current symlink
                        jm.update(jid, "running", 95, "Reloading nginx...");
                        s.proxy_provider().reload();
                        jm.update(jid, "completed", 100, "Certificate issued");
                    } else {
                        jm.update(jid, "failed", 100, result.message);
                    }
                });
            if (!submitted) {
                s.jobs().update(job_id, "failed", 0, "Task queue full");
            }

            r.status_code = 202;
            std::ostringstream json;
            json << "{\"success\":true,\"data\":{"
                 << "\"job_id\":" << job_id
                 << ",\"status\":\"pending\""
                 << ",\"message\":\"Certificate issuance queued\""
                 << "}}";
            r.body = json.str();
            return r;
        }

        if (action == "renew") {
            // Load existing metadata to get provider_id
            auto load_result = s.cert_store().load_metadata(site_id);
            std::string provider_id = "letsencrypt";
            if (load_result.success) {
                provider_id = load_result.metadata.provider_id;
            }

            if (s.certificate_providers().find(provider_id) == s.certificate_providers().end()) {
                r.status_code = 400;
                r.body = json_error("INVALID_PROVIDER", "Unknown provider: " + provider_id);
                return r;
            }

            // Save updated metadata with renewing status
            containercp::ssl::CertificateStore::Metadata meta;
            meta.site_id = site_id;
            meta.provider_id = provider_id;
            meta.status = "issuing";
            meta.domains = {domain};
            meta.auto_renew = true;
            meta.challenge_type = "http-01";
            meta.updated_at = containercp::ssl::CertificateStore::timestamp_utc();
            s.cert_store().save_metadata(site_id, meta);

            std::vector<std::string> steps = {"Validating domain...", "Renewing certificate...",
                                               "Finalizing..."};
            uint64_t job_id = s.jobs().create("ssl-renew", steps);
            s.jobs().update(job_id, "pending", 0, "Queued");

            bool submitted = s.job_executor().submit(job_id,
                [&s, provider_id, domain](jobs::JobManager& jm, uint64_t jid) {
                    auto& provider = *s.certificate_providers()[provider_id];
                    jm.update(jid, "running", 10, "Renewing certificate...");
                    auto result = provider.renew(domain);
                    if (result.success) {
                        jm.update(jid, "running", 95, "Reloading nginx...");
                        s.proxy_provider().reload();
                        jm.update(jid, "completed", 100, "Certificate renewed");
                    } else {
                        jm.update(jid, "failed", 100, result.message);
                    }
                });
            if (!submitted) {
                s.jobs().update(job_id, "failed", 0, "Task queue full");
            }

            r.status_code = 202;
            std::ostringstream json;
            json << "{\"success\":true,\"data\":{"
                 << "\"job_id\":" << job_id
                 << ",\"status\":\"pending\""
                 << ",\"message\":\"Certificate renewal queued\""
                 << "}}";
            r.body = json.str();
            return r;
        }

        if (action == "enable") {
            // Validate certificate through CertificateStore
            auto validation = s.cert_store().validate(site_id);
            if (!validation.valid) {
                r.status_code = 409;
                std::string reason = "Certificate validation failed";
                if (!validation.errors.empty()) reason = validation.errors[0];
                r.body = json_error("SSL_INVALID_STATE", reason);
                return r;
            }

            auto load_result = s.cert_store().load_metadata(site_id);
            if (!load_result.success || load_result.metadata.status != "active") {
                r.status_code = 409;
                r.body = json_error("SSL_INVALID_STATE", "Certificate is not in active state");
                return r;
            }

            // Transactional enable: attach cert to proxy FIRST, only save metadata on success
            std::string cert_path = s.cert_store().fullchain_path(site_id);
            std::string key_path = s.cert_store().privkey_path(site_id);
            s.logger().info("API", "Attaching cert for " + domain + ": cert=" + cert_path + " key=" + key_path);
            auto proxy_result = s.proxy_provider().attach_certificate(domain, cert_path, key_path);
            if (!proxy_result.success) {
                r.status_code = 500;
                r.body = json_error("PROXY_ERROR", proxy_result.message);
                return r;
            }

            // Only now save metadata (after proxy confirmed config is valid and reloaded)
            auto meta = load_result.metadata;
            meta.https_enabled = true;
            meta.updated_at = containercp::ssl::CertificateStore::timestamp_utc();
            s.cert_store().save_metadata(site_id, meta);

            auto* cert = s.ssl().find_by_domain(domain);
            if (cert) { cert->https_enabled = true; s.save(); }

            std::ostringstream json;
            json << "{\"success\":true,\"data\":{"
                 << "\"domain\":\"" << JsonFormatter::escape(domain)
                 << "\",\"https_enabled\":true"
                 << "}}";
            r.body = json.str();
            return r;
        }

        if (action == "disable") {
            // Detach from proxy FIRST, only save metadata on success
            auto proxy_result = s.proxy_provider().detach_certificate(domain);
            if (!proxy_result.success) {
                r.status_code = 500;
                r.body = json_error("PROXY_ERROR", proxy_result.message);
                return r;
            }

            auto load_result = s.cert_store().load_metadata(site_id);
            if (load_result.success) {
                auto meta = load_result.metadata;
                meta.https_enabled = false;
                meta.updated_at = containercp::ssl::CertificateStore::timestamp_utc();
                s.cert_store().save_metadata(site_id, meta);
            }

            auto* cert = s.ssl().find_by_domain(domain);
            if (cert) { cert->https_enabled = false; s.save(); }

            std::ostringstream json;
            json << "{\"success\":true,\"data\":{"
                 << "\"domain\":\"" << JsonFormatter::escape(domain)
                 << "\",\"https_enabled\":false"
                 << "}}";
            r.body = json.str();
            return r;
        }

        if (action == "redirect/enable") {
            auto load_result = s.cert_store().load_metadata(site_id);
            if (!load_result.success || load_result.metadata.status != "active") {
                r.status_code = 409;
                r.body = json_error("SSL_INVALID_STATE", "Certificate is not active");
                return r;
            }
            if (!load_result.metadata.https_enabled) {
                r.status_code = 409;
                r.body = json_error("SSL_INVALID_STATE", "HTTPS must be enabled before enabling redirect");
                return r;
            }

            // Generate new config with redirect, validate, reload, THEN save
            std::string cert_path = s.cert_store().fullchain_path(site_id);
            std::string key_path = s.cert_store().privkey_path(site_id);
            auto proxy_result = s.proxy_provider().attach_certificate(domain, cert_path, key_path, true);
            if (!proxy_result.success) {
                r.status_code = 500;
                r.body = json_error("PROXY_ERROR", proxy_result.message);
                return r;
            }

            auto meta = load_result.metadata;
            meta.redirect_enabled = true;
            meta.updated_at = containercp::ssl::CertificateStore::timestamp_utc();
            s.cert_store().save_metadata(site_id, meta);

            std::ostringstream json;
            json << "{\"success\":true,\"data\":{"
                 << "\"domain\":\"" << JsonFormatter::escape(domain)
                 << "\",\"redirect_enabled\":true"
                 << "}}";
            r.body = json.str();
            return r;
        }

        if (action == "redirect/disable") {
            auto load_result = s.cert_store().load_metadata(site_id);
            // Generate new config without redirect
            std::string cert_path = s.cert_store().fullchain_path(site_id);
            std::string key_path = s.cert_store().privkey_path(site_id);
            auto proxy_result = s.proxy_provider().attach_certificate(domain, cert_path, key_path, false);
            if (!proxy_result.success) {
                r.status_code = 500;
                r.body = json_error("PROXY_ERROR", proxy_result.message);
                return r;
            }

            if (load_result.success) {
                auto meta = load_result.metadata;
                meta.redirect_enabled = false;
                meta.updated_at = containercp::ssl::CertificateStore::timestamp_utc();
                s.cert_store().save_metadata(site_id, meta);
            }

            std::ostringstream json;
            json << "{\"success\":true,\"data\":{"
                 << "\"domain\":\"" << JsonFormatter::escape(domain)
                 << "\",\"redirect_enabled\":false"
                 << "}}";
            r.body = json.str();
            return r;
        }

        r.status_code = 404;
        r.body = json_error("NOT_FOUND", "Unknown SSL action: " + action);
        return r;
    });

    // Resource CRUD endpoints
    auto remove_resource = [&s](const std::string& type, const std::string& name) -> Response {
        Response r;
        if (type == "domain") {
            auto* domain = s.domains().find(name);
            if (!domain) { r.body = "{\"success\":false,\"error\":\"Not found\"}"; return r; }
            s.domains().remove(domain->id);
        } else if (type == "database") {
            auto* db = s.databases().find(name);
            if (!db) { r.body = "{\"success\":false,\"error\":\"Not found\"}"; return r; }
            s.databases().remove(db->id);
        } else if (type == "ssl") {
            auto* cert = s.ssl().find_by_domain(name);
            if (!cert) { r.body = "{\"success\":false,\"error\":\"Not found\"}"; return r; }
            s.ssl().remove(cert->id);
        } else if (type == "proxy") {
            auto* p = s.reverse_proxies().find_by_domain(name);
            if (!p) { r.body = "{\"success\":false,\"error\":\"Not found\"}"; return r; }
            // Protect system/admin proxy entries (site_id == 0)
            if (p->site_id == 0) {
                r.status_code = 403;
                r.body = "{\"success\":false,\"error\":\"System proxy entry cannot be removed\"}";
                return r;
            }
            // Remove nginx config file first, BEFORE removing the record
            auto remove_result = s.proxy_provider().remove_proxy(name);
            if (!remove_result.success) {
                r.status_code = 500;
                r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(remove_result.message) + "\"}";
                return r;
            }
            // Reload nginx to apply the change
            auto reload_result = s.proxy_provider().reload();
            if (!reload_result.success) {
                r.status_code = 500;
                r.body = "{\"success\":false,\"error\":\"Config removed but reload failed: "
                         + JsonFormatter::escape(reload_result.message) + "\"}";
                return r;
            }
            s.reverse_proxies().remove(p->id);
        } else if (type == "access-user") {
            auto* u = s.access_users().find(name);
            if (!u) { r.body = "{\"success\":false,\"error\":\"Not found\"}"; return r; }
            s.access_users().remove(u->id);
        } else {
            r.body = "{\"success\":false,\"error\":\"Unknown type\"}";
            return r;
        }
        s.save();
        r.body = "{\"success\":true,\"data\":{\"message\":\"Removed\"}}";
        return r;
    };

    auto modify_ssl = [&s](const std::string& domain, bool enable) -> Response {
        Response r;
        auto* cert = s.ssl().find_by_domain(domain);
        if (!cert) { r.body = "{\"success\":false,\"error\":\"SSL not found\"}"; return r; }
        cert->https_enabled = enable;
        if (enable) cert->status = "active";
        else cert->status = "disabled";
        s.save();
        r.body = enable ? "{\"success\":true,\"data\":{\"message\":\"SSL enabled\"}}"
                        : "{\"success\":true,\"data\":{\"message\":\"SSL disabled\"}}";
        return r;
    };

    router_.add("POST", "/api/domains/remove", [&s, &remove_resource](const Request& req) {
        std::string domain = json_extract(req.body, "domain");
        return remove_resource("domain", domain);
    });

    router_.add("POST", "/api/databases/remove", [&s, &remove_resource](const Request& req) {
        std::string name = json_extract(req.body, "name");
        return remove_resource("database", name);
    });

    router_.add("POST", "/api/ssl/remove", [&s, &remove_resource](const Request& req) {
        std::string domain = json_extract(req.body, "domain");
        return remove_resource("ssl", domain);
    });

    router_.add("POST", "/api/proxy/remove", [&s, &remove_resource](const Request& req) {
        std::string domain = json_extract(req.body, "domain");
        return remove_resource("proxy", domain);
    });

    router_.add("POST", "/api/access-users/remove", [&s, &remove_resource](const Request& req) {
        std::string username = json_extract(req.body, "username");
        return remove_resource("access-user", username);
    });

    router_.add("POST", "/api/ssl/enable", [&s, &modify_ssl](const Request& req) {
        std::string domain = json_extract(req.body, "domain");
        return modify_ssl(domain, true);
    });

    router_.add("POST", "/api/ssl/disable", [&s, &modify_ssl](const Request& req) {
        std::string domain = json_extract(req.body, "domain");
        return modify_ssl(domain, false);
    });

    // ── Mail Domain API ────────────────────────────────────────────
    // Helper: serialize a single MailDomain to JSON
    auto mail_domain_json = [](const mail::MailDomain& m) -> std::string {
        std::ostringstream j;
        j << "{"
          << "\"id\":" << m.id
          << ",\"domain\":\"" << JsonFormatter::escape(m.domain_name)
          << "\",\"mode\":\"" << JsonFormatter::escape(mail::mail_domain_mode_to_string(m.mode))
          << "\",\"domain_id\":" << m.domain_id
          << ",\"site_id\":" << m.site_id
          << ",\"enabled\":" << (m.enabled ? "true" : "false")
          << ",\"relay_host\":\"" << JsonFormatter::escape(m.relay_host)
           << "\",\"dkim_selector\":\"" << JsonFormatter::escape(m.dkim_selector)
           << "\",\"dkim_public_key_dns\":\"" << JsonFormatter::escape(m.dkim_public_key_dns)
           << "\",\"max_mailboxes\":" << m.max_mailboxes
          << ",\"max_aliases\":" << m.max_aliases
          << ",\"catch_all\":\"" << JsonFormatter::escape(m.catch_all)
          << "\",\"created_at\":\"" << JsonFormatter::escape(m.created_at)
           << "\",\"updated_at\":\"" << JsonFormatter::escape(m.updated_at)
           << "\"}";
        return j.str();
    };

    // ── Mailbox helpers (defined before routes so both mailbox and domain routes can use them) ──
    // Helper: normalize a local part (lowercase, trim, reject invalid)
    auto normalize_local_part = [](std::string raw) -> std::string {
        size_t s = 0, e = raw.size();
        while (s < e && (raw[s] == ' ' || raw[s] == '\t')) ++s;
        while (e > s && (raw[e-1] == ' ' || raw[e-1] == '\t')) --e;
        raw = raw.substr(s, e - s);
        for (auto& c : raw) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return raw;
    };

    auto validate_local_part = [](const std::string& lp) -> std::string {
        if (lp.empty()) return "local_part is required";
        if (lp[0] == '.') return "local_part must not start with a dot";
        if (lp.back() == '.') return "local_part must not end with a dot";
        if (lp.find("..") != std::string::npos) return "local_part must not contain consecutive dots";
        for (char c : lp) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
                return "Invalid character in local_part. Allowed: a-z, 0-9, ., _, -";
            }
        }
        return "";
    };

    auto domain_name_by_id = [&s](uint64_t domain_id) -> std::string {
        auto* domain = s.mail().find(domain_id);
        return domain ? domain->domain_name : "";
    };

    auto mailbox_json = [&domain_name_by_id](const mail::Mailbox& mb) -> std::string {
        std::string domain_name = domain_name_by_id(mb.domain_id);
        std::string address = mb.local_part + (domain_name.empty() ? "" : "@" + domain_name);
        std::ostringstream j;
        j << "{"
          << "\"id\":" << mb.id
          << ",\"domain_id\":" << mb.domain_id
          << ",\"local_part\":\"" << api::JsonFormatter::escape(mb.local_part)
          << "\",\"address\":\"" << api::JsonFormatter::escape(address)
          << "\",\"quota_bytes\":" << mb.quota_bytes
          << ",\"quota_messages\":" << mb.quota_messages
          << ",\"enabled\":" << (mb.enabled ? "true" : "false")
          << ",\"display_name\":\"" << api::JsonFormatter::escape(mb.display_name)
          << "\",\"forward_to\":\"" << api::JsonFormatter::escape(mb.forward_to)
          << "\",\"spam_enabled\":" << (mb.spam_enabled ? "true" : "false")
          << ",\"last_login\":\"" << api::JsonFormatter::escape(mb.last_login)
          << "\",\"created_at\":\"" << api::JsonFormatter::escape(mb.created_at)
          << "\",\"updated_at\":\"" << api::JsonFormatter::escape(mb.updated_at)
          << "\"}";
        return j.str();
    };

    auto alias_json = [](const mail::MailAlias& a) -> std::string {
        std::ostringstream j;
        j << "{"
          << "\"id\":" << a.id
          << ",\"domain_id\":" << a.domain_id
          << ",\"source\":\"" << api::JsonFormatter::escape(a.source_local_part)
          << "\",\"destination\":\"" << api::JsonFormatter::escape(a.destination)
          << "\",\"enabled\":" << (a.enabled ? "true" : "false")
          << ",\"created_at\":\"" << api::JsonFormatter::escape(a.created_at)
          << ",\"updated_at\":\"" << api::JsonFormatter::escape(a.updated_at)
          << "\"}";
        return j.str();
    };

    auto now_utc = []() -> std::string {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
        gmtime_r(&tt, &tm_buf);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
        return std::string(buf);
    };

    // ── Mailbox sub-routes (registered before domain routes so they match /mailboxes first) ──
    // GET /api/mail/domains/<id>/mailboxes — list mailboxes for a domain
    // ── Mail domain sub-routes (consolidated dispatchers) ───────────
    //
    // Single GET and POST prefix handlers for /api/mail/domains/<id>/...
    // Internal dispatch by sub-path avoids prefix handler conflicts.
    //
    // Supported sub-routes:
    //   GET  /api/mail/domains/<id>           → single domain
    //   GET  /api/mail/domains/<id>/mailboxes → list mailboxes
    //   GET  /api/mail/domains/<id>/aliases   → list aliases
    //   POST /api/mail/domains/<id>/mailboxes → create mailbox
    //   POST /api/mail/domains/<id>/aliases   → create alias
    //   POST /api/mail/domains/<id>/dkim/generate → generate DKIM

    router_.add_prefix("GET", "/api/mail/domains/", [&s, &mail_domain_json, &mailbox_json, &alias_json](const Request& req) {
        Response r;
        std::string remaining = req.path.substr(std::string("/api/mail/domains/").size());
        auto slash = remaining.find('/');
        std::string id_str = (slash != std::string::npos) ? remaining.substr(0, slash) : remaining;
        std::string sub = (slash != std::string::npos) ? remaining.substr(slash + 1) : "";
        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {}
        if (id == 0) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"Invalid domain ID\"}"; return r; }

        if (sub.empty()) {
            // GET /api/mail/domains/<id> — single domain
            auto* m = s.mail().find(id);
            if (!m) { r.status_code = 404; r.body = "{\"success\":false,\"error\":\"Mail domain not found\"}"; return r; }
            r.body = "{\"success\":true,\"data\":" + mail_domain_json(*m) + "}";
        } else if (sub == "mailboxes") {
            // GET /api/mail/domains/<id>/mailboxes — list mailboxes
            auto mailboxes = s.mailboxes().find_by_domain(id);
            std::ostringstream json;
            json << "{\"success\":true,\"data\":[";
            bool first = true;
            for (auto* mb : mailboxes) { if (!first) json << ","; first = false; json << mailbox_json(*mb); }
            json << "]}";
            r.body = json.str();
        } else if (sub == "aliases") {
            // GET /api/mail/domains/<id>/aliases — list aliases
            auto aliases = s.mail_aliases().find_by_domain(id);
            std::ostringstream json;
            json << "{\"success\":true,\"data\":[";
            bool first = true;
            for (auto* a : aliases) { if (!first) json << ","; first = false; json << alias_json(*a); }
            json << "]}";
            r.body = json.str();
        } else {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Unknown mail domain sub-resource: " + JsonFormatter::escape(sub) + "\"}";
        }
        return r;
    });

    router_.add_prefix("POST", "/api/mail/domains/", [&s, &mailbox_json, &alias_json, &now_utc, &normalize_local_part, &validate_local_part](const Request& req) {
        Response r;
        std::string remaining = req.path.substr(std::string("/api/mail/domains/").size());
        auto slash = remaining.find('/');
        std::string id_str = (slash != std::string::npos) ? remaining.substr(0, slash) : remaining;
        std::string sub = (slash != std::string::npos) ? remaining.substr(slash + 1) : "";
        uint64_t domain_id = 0;
        try { domain_id = std::stoull(id_str); } catch (...) {}
        if (domain_id == 0) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"Invalid domain ID\"}"; return r; }
        auto* domain = s.mail().find(domain_id);
        if (!domain) { r.status_code = 404; r.body = "{\"success\":false,\"error\":\"Mail domain not found\"}"; return r; }

        if (sub == "mailboxes") {
            // POST /api/mail/domains/<id>/mailboxes — create mailbox
            std::string local_part_raw = json_extract(req.body, "local_part");
            std::string password = json_extract(req.body, "password");
            std::string local_part = normalize_local_part(local_part_raw);
            if (local_part.empty()) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"local_part is required\"}"; return r; }
            std::string val_err = validate_local_part(local_part);
            if (!val_err.empty()) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(val_err) + "\"}"; return r; }
            if (password.empty()) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"password is required\"}"; return r; }
            if (password.size() < 3) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"Password must be at least 3 characters\"}"; return r; }
            std::string hash = mail::MailPasswordHasher::hash(password);
            if (hash.empty()) { r.status_code = 500; r.body = "{\"success\":false,\"error\":\"Failed to hash password\"}"; return r; }
            uint64_t id = s.mailboxes().create(domain_id, local_part, hash);
            if (id == 0) { r.status_code = 409; r.body = "{\"success\":false,\"error\":\"Mailbox already exists for this local part\"}"; return r; }
            auto* created = s.mailboxes().find(id);
            if (created) { created->created_at = now_utc(); created->updated_at = created->created_at; }
            s.save();
            (void)s.runtime_sync().sync("mail");
            if (created) { r.body = "{\"success\":true,\"data\":" + mailbox_json(*created) + "}"; return r; }
            r.body = "{\"success\":true,\"data\":{\"id\":" + std::to_string(id) + "}}";
        } else if (sub == "aliases") {
            // POST /api/mail/domains/<id>/aliases — create alias
            std::string source_raw = json_extract(req.body, "source");
            std::string dest = json_extract(req.body, "destination");
            std::string source = normalize_local_part(source_raw);
            if (source.empty()) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"source is required\"}"; return r; }
            std::string src_err = validate_local_part(source);
            if (!src_err.empty()) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(src_err) + "\"}"; return r; }
            if (dest == "null") { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"destination cannot be null\"}"; return r; }
            if (dest.empty()) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"destination is required\"}"; return r; }
            if (dest.find('@') == std::string::npos) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"destination must be a valid email address\"}"; return r; }
            // Reject self-loop (alias points to its own source address)
            if (dest == source + "@" + domain->domain_name) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Alias destination cannot be the same as source\"}";
                return r;
            }
            uint64_t id = s.mail_aliases().create(domain_id, source, dest);
            if (id == 0) { r.status_code = 409; r.body = "{\"success\":false,\"error\":\"Alias already exists\"}"; return r; }
            auto* created = s.mail_aliases().find(id);
            if (created) { created->created_at = now_utc(); created->updated_at = created->created_at; }
            s.save();
            (void)s.runtime_sync().sync("mail");
            if (created) { r.body = "{\"success\":true,\"data\":" + alias_json(*created) + "}"; return r; }
            r.body = "{\"success\":true,\"data\":{\"id\":" + std::to_string(id) + "}}";
        } else if (sub == "dkim/generate") {
            // POST /api/mail/domains/<id>/dkim/generate — generate DKIM key
            std::string dkim_dir = s.config().data_root() + "/mail/config/state/dkim";
            std::string dns_record = s.dkim().generate_key(
                dkim_dir, domain->domain_name, domain->dkim_selector);
            if (dns_record.empty()) { r.status_code = 500; r.body = "{\"success\":false,\"error\":\"Failed to generate DKIM key\"}"; return r; }
            domain->dkim_public_key_dns = dns_record;
            s.save();
            {
                auto sync_result = s.runtime_sync().sync("mail");
                if (!sync_result.success) {
                    r.status_code = 500;
                    r.body = "{\"success\":false,\"error\":\"DKIM key generated, "
                        "but mail configuration apply failed: " +
                        JsonFormatter::escape(sync_result.message) + "\"}";
                    return r;
                }
            }
            r.body = "{\"success\":true,\"data\":{\"message\":\"DKIM key generated\""
                     ",\"dns_record\":\"" + JsonFormatter::escape(dns_record) + "\"}}";
        } else {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Unknown mail domain sub-resource: " + JsonFormatter::escape(sub) + "\"}";
        }
        return r;
    });

    // GET /api/mail/domains — list all mail domains
    router_.add("GET", "/api/mail/domains", [&s, &mail_domain_json](const Request&) {
        Response r;
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;
        for (const auto& m : s.mail().list()) {
            if (!first) json << ",";
            first = false;
            json << mail_domain_json(m);
        }
        json << "]}";
        r.body = json.str();
        return r;
    });

    // POST /api/mail/domains — create a mail domain
    router_.add("POST", "/api/mail/domains", [&s, &mail_domain_json](const Request& req) {
        Response r;

        // Extract and normalize domain
        std::string domain_raw = json_extract(req.body, "domain");
        if (domain_raw.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Domain is required\"}";
            return r;
        }
        std::string domain = normalize_domain(domain_raw);

        if (!utils::Validator::is_valid_hostname(domain)) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid domain format\"}";
            return r;
        }

        // Parse mode (strict validation)
        std::string mode_str = json_extract(req.body, "mode");
        if (!mode_str.empty() && !mail::is_valid_mail_domain_mode(mode_str)) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid mail domain mode. Valid: disabled, local-primary, external-relay, split-m365\"}";
            return r;
        }

        // Parse domain_id (FK to ContainerCP Domain). 0 = external domain.
        std::string domain_id_str = json_extract(req.body, "domain_id");
        uint64_t domain_id = 0;
        uint64_t site_id = 0;
        std::string resolved_domain = domain;
        if (!domain_id_str.empty()) {
            try { domain_id = std::stoull(domain_id_str); }
            catch (...) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Invalid domain_id\"}";
                return r;
            }
            if (domain_id != 0) {
                auto* dom = s.domains().find(domain_id);
                if (!dom) {
                    r.status_code = 400;
                    r.body = "{\"success\":false,\"error\":\"Domain not found\"}";
                    return r;
                }
                site_id = dom->site_id;
                resolved_domain = dom->fqdn;
            }
        }

        mail::MailDomainMode mode = mail::mail_domain_mode_from_string(mode_str);

        // Parse optional relay_host
        std::string relay_host = json_extract(req.body, "relay_host");
        if (relay_host == "null") relay_host = "";
        if (!relay_host.empty()) {
            bool valid = true;
            for (char c : relay_host) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != ':') {
                    valid = false; break;
                }
            }
            if (!valid) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Invalid relay_host format\"}";
                return r;
            }
        }

        uint64_t id = s.mail().create(resolved_domain, mode, domain_id, site_id, relay_host);
        if (id == 0) {
            // Check if the failure was due to validation or duplicate
            std::string vr = mail::MailDomainManager::validate_mode_relay(mode, relay_host);
            if (!vr.empty()) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(vr) + "\"}";
            } else {
                r.status_code = 409;
                r.body = "{\"success\":false,\"error\":\"Domain already exists\"}";
            }
            return r;
        }

        s.save();
        (void)s.runtime_sync().sync("mail");

        auto* created = s.mail().find(id);
        if (created) {
            r.body = "{\"success\":true,\"data\":" + mail_domain_json(*created) + "}";
        } else {
            r.body = "{\"success\":true,\"data\":{\"id\":" + std::to_string(id) + "}}";
        }
        return r;
    });

    // PATCH /api/mail/domains/<id> — update a mail domain
    router_.add_prefix("PATCH", "/api/mail/domains/", [&s, &mail_domain_json](const Request& req) {
        Response r;
        std::string id_str = req.path.substr(std::string("/api/mail/domains/").size());
        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {}
        if (id == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid mail domain ID\"}";
            return r;
        }

        auto* m = s.mail().find(id);
        if (!m) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Mail domain not found\"}";
            return r;
        }

        // Extract optional fields — only update those present in request.
        // Use json_has_key to distinguish omitted vs explicitly set to empty/null.
        if (json_has_key(req.body, "mode")) {
            std::string mode_str = json_extract(req.body, "mode");
            if (!mail::is_valid_mail_domain_mode(mode_str)) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Invalid mail domain mode. Valid: disabled, local-primary, external-relay, split-m365\"}";
                return r;
            }
            m->mode = mail::mail_domain_mode_from_string(mode_str);
        }

        if (json_has_key(req.body, "enabled")) {
            std::string enabled_str = json_extract(req.body, "enabled");
            if (enabled_str != "true" && enabled_str != "false") {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"enabled must be true or false\"}";
                return r;
            }
            m->enabled = (enabled_str == "true");
        }

        if (json_has_key(req.body, "relay_host")) {
            std::string val = json_extract(req.body, "relay_host");
            // JSON null becomes "null" — treat as clear
            val = (val == "null") ? "" : val;
            if (!val.empty()) {
                for (char c : val) {
                    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != ':') {
                        r.status_code = 400;
                        r.body = "{\"success\":false,\"error\":\"Invalid relay_host format\"}";
                        return r;
                    }
                }
            }
            m->relay_host = val;
        }

        if (json_has_key(req.body, "max_mailboxes")) {
            std::string max_mb = json_extract(req.body, "max_mailboxes");
            try { m->max_mailboxes = max_mb.empty() ? 0 : std::stoull(max_mb); }
            catch (...) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Invalid max_mailboxes\"}";
                return r;
            }
        }

        if (json_has_key(req.body, "max_aliases")) {
            std::string max_al = json_extract(req.body, "max_aliases");
            try { m->max_aliases = max_al.empty() ? 0 : std::stoull(max_al); }
            catch (...) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Invalid max_aliases\"}";
                return r;
            }
        }

        if (json_has_key(req.body, "catch_all")) {
            std::string val = json_extract(req.body, "catch_all");
            m->catch_all = (val == "null") ? "" : val;
        }

        // Validate mode+relay combination after all fields updated
        std::string vr = mail::MailDomainManager::validate_mode_relay(m->mode, m->relay_host);
        if (!vr.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(vr) + "\"}";
            return r;
        }

        m->updated_at = ssl::CertificateStore::timestamp_utc();
        s.save();
        (void)s.runtime_sync().sync("mail");

        r.body = "{\"success\":true,\"data\":" + mail_domain_json(*m) + "}";
        return r;
    });

    // DELETE /api/mail/domains/<id> — remove a mail domain
    router_.add_prefix("DELETE", "/api/mail/domains/", [&s](const Request& req) {
        Response r;
        std::string id_str = req.path.substr(std::string("/api/mail/domains/").size());
        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {}
        if (id == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid ID\"}";
            return r;
        }

        auto* m = s.mail().find(id);
        if (!m) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Mail domain not found\"}";
            return r;
        }

        s.mail().remove(id);
        s.save();
        (void)s.runtime_sync().sync("mail");
        r.body = "{\"success\":true,\"data\":{\"message\":\"Mail domain removed\"}}";
        return r;
    });

    // ── Mailbox routes (PATCH, POST/password, DELETE) ──────────────────
    // PATCH /api/mail/mailboxes/<id> — update a mailbox
    router_.add_prefix("PATCH", "/api/mail/mailboxes/", [&s, &mailbox_json, &now_utc](const Request& req) {
        Response r;
        std::string id_str = req.path.substr(std::string("/api/mail/mailboxes/").size());
        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {}
        if (id == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid mailbox ID\"}";
            return r;
        }

        auto* mb = s.mailboxes().find(id);
        if (!mb) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Mailbox not found\"}";
            return r;
        }

        if (json_has_key(req.body, "enabled")) {
            std::string v = json_extract(req.body, "enabled");
            if (v != "true" && v != "false") {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"enabled must be true or false\"}";
                return r;
            }
            mb->enabled = (v == "true");
        }

        if (json_has_key(req.body, "quota_bytes")) {
            std::string v = json_extract(req.body, "quota_bytes");
            try { mb->quota_bytes = v.empty() ? 0 : std::stoull(v); }
            catch (...) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Invalid quota_bytes\"}";
                return r;
            }
        }

        if (json_has_key(req.body, "quota_messages")) {
            std::string v = json_extract(req.body, "quota_messages");
            try { mb->quota_messages = v.empty() ? 0 : std::stoull(v); }
            catch (...) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Invalid quota_messages\"}";
                return r;
            }
        }

        if (json_has_key(req.body, "display_name")) {
            std::string v = json_extract(req.body, "display_name");
            mb->display_name = (v == "null") ? "" : v;
        }

        if (json_has_key(req.body, "forward_to")) {
            std::string v = json_extract(req.body, "forward_to");
            mb->forward_to = (v == "null") ? "" : v;
        }

        if (json_has_key(req.body, "spam_enabled")) {
            std::string v = json_extract(req.body, "spam_enabled");
            if (v != "true" && v != "false") {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"spam_enabled must be true or false\"}";
                return r;
            }
            mb->spam_enabled = (v == "true");
        }

        mb->updated_at = now_utc();
        s.save();
        (void)s.runtime_sync().sync("mail");
        r.body = "{\"success\":true,\"data\":" + mailbox_json(*mb) + "}";
        return r;
    });

    // POST /api/mail/mailboxes/<id>/password — change mailbox password
    router_.add_prefix("POST", "/api/mail/mailboxes/", [&s, &now_utc](const Request& req) {
        Response r;
        std::string remaining = req.path.substr(std::string("/api/mail/mailboxes/").size());
        // remaining = "<id>/password"
        auto slash = remaining.find('/');
        std::string id_str = (slash != std::string::npos) ? remaining.substr(0, slash) : remaining;
        std::string sub = (slash != std::string::npos) ? remaining.substr(slash + 1) : "";

        if (sub != "password") {
            r.status_code = 404;
            return r;
        }

        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {}
        if (id == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid mailbox ID\"}";
            return r;
        }

        auto* mb = s.mailboxes().find(id);
        if (!mb) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Mailbox not found\"}";
            return r;
        }

        std::string password = json_extract(req.body, "password");
        if (password.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"password is required\"}";
            return r;
        }
        if (password.size() < 3) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Password must be at least 3 characters\"}";
            return r;
        }

        std::string hash = mail::MailPasswordHasher::hash(password);
        if (hash.empty()) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Failed to hash password\"}";
            return r;
        }

        mb->password_hash = hash;
        mb->updated_at = now_utc();
        s.save();
        (void)s.runtime_sync().sync("mail");
        r.body = "{\"success\":true,\"data\":{\"message\":\"Password changed\"}}";
        return r;
    });

    // DELETE /api/mail/mailboxes/<id> — remove a mailbox
    router_.add_prefix("DELETE", "/api/mail/mailboxes/", [&s](const Request& req) {
        Response r;
        std::string id_str = req.path.substr(std::string("/api/mail/mailboxes/").size());
        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {}
        if (id == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid mailbox ID\"}";
            return r;
        }

        if (!s.mailboxes().find(id)) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Mailbox not found\"}";
            return r;
        }

        s.mailboxes().remove(id);
        s.save();
        (void)s.runtime_sync().sync("mail");
        r.body = "{\"success\":true,\"data\":{\"message\":\"Mailbox removed\"}}";
        return r;
    });

    // DELETE /api/mail/aliases/<id> — remove an alias
    router_.add_prefix("DELETE", "/api/mail/aliases/", [&s](const Request& req) {
        Response r;
        std::string id_str = req.path.substr(std::string("/api/mail/aliases/").size());
        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {}
        if (id == 0) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"Invalid alias ID\"}"; return r; }
        if (!s.mail_aliases().find(id)) { r.status_code = 404; r.body = "{\"success\":false,\"error\":\"Alias not found\"}"; return r; }
        s.mail_aliases().remove(id);
        s.save();
        (void)s.runtime_sync().sync("mail");
        r.body = "{\"success\":true,\"data\":{\"message\":\"Alias removed\"}}";
        return r;
    });

    // PATCH /api/mail/aliases/<id> — update an alias
    router_.add_prefix("PATCH", "/api/mail/aliases/", [&s, &now_utc](const Request& req) {
        Response r;
        std::string id_str = req.path.substr(std::string("/api/mail/aliases/").size());
        uint64_t id = 0;
        try { id = std::stoull(id_str); } catch (...) {}
        if (id == 0) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"Invalid alias ID\"}"; return r; }
        auto* a = s.mail_aliases().find(id);
        if (!a) { r.status_code = 404; r.body = "{\"success\":false,\"error\":\"Alias not found\"}"; return r; }
        if (json_has_key(req.body, "destination")) {
            std::string v = json_extract(req.body, "destination");
            // JSON null → reject (alias without destination has no meaning)
            if (v == "null") { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"destination cannot be null\"}"; return r; }
            if (v.empty()) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"destination is required\"}"; return r; }
            if (v.find('@') == std::string::npos) { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"destination must be a valid email address\"}"; return r; }
            a->destination = v;
        }
        if (json_has_key(req.body, "enabled")) {
            std::string v = json_extract(req.body, "enabled");
            if (v != "true" && v != "false") { r.status_code = 400; r.body = "{\"success\":false,\"error\":\"enabled must be true or false\"}"; return r; }
            a->enabled = (v == "true");
        }
        a->updated_at = now_utc();
        s.save();
        (void)s.runtime_sync().sync("mail");
        std::ostringstream json;
        json << "{\"success\":true,\"data\":{"
             << "\"id\":" << a->id
             << ",\"domain_id\":" << a->domain_id
             << ",\"source\":\"" << JsonFormatter::escape(a->source_local_part)
             << "\",\"destination\":\"" << JsonFormatter::escape(a->destination)
             << "\",\"enabled\":" << (a->enabled ? "true" : "false")
             << "}}";
        r.body = json.str();
        return r;
    });

    // ── Mail Module Status API ──────────────────────────────────────
    // GET /api/mail/status — query mail module state
    router_.add("GET", "/api/mail/status", [&s](const Request&) {
        Response r;
        auto state = s.mail().module_state();
        std::string state_str = mail::mail_module_state_to_string(state);
        std::ostringstream js;
        js << "{\"success\":true,\"data\":{"
           << "\"module\":\"mail\""
           << ",\"state\":\"" << JsonFormatter::escape(state_str) << "\""
           << ",\"domains\":" << s.mail().list().size()
           << ",\"mailboxes\":" << s.mailboxes().list().size()
           << ",\"aliases\":" << s.mail_aliases().list().size()
           << "}}";
        r.body = js.str();
        return r;
    });

    // GET /api/mail/health — mail module health report
    // Serializes the generic HealthReport model (libs/runtime/).
    // Response evolves with the HealthReport struct — no Mail-specific
    // JSON construction here.
    router_.add("GET", "/api/mail/health", [&s](const Request&) {
        Response r;
        auto report = s.health().check("mail");

        std::ostringstream js;
        js << "{\"success\":true,\"data\":{"
           << "\"status\":\"" << JsonFormatter::escape(report.status) << "\"";

        // Services array
        if (!report.services.empty()) {
            js << ",\"services\":[";
            bool first_svc = true;
            for (const auto& svc : report.services) {
                if (!first_svc) js << ",";
                first_svc = false;
                js << "{\"name\":\"" << JsonFormatter::escape(svc.name) << "\""
                   << ",\"status\":\"" << JsonFormatter::escape(svc.status) << "\""
                   << ",\"message\":\"" << JsonFormatter::escape(svc.message) << "\"}";
            }
            js << "]";
        }

        // Module details (arbitrary JSON from the health check)
        if (!report.details.empty()) {
            js << ",\"details\":" << report.details;
        }

        js << "}}";
        r.body = js.str();
        return r;
    });

    // POST /api/mail/activate — activate mail module
    router_.add("POST", "/api/mail/activate", [&s](const Request&) {
        Response r;
        if (s.mail().module_state() == mail::MailModuleState::Active) {
            r.body = "{\"success\":true,\"data\":{\"message\":\"Mail module already active\"}}";
            return r;
        }
        // Prepare runtime environment (directories, network) first
        auto env = s.mail_provider().prepare_environment();
        if (!env.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Environment failed: " + JsonFormatter::escape(env.message) + "\"}";
            return r;
        }
        // Then write configuration files
        auto cfg = s.mail_provider().write_configs(
            s.mail().list(), s.mailboxes(), s.mail_aliases());
        if (!cfg.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Config failed: " + JsonFormatter::escape(cfg.message) + "\"}";
            return r;
        }
        // Start containers
        auto start_res = s.mail_provider().start();
        if (!start_res.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Start failed: " + JsonFormatter::escape(start_res.message) + "\"}";
            return r;
        }
        s.mail().set_module_state(mail::MailModuleState::Active);
        s.save();
        r.body = "{\"success\":true,\"data\":{\"message\":\"Mail module activated\"}}";
        return r;
    });

    // POST /api/mail/deactivate — deactivate mail module
    router_.add("POST", "/api/mail/deactivate", [&s](const Request&) {
        Response r;
        if (s.mail().module_state() == mail::MailModuleState::Inactive) {
            r.body = "{\"success\":true,\"data\":{\"message\":\"Mail module already inactive\"}}";
            return r;
        }
        auto stop = s.mail_provider().stop();
        if (!stop.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Stop failed: " + JsonFormatter::escape(stop.message) + "\"}";
            return r;
        }
        s.mail().set_module_state(mail::MailModuleState::Inactive);
        s.save();
        r.body = "{\"success\":true,\"data\":{\"message\":\"Mail module deactivated\"}}";
        return r;
    });

    // POST /api/mail/regenerate — regenerate mail config
    router_.add("POST", "/api/mail/regenerate", [&s](const Request&) {
        Response r;
        if (s.mail().module_state() != mail::MailModuleState::Active) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Mail module is not active\"}";
            return r;
        }
        auto result = s.mail_provider().apply_config(
            s.mail().list(), s.mailboxes(), s.mail_aliases());
        if (!result.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Config apply failed at stage '"
                + JsonFormatter::escape(result.failed_stage) + "': "
                + JsonFormatter::escape(result.message) + "\"}";
            return r;
        }
        r.body = "{\"success\":true,\"data\":{\"message\":\"Configuration regenerated and reloaded\"}}";
        return r;
    });

    // POST /api/mail/reload — reload Postfix/Dovecot config without full restart
    router_.add("POST", "/api/mail/reload", [&s](const Request&) {
        Response r;
        if (s.mail().module_state() != mail::MailModuleState::Active) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Mail module is not active\"}";
            return r;
        }
        auto result = s.mail_provider().reload();
        if (!result.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(result.message) + "\"}";
            return r;
        }
        r.body = "{\"success\":true,\"data\":{\"message\":\"Mail configuration reloaded\"}}";
        return r;
    });

    // POST /api/mail/recover — full restart of mail containers
    router_.add("POST", "/api/mail/recover", [&s](const Request&) {
        Response r;
        if (s.mail().module_state() != mail::MailModuleState::Active) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Mail module is not active\"}";
            return r;
        }
        auto regen = s.mail_provider().apply_config(
            s.mail().list(), s.mailboxes(), s.mail_aliases());
        if (!regen.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Config regeneration failed: "
                + JsonFormatter::escape(regen.message) + "\"}";
            return r;
        }
        auto stop = s.mail_provider().stop();
        if (!stop.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Stop failed: " + JsonFormatter::escape(stop.message) + "\"}";
            return r;
        }
        auto start = s.mail_provider().start();
        if (!start.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Start failed: " + JsonFormatter::escape(start.message) + "\"}";
            return r;
        }
        r.body = "{\"success\":true,\"data\":{\"message\":\"Mail stack recovered and restarted\"}}";
        return r;
    });

    // GET /api/mail/smarthost — get smarthost config
    router_.add("GET", "/api/mail/smarthost", [&s](const Request&) {
        Response r;
        auto& sc = s.mail().smarthost();
        std::ostringstream js;
        js << "{\"success\":true,\"data\":{"
           << "\"enabled\":" << (sc.enabled ? "true" : "false")
           << ",\"host\":\"" << JsonFormatter::escape(sc.host) << "\""
           << ",\"port\":" << sc.port
           << ",\"username\":\"" << JsonFormatter::escape(sc.username) << "\""
           << ",\"password_set\":" << (sc.password.empty() ? "false" : "true")
           << "}}";
        r.body = js.str();
        return r;
    });

    // POST /api/mail/smarthost — set smarthost config
    router_.add("POST", "/api/mail/smarthost", [&s](const Request& req) {
        Response r;
        mail::MailDomainManager::SmarthostConfig cfg;
        cfg.enabled = json_extract(req.body, "enabled") == "true";

        std::string host = json_extract(req.body, "host");
        if (host.empty() && cfg.enabled) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"host is required when enabled\"}";
            return r;
        }
        cfg.host = host;

        std::string port_str = json_extract(req.body, "port");
        if (!port_str.empty()) {
            try { cfg.port = std::stoi(port_str); }
            catch (...) { cfg.port = 587; }
        }

        cfg.username = json_extract(req.body, "username");
        std::string password = json_extract(req.body, "password");
        if (!password.empty() && password != "null") {
            cfg.password = password;
        } else {
            // Keep existing password if not provided in update
            cfg.password = s.mail().smarthost().password;
        }

        s.mail().set_smarthost(cfg);
        s.mail_provider().set_smarthost(cfg.host, cfg.port, cfg.username, cfg.password);
        s.save();

        // Regenerate config and reload
        auto result = s.mail_provider().apply_config(
            s.mail().list(), s.mailboxes(), s.mail_aliases());
        if (!result.success) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Config apply failed: "
                + JsonFormatter::escape(result.message) + "\"}";
            return r;
        }

        r.body = "{\"success\":true,\"data\":{\"message\":\"Smarthost configured\"}}";
        return r;
    });

    // ── Migration API (myVestaCP import, read-only) ────────────────────
    router_.add("GET", "/api/migration/vesta/backups", [&s](const Request&) {
        Response r;
        std::vector<std::string> allowed_dirs = {"/backup", s.config().data_root() + "/backups"};
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;

        for (const auto& dir : allowed_dirs) {
            std::string list_cmd = "ls -1 " + dir + "/*.tar 2>/dev/null";
            std::array<char, 4096> buf;
            std::string result;
            auto pipe_cmd = "ls -1 \"" + dir + "\"/*.tar 2>/dev/null; ls -1 \"" + dir + "\"/*.tar.gz 2>/dev/null";
            FILE* pipe_fp = ::popen(pipe_cmd.c_str(), "r");
            if (pipe_fp) {
                // Read pipe output
                while (::fgets(buf.data(), buf.size(), pipe_fp) != nullptr) {
                    result += buf.data();
                }
                ::pclose(pipe_fp);
            }

            std::istringstream stream(result);
            std::string fname;
            while (std::getline(stream, fname)) {
                if (fname.empty()) continue;
                // Resolve canonical path
                char real[PATH_MAX];
                if (!::realpath(fname.c_str(), real)) continue;
                std::string canon(real);
                // Verify it's in an allowed directory
                bool allowed = false;
                for (const auto& ad : allowed_dirs) {
                    char ad_real[PATH_MAX];
                    if (::realpath(ad.c_str(), ad_real) && canon.substr(0, strlen(ad_real)) == ad_real) {
                        allowed = true;
                        break;
                    }
                }
                if (!allowed) continue;
                // Get size and mtime
                struct stat st;
                if (::stat(real, &st) != 0 || !S_ISREG(st.st_mode)) continue;

                // Extract basename only
                std::string basename = fname;
                auto slash = basename.rfind('/');
                if (slash != std::string::npos) basename = basename.substr(slash + 1);

                if (!first) json << ",";
                first = false;
                json << "{\"name\":\"" << JsonFormatter::escape(basename)
                     << "\",\"size\":" << st.st_size
                     << ",\"mtime\":" << st.st_mtime
                     << "}";
            }
        }

        json << "]}";
        r.body = json.str();
        return r;
    });

    router_.add("POST", "/api/migration/vesta/inspect", [&s](const Request& req) {
        Response r;

        // Parse body
        std::string backup, domain, owner, database;
        bool skip_db = false, keep_staging = false;

        auto extract = [&](const std::string& name) -> std::string {
            auto pos = req.body.find("\"" + name + "\":\"");
            if (pos == std::string::npos) {
                pos = req.body.find("\"" + name + "\": \"");
                if (pos == std::string::npos) return "";
            }
            auto start = req.body.find('"', pos + name.size() + 3);
            if (start == std::string::npos) return "";
            auto end = req.body.find('"', start + 1);
            if (end == std::string::npos) return "";
            return req.body.substr(start + 1, end - start - 1);
        };
        auto extract_bool = [&](const std::string& name) -> bool {
            auto pos = req.body.find("\"" + name + "\":true");
            if (pos != std::string::npos) return true;
            pos = req.body.find("\"" + name + "\": true");
            return pos != std::string::npos;
        };

        backup = extract("backup");
        domain = extract("domain");
        owner = extract("owner");
        database = extract("database");
        skip_db = extract_bool("skip_db");
        keep_staging = extract_bool("keep_staging");

        if (backup.empty() || domain.empty() || owner.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"backup, domain and owner are required\"}";
            return r;
        }

        // Resolve backup path: check allowed directories only
        std::string resolved_path;
        std::vector<std::string> allowed_dirs = {"/backup", s.config().data_root() + "/backups"};
        for (const auto& dir : allowed_dirs) {
            std::string candidate = dir + "/" + backup;
            char real[PATH_MAX];
            if (::realpath(candidate.c_str(), real)) {
                std::string canon(real);
                char ad_real[PATH_MAX];
                if (::realpath(dir.c_str(), ad_real)) {
                    std::string ad(ad_real);
                    struct stat path_stat;
                    if (canon.substr(0, ad.size()) == ad && ::stat(real, &path_stat) == 0) {
                        resolved_path = canon;
                        break;
                    }
                }
            }
        }

        if (resolved_path.empty()) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Backup file not found in allowed directories\"}";
            return r;
        }

        // Run inspect
        runtime::CommandExecutor exec;
        migration::VestaSiteImporter importer(exec, s.filesystem(), s.config(),
                                              &s.sites(), &s.domains());
        migration::Options opts;
        opts.backup_path = resolved_path;
        opts.domain = domain;
        opts.owner = owner;
        opts.database = database;
        opts.skip_db = skip_db;
        opts.keep_staging = keep_staging;
        opts.dry_run = true;

        auto manifest = importer.inspect(opts);

        // Build JSON response
        std::ostringstream json;
        json << "{"
             << "\"success\":true,"
             << "\"data\":{"
             << "\"domain_found\":" << (manifest.domain_found ? "true" : "false")
             << ",\"web_archive_path\":\"" << JsonFormatter::escape(manifest.web_archive_path)
             << "\",\"web_root_type\":\"" << JsonFormatter::escape(manifest.web_root_type)
             << "\",\"web_size_known\":" << (manifest.web_size_known ? "true" : "false")
             << ",\"web_size\":" << manifest.web_size
             << ",\"wp_config_found\":" << (manifest.wp_config_found ? "true" : "false")
             << ",\"wp_config_parsed\":" << (manifest.wp_config_parsed ? "true" : "false")
             << ",\"wp_db_ambiguous\":" << (manifest.wp_db_ambiguous ? "true" : "false")
             << ",\"wp_db_name\":\"" << JsonFormatter::escape(manifest.wp_db_name)
             << "\",\"wp_db_user\":\"" << JsonFormatter::escape(manifest.wp_db_user)
             << "\",\"wp_db_host\":\"" << JsonFormatter::escape(manifest.wp_db_host)
             << "\",\"db_dump_found\":" << (manifest.db_dump_found ? "true" : "false")
             << ",\"db_dump_path\":\"" << JsonFormatter::escape(manifest.db_dump_path)
             << "\",\"db_type\":\"" << JsonFormatter::escape(manifest.db_type)
             << "\",\"site_exists\":" << (manifest.site_exists ? "true" : "false")
             << ",\"available_disk_mb\":" << manifest.available_disk_mb
             << ",\"all_databases\":[";

        bool first_db = true;
        for (const auto& db : manifest.all_databases) {
            if (!first_db) json << ",";
            first_db = false;
            json << "\"" << JsonFormatter::escape(db) << "\"";
        }

        json << "],\"errors\":[";
        bool first_err = true;
        for (const auto& e : manifest.errors) {
            if (!first_err) json << ",";
            first_err = false;
            json << "\"" << JsonFormatter::escape(e) << "\"";
        }

        json << "],\"warnings\":[";
        bool first_warn = true;
        for (const auto& w : manifest.warnings) {
            if (!first_warn) json << ",";
            first_warn = false;
            json << "\"" << JsonFormatter::escape(w) << "\"";
        }

        json << "]}}";
        r.body = json.str();
        return r;
    });

    // Accept loop
    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) {
                services_.logger().error("ApiServer: Accept failed");
            }
            break;
        }
        handle_client(client_fd, this);
    }

    ::close(server_fd_);
    return true;
}

void ApiServer::stop() {
    running_ = false;
    ::shutdown(server_fd_, SHUT_RDWR);
}

Router& ApiServer::router() {
    return router_;
}

} // namespace containercp::api
