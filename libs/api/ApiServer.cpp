#include "ApiServer.h"
#include "api/JsonFormatter.h"
#include "api/SitesViewService.h"
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
#include "dns/DnsCheckHandler.h"
#include "dns/DnsCheckService.h"
#include "dns/SpfAnalyzer.h"
#include "migration/VestaSiteImporter.h"
#include "provider/DockerComposeProvider.h"
#include "utils/Validator.h"

#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

#include <mutex>

namespace containercp::api {

// Serialize conflicting proxy management operations (reload, sync, recover)
static std::mutex proxy_op_mutex;
static constexpr int kMaxApiRequestBodyBytes = 6 * 1024 * 1024;

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

static std::string json_extract_string_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return "";
    ++pos;

    std::string out;
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') break;
        if (c != '\\' || pos >= json.size()) {
            out += c;
            continue;
        }
        char esc = json[pos++];
        switch (esc) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            default: out += esc; break;
        }
    }
    return out;
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

// Simple URL percent-decoding for query parameter values.
// Decodes %XX hex sequences, %2C → comma, etc.
static std::string url_decode(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = {s[i+1], s[i+2], '\0'};
            char* end = nullptr;
            long val = std::strtol(hex, &end, 16);
            if (end == hex + 2) {
                result.push_back(static_cast<char>(val));
                i += 2;
                continue;
            }
        }
        result.push_back(s[i]);
    }
    return result;
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
    std::string raw(buf, static_cast<std::size_t>(n));
    auto header_end = raw.find("\r\n\r\n");
    if (header_end != std::string::npos) {
        auto header = raw.substr(0, header_end);
        auto cl_pos = header.find("Content-Length:");
        if (cl_pos != std::string::npos) {
            cl_pos += 15;
            while (cl_pos < header.size() && header[cl_pos] == ' ') ++cl_pos;
            auto cl_end = header.find("\r\n", cl_pos);
            int expected = 0;
            try { expected = std::stoi(header.substr(cl_pos, cl_end - cl_pos)); } catch (...) { expected = 0; }
            if (expected > 0 && expected <= kMaxApiRequestBodyBytes) {
                const std::size_t body_start = header_end + 4;
                while (raw.size() < body_start + static_cast<std::size_t>(expected)) {
                    ssize_t more = ::read(client_fd, buf, sizeof(buf));
                    if (more <= 0) break;
                    raw.append(buf, static_cast<std::size_t>(more));
                }
            }
        }
    }

    std::istringstream stream(raw);
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
                req.query[url_decode(line.substr(0, eq))] = url_decode(line.substr(eq + 1));
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
        const auto body_start = raw.find("\r\n\r\n");
        if (body_start != std::string::npos && content_length <= kMaxApiRequestBodyBytes) {
            const auto start = body_start + 4;
            if (raw.size() >= start) req.body = raw.substr(start, static_cast<std::size_t>(content_length));
        }
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

    auto issue_ssl_certificate = [&s](const std::string& domain,
                                      uint64_t site_id,
                                      const std::string& provider_id) -> containercp::core::OperationResult {
        auto providers = s.certificate_providers();
        auto provider_it = providers.find(provider_id);
        if (provider_it == providers.end() || !provider_it->second) {
            return {false, "Unknown provider: " + provider_id};
        }

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

        auto result = provider_it->second->request(domain);
        if (!result.success) {
            return result;
        }

        std::string cert_path = s.cert_store().fullchain_path(site_id);
        std::string key_path = s.cert_store().privkey_path(site_id);
        auto proxy_result = s.proxy_provider().attach_certificate(domain, cert_path, key_path);
        if (!proxy_result.success) {
            auto load_result = s.cert_store().load_metadata(site_id);
            if (load_result.success) {
                auto failed_meta = load_result.metadata;
                failed_meta.https_enabled = false;
                failed_meta.updated_at = containercp::ssl::CertificateStore::timestamp_utc();
                s.cert_store().save_metadata(site_id, failed_meta);
            }
            return {false, "Certificate issued but HTTPS enable failed: " + proxy_result.message};
        }

        return {true, "Certificate issued"};
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
        auto sites_json = api::build_enriched_sites_json(
            s.sites().list(),
            s.config().server_hostname(),
            s.reverse_proxies(),
            s.cert_store());
        r.body = "{\"success\":true,\"data\":" + sites_json + "}";
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

    // GET /api/domains/<domain>/dns-check — live DNS resolution for a domain
    // Uses prefix routing. Must be registered AFTER more specific domain routes.
    // Dispatch pattern: /api/domains/<domain>/dns-check → DNS check
    //                    everything else → 404 (safe for future routes registered before this)
    router_.add_prefix("GET", "/api/domains/", [&s](const Request& req) {
        return dns::handleDnsCheck(req, s.dns_check(), &s.network());
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

    // POST /api/wordpress/database-credentials/rotate — queue WordPress DB credential rotation
    router_.add("POST", "/api/wordpress/database-credentials/rotate", [&s](const Request& req) {
        Response r;
        struct ParsedId {
            bool valid = false;
            uint64_t value = 0;
        };
        auto parse_uint = [](const std::string& value) -> ParsedId {
            if (value.empty()) {
                return {};
            }
            for (unsigned char c : value) {
                if (!std::isdigit(c)) {
                    return {};
                }
            }
            try {
                size_t parsed = 0;
                const uint64_t id = std::stoull(value, &parsed);
                if (parsed != value.size()) {
                    return {};
                }
                return {true, id};
            } catch (...) {
                return {};
            }
        };

        const auto parsed_site_id = parse_uint(json_extract(req.body, "site_id"));
        const auto parsed_database_id = parse_uint(json_extract(req.body, "database_id"));
        if (!parsed_site_id.valid || !parsed_database_id.valid) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":{\"code\":\"invalid_identifier\",\"message\":\"Site id and database id must be valid numeric identifiers\"}}";
            return r;
        }

        database::DatabaseCredentialRotationJobRequest request;
        request.site_id = parsed_site_id.value;
        request.database_id = parsed_database_id.value;
        request.confirmation = json_extract(req.body, "confirmation");

        const auto credential_status = s.wordpress_database_credentials().resolve_site(request.site_id);
        if (!credential_status.target.available) {
            if (credential_status.target.status == "site_not_found" || credential_status.target.status == "database_target_missing") {
                r.status_code = 404;
            } else if (credential_status.target.status == "database_target_ambiguous") {
                r.status_code = 409;
            } else {
                r.status_code = 400;
            }
            r.body = "{\"success\":false,\"error\":{\"code\":\"" + JsonFormatter::escape(credential_status.target.status)
                + "\",\"message\":\"" + JsonFormatter::escape(credential_status.target.message) + "\"}}";
            return r;
        }
        if (credential_status.target.database_id != request.database_id) {
            r.status_code = 409;
            r.body = "{\"success\":false,\"error\":{\"code\":\"database_target_mismatch\",\"message\":\"Requested database does not match the WordPress credential target\"}}";
            return r;
        }

        const auto queued = s.database_credential_rotation_jobs().enqueue(request);
        if (!queued.success) {
            if (queued.code == "site_not_found" || queued.code == "database_not_found") {
                r.status_code = 404;
            } else if (queued.code == "rotation_already_running") {
                r.status_code = 409;
            } else if (queued.code == "queue_unavailable") {
                r.status_code = 503;
            } else {
                r.status_code = 400;
            }
            r.body = "{\"success\":false,\"error\":{\"code\":\"" + JsonFormatter::escape(queued.code)
                + "\",\"message\":\"" + JsonFormatter::escape(queued.message) + "\"}}";
            return r;
        }

        r.status_code = 202;
        r.body = "{\"success\":true,\"data\":{\"job_id\":" + std::to_string(queued.job_id)
            + ",\"status\":\"pending\",\"message\":\"Credential rotation queued\"}}";
        return r;
    });

    // GET /api/wordpress/database-credentials/status?site_id=N — public-safe WordPress credential status
    router_.add("GET", "/api/wordpress/database-credentials/status", [&s](const Request& req) {
        Response r;
        struct ParsedId {
            bool valid = false;
            uint64_t value = 0;
        };
        auto parse_uint = [](const std::string& value) -> ParsedId {
            if (value.empty()) {
                return {};
            }
            for (unsigned char c : value) {
                if (!std::isdigit(c)) {
                    return {};
                }
            }
            try {
                size_t parsed = 0;
                const uint64_t id = std::stoull(value, &parsed);
                if (parsed != value.size()) {
                    return {};
                }
                return {true, id};
            } catch (...) {
                return {};
            }
        };

        auto it = req.query.find("site_id");
        const auto parsed_site_id = it == req.query.end() ? ParsedId{} : parse_uint(it->second);
        if (!parsed_site_id.valid) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":{\"code\":\"invalid_identifier\",\"message\":\"Site id must be a valid numeric identifier\"}}";
            return r;
        }

        const auto credential_status = s.wordpress_database_credentials().resolve_site(parsed_site_id.value);
        const auto& view = credential_status.view;
        const auto& target = credential_status.target;

        std::ostringstream json;
        json << "{\"success\":true,\"data\":{"
             << "\"available\":" << (view.available ? "true" : "false")
             << ",\"site_id\":" << view.site_id
             << ",\"domain\":\"" << JsonFormatter::escape(view.domain) << "\""
             << ",\"status\":\"" << JsonFormatter::escape(view.status) << "\""
             << ",\"source\":\"" << JsonFormatter::escape(view.source) << "\""
             << ",\"mutability\":\"" << JsonFormatter::escape(view.mutability) << "\""
             << ",\"db_name\":\"" << JsonFormatter::escape(view.db_name) << "\""
             << ",\"db_user\":\"" << JsonFormatter::escape(view.db_user) << "\""
             << ",\"db_host\":\"" << JsonFormatter::escape(view.db_host) << "\""
             << ",\"db_password_present\":" << (view.db_password_present ? "true" : "false")
             << ",\"database_target_available\":" << (target.available ? "true" : "false")
             << ",\"database_id\":" << target.database_id
             << ",\"database_target_status\":\"" << JsonFormatter::escape(target.status) << "\""
             << ",\"database_target_message\":\"" << JsonFormatter::escape(target.message) << "\""
             << ",\"issues\":[";
        for (size_t i = 0; i < view.issues.size(); ++i) {
            if (i > 0) json << ",";
            const auto& issue = view.issues[i];
            json << "{\"severity\":\"" << JsonFormatter::escape(wordpress::config_issue_severity_to_string(issue.severity))
                 << "\",\"code\":\"" << JsonFormatter::escape(issue.code)
                 << "\",\"message\":\"" << JsonFormatter::escape(issue.message) << "\"}";
        }
        json << "]}}";
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
        r.body = "{\"success\":true,\"data\":" + s.backup_service().backups_json() + "}";
        return r;
    });

    router_.add_prefix("GET", "/api/backups/", [&s](const Request& req) {
        Response r;
        const std::string rest = req.path.substr(std::string("/api/backups/").size());
        const auto slash = rest.find('/');
        const std::string id_text = slash == std::string::npos ? rest : rest.substr(0, slash);
        uint64_t backup_id = 0;
        try { backup_id = std::stoull(id_text); } catch (...) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":{\"code\":\"backup_id_invalid\",\"message\":\"Backup id must be numeric\"}}";
            return r;
        }
        auto* backup = s.backups().find(backup_id);
        if (backup == nullptr) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":{\"code\":\"backup_not_found\",\"message\":\"Backup was not found\"}}";
            return r;
        }
        if (slash == std::string::npos) {
            r.body = "{\"success\":true,\"data\":" + s.backup_service().backup_json(*backup) + "}";
            return r;
        }
        const std::string action = rest.substr(slash + 1);
        if (action == "download") {
            auto download = s.backup_service().download_backup(backup_id);
            if (!download.success) {
                r.status_code = 404;
                r.body = "{\"success\":false,\"error\":{\"code\":\"" + JsonFormatter::escape(download.code) + "\",\"message\":\"" + JsonFormatter::escape(download.message) + "\"}}";
                return r;
            }
            r.content_type = download.content_type;
            r.headers["Content-Disposition"] = "attachment; filename=\"" + JsonFormatter::escape(download.filename) + "\"";
            r.body = std::move(download.body);
            return r;
        }
        r.status_code = 404;
        r.body = "{\"success\":false,\"error\":\"Not found\"}";
        return r;
    });

    router_.add("GET", "/api/databases", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(s.database_view().build_enriched_json());
        return r;
    });

    auto lifecycle_job_response = [](const database::DatabaseLifecycleJobResult& result) -> Response {
        Response r;
        if (!result.success) {
            r.status_code = (result.code == "site_not_found" || result.code == "database_not_found") ? 404 : 400;
            r.body = "{\"success\":false,\"error\":{\"code\":\"" + JsonFormatter::escape(result.code) +
                     "\",\"message\":\"" + JsonFormatter::escape(result.message) + "\"}}";
            return r;
        }
        r.status_code = result.job_id == 0 ? 200 : 202;
        std::ostringstream json;
        json << "{\"success\":true,\"data\":{"
             << "\"job_id\":" << result.job_id
             << ",\"operation\":\"" << JsonFormatter::escape(result.operation)
             << "\",\"database_id\":" << result.database_id
             << ",\"site_id\":" << result.site_id
             << ",\"status\":\"" << (result.job_id == 0 ? "completed" : "pending")
             << "\",\"status_url\":\"" << (result.job_id == 0 ? "" : "/api/jobs?id=" + std::to_string(result.job_id))
             << "\",\"message\":\"" << JsonFormatter::escape(result.message) << "\"}}";
        r.body = json.str();
        return r;
    };

    auto dump_job_response = [](const database::DatabaseDumpJobResult& result) -> Response {
        Response r;
        if (!result.success) {
            r.status_code = (result.code == "database_not_found" || result.code == "site_not_found" || result.code == "artifact_not_found") ? 404 : 400;
            r.body = "{\"success\":false,\"error\":{\"code\":\"" + JsonFormatter::escape(result.code) +
                     "\",\"message\":\"" + JsonFormatter::escape(result.message) + "\"}}";
            return r;
        }
        r.status_code = 202;
        std::ostringstream json;
        json << "{\"success\":true,\"data\":{"
             << "\"job_id\":" << result.job_id
             << ",\"operation\":\"" << JsonFormatter::escape(result.operation)
             << "\",\"database_id\":" << result.database_id
             << ",\"site_id\":" << result.site_id
             << ",\"artifact_id\":\"" << JsonFormatter::escape(result.artifact_id)
             << "\",\"status\":\"pending\""
             << ",\"status_url\":\"/api/jobs?id=" << result.job_id
             << "\",\"message\":\"" << JsonFormatter::escape(result.message) << "\"}}";
        r.body = json.str();
        return r;
    };

    auto backup_job_response = [](const backup::BackupJobResult& result) -> Response {
        Response r;
        if (!result.success) {
            r.status_code = (result.code == "backup_not_found" || result.code == "site_not_found") ? 404 : 400;
            r.body = "{\"success\":false,\"error\":{\"code\":\"" + JsonFormatter::escape(result.code) +
                     "\",\"message\":\"" + JsonFormatter::escape(result.message) + "\"}}";
            return r;
        }
        r.status_code = 202;
        std::ostringstream json;
        json << "{\"success\":true,\"data\":{";
        json << "\"job_id\":" << result.job_id
             << ",\"operation\":\"" << JsonFormatter::escape(result.operation)
             << "\",\"backup_id\":" << result.backup_id
             << ",\"site_id\":" << result.site_id
             << ",\"recovery_backup_id\":" << result.recovery_backup_id
             << ",\"status\":\"pending\""
             << ",\"status_url\":\"/api/jobs?id=" << result.job_id
             << "\",\"message\":\"" << JsonFormatter::escape(result.message) << "\"}}";
        r.body = json.str();
        return r;
    };

    router_.add("POST", "/api/databases", [&s, &lifecycle_job_response](const Request& req) {
        const auto site_id_text = json_extract(req.body, "site_id");
        uint64_t site_id = 0;
        try {
            site_id = std::stoull(site_id_text);
        } catch (...) {
            Response r;
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"site_id is required\"}";
            return r;
        }
        const auto result = s.database_lifecycle_jobs().enqueue_create(site_id,
                                                                       json_extract(req.body, "database_name"),
                                                                       json_extract(req.body, "database_user"));
        return lifecycle_job_response(result);
    });

    router_.add_prefix("POST", "/api/databases/", [&s, &lifecycle_job_response, &dump_job_response](const Request& req) {
        Response r;
        const std::string rest = req.path.substr(std::string("/api/databases/").size());
        if (rest == "remove") {
            const std::string name = json_extract(req.body, "name");
            auto* db = s.databases().find(name);
            if (!db) {
                r.body = "{\"success\":false,\"error\":\"Not found\"}";
                return r;
            }
            s.logger().warning("AUDIT", "database_lifecycle operation=legacy-remove stage=requested result=deprecated_metadata_only database_id=" + std::to_string(db->id) + " site_id=" + std::to_string(db->site_id));
            s.databases().remove(db->id);
            s.save();
            r.body = "{\"success\":true,\"data\":{\"message\":\"Database metadata removed; physical MariaDB objects were not dropped\",\"deprecated\":true,\"physical_drop\":false}}";
            return r;
        }
        const auto slash = rest.find('/');
        if (slash == std::string::npos || slash == 0 || slash + 1 >= rest.size()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Database action path is invalid\"}";
            return r;
        }
        const std::string id_text = rest.substr(0, slash);
        const std::string action = rest.substr(slash + 1);
        uint64_t database_id = 0;
        try {
            database_id = std::stoull(id_text);
        } catch (...) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Database id must be numeric\"}";
            return r;
        }
        if (action.rfind("exports/", 0) == 0 && action.size() > std::string("exports//revoke").size()) {
            const std::string prefix = "exports/";
            const std::string suffix = "/revoke";
            if (action.substr(action.size() - suffix.size()) == suffix) {
                const std::string artifact_id = action.substr(prefix.size(), action.size() - prefix.size() - suffix.size());
                const auto revoked = s.database_dump().revokeArtifact(database_id, artifact_id);
                if (!revoked.success) {
                    r.status_code = 404;
                    r.body = "{\"success\":false,\"error\":{\"code\":\"" + JsonFormatter::escape(revoked.code) +
                             "\",\"message\":\"" + JsonFormatter::escape(revoked.message) + "\"}}";
                    return r;
                }
                r.body = "{\"success\":true,\"data\":{\"artifact_id\":\"" + JsonFormatter::escape(revoked.artifact_id) + "\",\"message\":\"Artifact revoked\"}}";
                return r;
            }
        }
        if (action == "verify") {
            return lifecycle_job_response(s.database_lifecycle_jobs().enqueue_verify(database_id));
        }
        if (action == "export") {
            return dump_job_response(s.database_dump_jobs().enqueue_export(database_id));
        }
        if (action == "import-upload") {
            const auto filename_it = req.query.find("filename");
            const std::string filename = filename_it == req.query.end() ? "upload.sql" : filename_it->second;
            const auto staged = s.database_dump().stageImportUpload(database_id, filename, req.body);
            Response upload;
            if (!staged.success) {
                upload.status_code = staged.code == "database_not_found" ? 404 : 400;
                upload.body = "{\"success\":false,\"error\":{\"code\":\"" + JsonFormatter::escape(staged.code) +
                              "\",\"message\":\"" + JsonFormatter::escape(staged.message) + "\"}}";
                return upload;
            }
            upload.status_code = 201;
            upload.body = "{\"success\":true,\"data\":{\"artifact_id\":\"" + JsonFormatter::escape(staged.artifact_id) +
                          "\",\"database_id\":" + std::to_string(staged.database_id) +
                          ",\"site_id\":" + std::to_string(staged.site_id) +
                          ",\"message\":\"Import upload staged\"}}";
            return upload;
        }
        if (action == "import") {
            return dump_job_response(s.database_dump_jobs().enqueue_import(database_id, json_extract(req.body, "artifact_id"), json_extract(req.body, "confirmation")));
        }
        if (action == "drop") {
            return lifecycle_job_response(s.database_lifecycle_jobs().enqueue_drop(database_id, json_extract(req.body, "confirmation")));
        }
        if (action == "forget-metadata") {
            return lifecycle_job_response(s.database_lifecycle_jobs().forget_metadata(database_id, json_extract(req.body, "confirmation")));
        }
        r.status_code = 404;
        r.body = "{\"success\":false,\"error\":\"Database action not found\"}";
        return r;
    });

    router_.add_prefix("GET", "/api/databases/", [&s](const Request& req) {
        const std::string rest = req.path.substr(std::string("/api/databases/").size());
        const auto marker = rest.find("/exports/");
        if (marker == std::string::npos) {
            Response r;
            if (rest.empty()) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Database id is required\"}";
                return r;
            }
            for (unsigned char c : rest) {
                if (!std::isdigit(c)) {
                    r.status_code = 400;
                    r.body = "{\"success\":false,\"error\":\"Database id must be numeric\"}";
                    return r;
                }
            }
            uint64_t database_id = 0;
            try {
                size_t parsed = 0;
                database_id = std::stoull(rest, &parsed);
                if (parsed != rest.size()) throw std::invalid_argument("trailing data");
            } catch (...) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Database id must be numeric\"}";
                return r;
            }
            const auto json = s.database_view().build_enriched_json(database_id);
            if (json == "null") {
                r.status_code = 404;
                r.body = "{\"success\":false,\"error\":\"Database not found\"}";
                return r;
            }
            r.body = JsonFormatter::success(json);
            return r;
        }
        uint64_t database_id = 0;
        try { database_id = std::stoull(rest.substr(0, marker)); } catch (...) {
            Response r;
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Database id must be numeric\"}";
            return r;
        }
        std::string tail = rest.substr(marker + std::string("/exports/").size());
        bool download = false;
        const std::string suffix = "/download";
        if (tail.size() > suffix.size() && tail.substr(tail.size() - suffix.size()) == suffix) {
            download = true;
            tail = tail.substr(0, tail.size() - suffix.size());
        }
        if (!database::database_artifact_id_valid(tail)) {
            Response r;
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Artifact not found\"}";
            return r;
        }
        const auto meta = s.database_dump().artifact(database_id, tail);
        if (!meta) {
            Response r;
            r.status_code = 410;
            r.body = "{\"success\":false,\"error\":{\"code\":\"artifact_unavailable\",\"message\":\"Artifact is unavailable, expired, or revoked\"}}";
            return r;
        }
        if (!download) {
            Response r;
            r.body = JsonFormatter::success(database::database_artifact_metadata_json(*meta));
            return r;
        }
        const auto path = s.database_dump().artifact_path(database_id, tail);
        if (!path) {
            Response r;
            r.status_code = 410;
            r.body = "{\"success\":false,\"error\":{\"code\":\"artifact_unavailable\",\"message\":\"Artifact is unavailable, expired, or revoked\"}}";
            return r;
        }
        std::ifstream file(*path, std::ios::binary);
        if (!file.is_open()) {
            Response r;
            r.status_code = 410;
            r.body = "{\"success\":false,\"error\":{\"code\":\"artifact_unavailable\",\"message\":\"Artifact is unavailable\"}}";
            return r;
        }
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        s.database_dump().record_download(database_id, tail);
        Response r;
        r.content_type = "application/sql";
        r.body = std::move(content);
        r.headers["Content-Disposition"] = "attachment; filename=\"" + meta->sanitized_filename + "\"";
        r.headers["X-Content-Type-Options"] = "nosniff";
        return r;
    });

    router_.add_prefix("GET", "/api/databases/", [&s](const Request& req) {
        Response r;
        const std::string id_text = req.path.substr(std::string("/api/databases/").size());
        if (id_text.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Database id is required\"}";
            return r;
        }
        for (unsigned char c : id_text) {
            if (!std::isdigit(c)) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Database id must be numeric\"}";
                return r;
            }
        }
        uint64_t database_id = 0;
        try {
            size_t parsed = 0;
            database_id = std::stoull(id_text, &parsed);
            if (parsed != id_text.size()) {
                throw std::invalid_argument("trailing data");
            }
        } catch (...) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Database id must be numeric\"}";
            return r;
        }

        const auto json = s.database_view().build_enriched_json(database_id);
        if (json == "null") {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Database not found\"}";
            return r;
        }
        r.body = JsonFormatter::success(json);
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

    auto is_valid_template_name = [](const std::string& name) {
        if (name.empty() || name.size() > 80) return false;
        for (unsigned char c : name) {
            if (!std::isalnum(c) && c != '-' && c != '_') return false;
        }
        return true;
    };

    auto profile_default_count = [&s](const std::string& web_server, uint64_t except_id = 0) {
        int count = 0;
        for (const auto& p : s.profiles().list()) {
            if (p.id != except_id && p.type == profile::ProfileType::WEB_SERVER
                && p.web_server == web_server && p.default_profile) {
                ++count;
            }
        }
        return count;
    };

    router_.add("GET", "/api/profiles", [&s](const Request&) {
        Response r;
        auto& profiles = s.profiles().list();
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;
        for (const auto& p : profiles) {
            if (!first) json << ",";
            first = false;
            std::string content;
            if (!p.template_path.empty() && s.filesystem().exists(p.template_path)) {
                content = s.filesystem().read_file(p.template_path);
            }
            json << "{\"id\":" << p.id
                 << ",\"name\":\"" << JsonFormatter::escape(p.profile_name)
                 << "\",\"type\":\"" << profile::profile_type_to_string(p.type)
                 << "\",\"web_server\":\"" << JsonFormatter::escape(p.web_server)
                 << "\",\"description\":\"" << JsonFormatter::escape(p.description)
                 << "\",\"enabled\":" << (p.enabled ? "true" : "false")
                 << ",\"default\":" << (p.default_profile ? "true" : "false")
                 << ",\"content\":\"" << JsonFormatter::escape(content)
                 << "\"}";
        }
        json << "]}";
        r.body = json.str();
        return r;
    });

    // POST /api/profiles — create a new web server template
    router_.add("POST", "/api/profiles", [&s, &is_valid_template_name](const Request& req) {
        Response r;
        std::string name = json_extract_string_value(req.body, "name");
        std::string web_server = json_extract_string_value(req.body, "web_server");
        std::string description = json_extract_string_value(req.body, "description");
        std::string content = json_extract_string_value(req.body, "content");
        std::string default_str = json_extract(req.body, "default");

        if (name.empty() || content.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"name and content are required\"}";
            return r;
        }
        if (!is_valid_template_name(name)) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Profile name may contain only ASCII letters, numbers, dash, and underscore\"}";
            return r;
        }
        if (web_server != "apache" && web_server != "nginx") {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"web_server must be 'apache' or 'nginx'\"}";
            return r;
        }

        // Check for duplicate name
        if (s.profiles().find(name) != nullptr) {
            r.status_code = 409;
            r.body = "{\"success\":false,\"error\":\"Profile name already exists: " + JsonFormatter::escape(name) + "\"}";
            return r;
        }

        // Write template file to disk
        std::string path = s.config().web_templates_dir() + name + ".conf.template";
        s.filesystem().create_directory(s.config().web_templates_dir());
        s.filesystem().create_file(path, content);

        bool is_default = (default_str == "true");
        if (is_default) {
            // Unset the existing default only for the selected backend.
            auto all = s.profiles().list();
            for (auto& p : all) {
                if (p.default_profile && p.type == profile::ProfileType::WEB_SERVER
                    && p.web_server == web_server) {
                    p.default_profile = false;
                }
            }
            s.profiles().set_profiles(all);
        }

        uint64_t id = s.profiles().create(name, profile::ProfileType::WEB_SERVER,
                                          web_server, path, description, is_default);
        s.save();

        r.body = "{\"success\":true,\"data\":{\"id\":" + std::to_string(id)
                 + ",\"name\":\"" + JsonFormatter::escape(name) + "\"}}";
        return r;
    });

    // POST /api/profiles/<id> — update an existing template
    router_.add_prefix("POST", "/api/profiles/", [&s, &is_valid_template_name, &profile_default_count](const Request& req) {
        Response r;
        std::string rest = req.path.substr(std::string("/api/profiles/").size());
        if (rest.empty() || rest.find_first_not_of("0123456789") != std::string::npos) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Profile id must be numeric\"}";
            return r;
        }
        uint64_t id = std::stoull(rest);
        auto* prof = s.profiles().find(id);
        if (prof == nullptr) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Profile not found\"}";
            return r;
        }

        std::string name = json_extract_string_value(req.body, "name");
        std::string web_server = json_extract_string_value(req.body, "web_server");
        std::string description = json_extract_string_value(req.body, "description");
        std::string content = json_extract_string_value(req.body, "content");
        std::string default_str = json_extract(req.body, "default");
        bool has_content = json_has_key(req.body, "content");
        bool has_name = json_has_key(req.body, "name");
        bool has_web_server = json_has_key(req.body, "web_server");
        bool has_description = json_has_key(req.body, "description");
        bool has_default = json_has_key(req.body, "default");

        if (has_web_server && web_server != "apache" && web_server != "nginx") {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"web_server must be 'apache' or 'nginx'\"}";
            return r;
        }
        if (has_name && !name.empty() && name != prof->profile_name) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Profile name cannot be changed; clone the template instead\"}";
            return r;
        }
        if (has_name && !name.empty() && !is_valid_template_name(name)) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Profile name may contain only ASCII letters, numbers, dash, and underscore\"}";
            return r;
        }
        if (has_web_server && prof->default_profile && profile_default_count(prof->web_server, id) == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Cannot move the last default template for its backend\"}";
            return r;
        }
        if (has_default && default_str != "true" && prof->default_profile
            && profile_default_count(prof->web_server, id) == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Cannot unset the last default template for its backend\"}";
            return r;
        }

        // Build updated profile — create a new list entry since we can't modify in-place through the manager
        auto all = s.profiles().list();
        for (auto& p : all) {
            if (p.id != id) continue;

            if (has_name && !name.empty()) {
                p.profile_name = name;
                p.name = name;
            }
            if (has_web_server) p.web_server = web_server;
            if (has_description) p.description = description;

            if (has_content) {
                p.template_path = s.config().web_templates_dir() + p.profile_name + ".conf.template";
                s.filesystem().create_directory(s.config().web_templates_dir());
                s.filesystem().create_file(p.template_path, content);
            }

            if (has_default && default_str == "true") {
                // Unset other defaults for the selected backend only.
                for (auto& other : all) {
                    if (other.id != id && other.type == profile::ProfileType::WEB_SERVER
                        && other.web_server == p.web_server) {
                        other.default_profile = false;
                    }
                }
                p.default_profile = true;
            } else if (has_default && default_str != "true") {
                p.default_profile = false;
            }
            break;
        }
        s.profiles().set_profiles(all);
        s.save();

        r.body = "{\"success\":true,\"data\":{\"id\":" + std::to_string(id)
                 + ",\"message\":\"Profile updated\"}}";
        return r;
    });

    // DELETE /api/profiles/<id> — delete a template
    router_.add_prefix("DELETE", "/api/profiles/", [&s, &profile_default_count](const Request& req) {
        Response r;
        std::string rest = req.path.substr(std::string("/api/profiles/").size());
        if (rest.empty() || rest.find_first_not_of("0123456789") != std::string::npos) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Profile id must be numeric\"}";
            return r;
        }
        uint64_t id = std::stoull(rest);
        auto* prof = s.profiles().find(id);
        if (prof == nullptr) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Profile not found\"}";
            return r;
        }

        // Prevent deleting the last default for this backend.
        if (prof->default_profile && prof->type == profile::ProfileType::WEB_SERVER
            && profile_default_count(prof->web_server, id) == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Cannot delete the last default template for this backend\"}";
            return r;
        }

        // Remove disk file
        if (!prof->template_path.empty() && s.filesystem().exists(prof->template_path)) {
            std::filesystem::remove(prof->template_path);
        }

        s.profiles().remove(id);
        s.save();

        r.body = "{\"success\":true,\"data\":{\"message\":\"Profile deleted\"}}";
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
                 << "\",\"steps\":[";
            for (std::size_t i = 0; i < job->step_details.size(); ++i) {
                const auto& step = job->step_details[i];
                if (i > 0) json << ",";
                json << "{\"id\":\"" << JsonFormatter::escape(step.id)
                     << "\",\"name\":\"" << JsonFormatter::escape(step.name)
                     << "\",\"started\":" << (step.started ? "true" : "false")
                     << ",\"completed\":" << (step.completed ? "true" : "false")
                     << ",\"failed\":" << (step.failed ? "true" : "false")
                     << ",\"skipped\":" << (step.skipped ? "true" : "false")
                     << ",\"duration_ms\":" << step.duration_ms
                     << ",\"result\":\"" << JsonFormatter::escape(step.result)
                     << "\",\"message\":\"" << JsonFormatter::escape(step.message)
                     << "\",\"error_code\":\"" << JsonFormatter::escape(step.error_code)
                     << "\",\"started_at\":\"" << JsonFormatter::escape(step.started_at)
                     << "\",\"completed_at\":\"" << JsonFormatter::escape(step.completed_at)
                     << "\"}";
            }
            json << "],\"failure\":{"
                 << "\"step\":\"" << JsonFormatter::escape(job->failure.step)
                 << "\",\"step_name\":\"" << JsonFormatter::escape(job->failure.step_name)
                 << "\",\"reason\":\"" << JsonFormatter::escape(job->failure.reason)
                 << "\",\"error_code\":\"" << JsonFormatter::escape(job->failure.error_code)
                 << "\",\"compensation_started\":" << (job->failure.compensation_started ? "true" : "false")
                 << ",\"compensation_result\":\"" << JsonFormatter::escape(job->failure.compensation_result)
                 << "\",\"manual_recovery_required\":" << (job->failure.manual_recovery_required ? "true" : "false")
                 << "}}}";
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
        std::string owner = json_extract_string_value(req.body, "owner");
        std::string domain = json_extract_string_value(req.body, "domain");
        std::string profile = json_extract_string_value(req.body, "profile");
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

        // Protect admin-panel system site from removal
        if (domain == s.config().server_hostname()) {
            r.status_code = 403;
            r.body = "{\"success\":false,\"error\":\"System site cannot be removed\"}";
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

    auto backup_create_handler = [&s, &backup_job_response](const Request& req) {
        Response r;
        std::string domain = json_extract(req.body, "domain");
        uint64_t site_id = 0;
        if (!json_extract(req.body, "site_id").empty()) {
            try { site_id = std::stoull(json_extract(req.body, "site_id")); } catch (...) { site_id = 0; }
        }
        auto* site = site_id != 0 ? s.sites().find_by_id(site_id) : s.sites().find(domain);
        if (!site) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":{\"code\":\"site_not_found\",\"message\":\"Site was not found\"}}";
            return r;
        }
        return backup_job_response(s.backup_jobs().enqueue_create(site->id));
    };

    router_.add("POST", "/api/backups", backup_create_handler);
    router_.add("POST", "/api/backups/create", backup_create_handler);

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
        auto removed = s.backup_service().remove_backup(id);
        if (!removed.success) {
            r.status_code = removed.code == "backup_not_found" ? 404 : 400;
            r.body = "{\"success\":false,\"error\":{\"code\":\"" + JsonFormatter::escape(removed.code) + "\",\"message\":\"" + JsonFormatter::escape(removed.message) + "\"}}";
            return r;
        }
        s.save();
        r.body = "{\"success\":true,\"data\":{\"message\":\"Backup removed\",\"backup_id\":" + std::to_string(id) + "}}";
        return r;
    });

    auto backup_restore_handler = [&s, &backup_job_response](const Request& req, uint64_t route_backup_id) {
        Response r;
        std::string id_str = json_extract(req.body, "id");
        uint64_t id = route_backup_id;
        if (id == 0 && id_str.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":{\"code\":\"backup_id_required\",\"message\":\"Backup id is required\"}}";
            return r;
        }
        if (id == 0) {
            try { id = std::stoull(id_str); } catch (...) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":{\"code\":\"backup_id_invalid\",\"message\":\"Backup id must be numeric\"}}";
                return r;
            }
        }
        uint64_t target_site_id = 0;
        const auto target_site = json_extract(req.body, "target_site_id");
        if (!target_site.empty()) {
            try { target_site_id = std::stoull(target_site); } catch (...) { target_site_id = 0; }
        }
        std::string mode = json_extract(req.body, "mode");
        if (mode.empty()) mode = "full";
        return backup_job_response(s.backup_jobs().enqueue_restore(id, target_site_id, mode, json_extract(req.body, "confirmation")));
    };

    router_.add("POST", "/api/backups/restore", [backup_restore_handler](const Request& req) {
        return backup_restore_handler(req, 0);
    });

    router_.add_prefix("POST", "/api/backups/", [backup_restore_handler](const Request& req) {
        Response r;
        const std::string rest = req.path.substr(std::string("/api/backups/").size());
        const auto slash = rest.find('/');
        if (slash == std::string::npos || rest.substr(slash + 1) != "restore") {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Not found\"}";
            return r;
        }
        uint64_t backup_id = 0;
        try { backup_id = std::stoull(rest.substr(0, slash)); } catch (...) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":{\"code\":\"backup_id_invalid\",\"message\":\"Backup id must be numeric\"}}";
            return r;
        }
        return backup_restore_handler(req, backup_id);
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
                 << "\",\"domains\":[";
        for (size_t i = 0; i < meta.domains.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << JsonFormatter::escape(meta.domains[i]) << "\"";
        }
        json << "]"
             << ",\"last_validation\":\"" << JsonFormatter::escape(meta.last_validation)
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
    router_.add_prefix("POST", "/api/ssl/", [&s, &ssl_domain_from_path, &json_error, &issue_ssl_certificate](const Request& req) {
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

            std::vector<std::string> steps = {"Validating domain...", "Requesting certificate...",
                                               "Waiting for ACME validation...", "Finalizing..."};
            uint64_t job_id = s.jobs().create("ssl-issue", steps);
            s.jobs().update(job_id, "pending", 0, "Queued");

            // Enqueue async job execution via JobExecutor
            bool submitted = s.job_executor().submit(job_id,
                [&issue_ssl_certificate, provider_id, domain, site_id](jobs::JobManager& jm, uint64_t jid) {
                    jm.update(jid, "running", 10, "Requesting certificate...");
                    auto result = issue_ssl_certificate(domain, site_id, provider_id);
                    if (result.success) {
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
            // Protect admin-panel domain (server hostname) from removal
            if (name == s.config().server_hostname()) {
                r.status_code = 403;
                r.body = "{\"success\":false,\"error\":\"System domain cannot be removed\"}";
                return r;
            }
            auto* domain = s.domains().find(name);
            if (!domain) { r.body = "{\"success\":false,\"error\":\"Not found\"}"; return r; }
            s.domains().remove(domain->id);
        } else if (type == "database") {
            auto* db = s.databases().find(name);
            if (!db) { r.body = "{\"success\":false,\"error\":\"Not found\"}"; return r; }
            s.logger().warning("AUDIT", "database_lifecycle operation=legacy-remove stage=requested result=deprecated_metadata_only database_id=" + std::to_string(db->id) + " site_id=" + std::to_string(db->site_id));
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
        auto r = remove_resource("database", name);
        if (r.status_code == 200) {
            r.body = "{\"success\":true,\"data\":{\"message\":\"Database metadata removed; physical MariaDB objects were not dropped\",\"deprecated\":true,\"physical_drop\":false}}";
        }
        return r;
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

    // PATCH /api/mail/domains/<id> — update a mail domain (transactional, no partial mutation)
    router_.add_prefix("PATCH", "/api/mail/domains/", [&s, &mail_domain_json](const Request& req) {
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

        // Validate all input into temporary variables — NO mutation of m yet
        auto new_mode = m->mode;
        bool new_enabled = m->enabled;
        std::string new_relay = m->relay_host;

        std::string mode_str = json_extract(req.body, "mode");
        if (!mode_str.empty()) {
            if (!mail::is_valid_mail_domain_mode(mode_str)) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Invalid mode. Valid: disabled, local-primary, external-relay, split-m365\"}";
                return r;
            }
            new_mode = mail::mail_domain_mode_from_string(mode_str);
        }

        std::string enabled_str = json_extract(req.body, "enabled");
        if (!enabled_str.empty()) {
            if (enabled_str != "true" && enabled_str != "false") {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"enabled must be boolean true or false\"}";
                return r;
            }
            new_enabled = (enabled_str == "true");
        }

        std::string relay_host = json_extract(req.body, "relay_host");
        if (!relay_host.empty()) {
            if (relay_host == "null") {
                new_relay.clear();
            } else {
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
                new_relay = relay_host;
            }
        }

        // Validate final mode+relay combination (against new_* values, NOT m)
        std::string vr = mail::MailDomainManager::validate_mode_relay(new_mode, new_relay);
        if (!vr.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(vr) + "\"}";
            return r;
        }

        // All validation passed — snapshot old state for sync-rollback, then mutate m
        auto old_mode = m->mode;
        bool old_enabled = m->enabled;
        std::string old_relay = m->relay_host;

        m->mode = new_mode;
        m->enabled = new_enabled;
        m->relay_host = new_relay;

        // Persist + sync with rollback on failure
        s.save();
        auto sync_result = s.runtime_sync().sync("mail");
        if (!sync_result.success) {
            m->mode = old_mode;
            m->enabled = old_enabled;
            m->relay_host = old_relay;
            s.save();
            (void)s.runtime_sync().sync("mail");
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"Update failed, previous state restored: "
                + JsonFormatter::escape(sync_result.message) + "\"}";
            return r;
        }

        r.body = "{\"success\":true,\"data\":" + mail_domain_json(*m) + "}";
        return r;
    });

    // ── Site mail endpoints (enable/disable/status) ────────────────────
    // Uses SiteMailOrchestrator for all business logic (Single Source of Truth)

    // Helper: parse /api/sites/{site_id}/{action} from path
    // Returns true if parsed, false if 404 should be returned
    // Sets site_id, action, site (if found). Also sets r if error.
    struct SiteMailAction {
        uint64_t site_id = 0;
        std::string action;
        site::Site* site = nullptr;
    };
    auto parse_site_mail_path = [&s](const Request& req, Response& r) -> SiteMailAction {
        SiteMailAction result;
        std::string remaining = req.path.substr(std::string("/api/sites/").size());
        auto slash = remaining.find('/');
        if (slash == std::string::npos || slash == 0) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Not found\"}";
            return result;
        }
        std::string id_str = remaining.substr(0, slash);
        result.action = remaining.substr(slash + 1);
        try { result.site_id = std::stoull(id_str); } catch (...) {}
        if (result.site_id == 0) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Invalid site ID\"}";
            return result;
        }
        result.site = s.sites().find_by_id(result.site_id);
        if (!result.site) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Site not found\"}";
            return result;
        }
        return result;
    };

    // GET /api/sites/<id>/mail-status — read-only status
    router_.add_prefix("GET", "/api/sites/", [&s, &parse_site_mail_path](const Request& req) {
        Response r;
        auto parsed = parse_site_mail_path(req, r);
        if (!parsed.site) return r;  // r already set by parse
        if (parsed.action != "mail-status") {
            r.status_code = 405;
            r.body = "{\"success\":false,\"error\":\"Only mail-status is available via GET\"}";
            return r;
        }
        auto status = s.mail_orchestrator().get_status(
            parsed.site_id, parsed.site->domain, parsed.site->php_mail_enabled);
        auto* md = s.mail().find_by_domain(parsed.site->domain);
        r.body = "{\"success\":true,\"data\":{"
            "\"site_id\":" + std::to_string(parsed.site_id) + ","
            "\"domain\":\"" + JsonFormatter::escape(parsed.site->domain) + "\","
            "\"enabled\":" + (status.enabled ? "true" : "false") + ","
            "\"mail_domain\":" + (md ? "true" : "false") + ","
            "\"credential_exists\":" + (status.credential_exists ? "true" : "false") + ","
            "\"msmtprc\":" + (status.msmtprc_exists ? "true" : "false") + ","
            "\"network\":" + (status.network_connected ? "true" : "false") + "}}";
        return r;
    });

    // POST /api/sites/<id>/enable-mail — enable mail for a site
    // POST /api/sites/<id>/disable-mail — disable mail
    // POST /api/sites/<id>/send-test-email — test msmtp chain
    router_.add_prefix("POST", "/api/sites/", [&s, &parse_site_mail_path](const Request& req) {
        Response r;
        auto parsed = parse_site_mail_path(req, r);
        if (!parsed.site) return r;

        auto* site = parsed.site;
        uint64_t site_id = parsed.site_id;
        const std::string action = parsed.action;

        // Helper: regenerate docker-compose.yml from internal model (canonical compose generator)
        auto regenerate_compose = [&](bool mail_active) -> core::OperationResult {
            std::string site_dir = s.config().sites_dir() + site->domain + "/";
            std::string web_server_type = site->web_server.empty() ? "apache" : site->web_server;
            std::string php_image = "ghcr.io/powern/containercp-php:8.4";
            auto* pv = s.php_versions().get_default();
            if (pv) php_image = pv->image;

            std::string web_image = "nginx:alpine";
            std::string web_cfg = "/etc/nginx/conf.d";
            std::string web_log = "/var/log/nginx";
            std::string web_root = "/var/www/html";
            std::string web_local_cfg = "config/nginx";
            std::string web_local_log = "logs/nginx";
            std::string web_cmd = "";
            if (web_server_type == "apache") {
                web_image = "httpd:alpine";
                web_cfg = "/usr/local/apache2/conf/extra";
                web_log = "/usr/local/apache2/logs";
                web_root = "/usr/local/apache2/htdocs";
                web_local_cfg = "config/apache";
                web_local_log = "logs/apache";
                web_cmd = "[\"httpd-foreground\", \"-c\", \"IncludeOptional conf/extra/*.conf\"]";
            }

            docker::ComposeGenerator gen(s.filesystem(), s.config().templates_dir());
            std::string gen_path = site_dir + "docker-compose.yml.gen-tmp";
            bool gen_ok = gen.generate(site->domain, site->owner, php_image, gen_path,
                std::to_string(site_id), web_image, web_cfg, web_log, web_root,
                web_local_cfg, web_local_log, web_cmd, mail_active);
            if (!gen_ok) {
                std::remove(gen_path.c_str());
                return {false, "Failed to generate docker-compose.yml"};
            }

            runtime::CommandExecutor exec;
            auto validate = exec.run({
                "docker", "compose", "-f", gen_path, "config", "--quiet"
            });
            if (validate.exit_code != 0) {
                std::remove(gen_path.c_str());
                return {false, "Compose validation failed: " + validate.err};
            }

            if (std::rename(gen_path.c_str(), (site_dir + "docker-compose.yml").c_str()) != 0) {
                std::remove(gen_path.c_str());
                return {false, "Failed to replace compose file"};
            }
            return {true, ""};
        };

        // Rollback helper for enable-mail failures
        auto rollback_enable_mail = [&](uint64_t sid, const std::string& dm) {
            site->php_mail_enabled = false;
            s.mail_orchestrator().disable_mail(sid, dm);
            auto comp = regenerate_compose(false);
            if (!comp.success) {
                s.logger().warning("MAIL", "Rollback compose regeneration failed: " + comp.message);
            }
            s.save();
        };

        if (action == "apply-template") {
            std::string template_name = json_extract_string_value(req.body, "template_name");
            std::string template_id_str = json_extract(req.body, "template_id");

            std::string site_backend = site->web_server.empty() ? "apache" : site->web_server;
            profile::Profile* prof = nullptr;
            if (!template_id_str.empty()) {
                try {
                    uint64_t tid = std::stoull(template_id_str);
                    auto* tp = s.profiles().find(tid);
                    if (tp && tp->type == profile::ProfileType::WEB_SERVER) prof = tp;
                } catch (...) {}
            } else if (!template_name.empty()) {
                auto* tp = s.profiles().find(template_name);
                if (tp && tp->type == profile::ProfileType::WEB_SERVER) prof = tp;
            } else {
                for (auto* tp : s.profiles().list_by_type(profile::ProfileType::WEB_SERVER)) {
                    if (tp != nullptr && tp->web_server == site_backend && tp->default_profile) {
                        prof = tp;
                        break;
                    }
                }
            }

            if (!prof || prof->template_path.empty()) {
                r.status_code = 404;
                r.body = "{\"success\":false,\"error\":\"Template profile not found\"}";
                return r;
            }
            if (prof->web_server != site_backend) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Template backend does not match site backend\"}";
                return r;
            }

            containercp::provider::DockerComposeProvider* dcp =
                dynamic_cast<containercp::provider::DockerComposeProvider*>(&s.hosting_provider());
            if (!dcp) {
                r.status_code = 500;
                r.body = "{\"success\":false,\"error\":\"Provider does not support apply-template\"}";
                return r;
            }

            auto result = dcp->apply_web_template(*site, prof->template_path);
            if (!result.success) {
                r.status_code = 500;
                r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(result.message) + "\"}";
                return r;
            }

            r.body = "{\"success\":true,\"data\":{\"message\":\"" + JsonFormatter::escape(result.message) + "\"}}";
            return r;
        }

        if (action == "mail-domain") {
            // Create a MailDomain for this site, linked to its canonical Domain
            containercp::domain::Domain* dom = nullptr;
            for (const auto& d : s.domains().list()) {
                if (d.site_id == site_id) { dom = s.domains().find(d.id); break; }
            }
            if (!dom) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"domain_not_found\","
                    "\"message\":\"No ContainerCP Domain found for this site\"}";
                return r;
            }
            uint64_t md_id = s.mail().create(site->domain,
                mail::MailDomainMode::LocalPrimary, dom->id, site_id, "");
            if (md_id == 0) {
                r.status_code = 409;
                r.body = "{\"success\":false,\"error\":\"mail_domain_exists\","
                    "\"message\":\"MailDomain already exists for this domain\"}";
                return r;
            }
            s.save();
            // Trigger mail config regeneration with rollback on failure
            auto sync_result = s.runtime_sync().sync("mail");
            if (!sync_result.success) {
                s.mail().remove(md_id);
                s.save();
                r.status_code = 500;
                r.body = "{\"success\":false,\"error\":\"Failed to sync mail config: "
                    + JsonFormatter::escape(sync_result.message) + "\"}";
                return r;
            }
            r.body = "{\"success\":true,\"data\":{\"id\":" + std::to_string(md_id) + "}}";
            return r;
        }

        if (action == "enable-mail") {
            // 1. MailDomain must exist (created separately via Mail → Domains)
            auto* md = s.mail().find_by_domain(site->domain);
            if (!md) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"mail_domain_missing\","
                    "\"message\":\"Mail Domain not found. Create one via Mail → Domains first.\"}";
                return r;
            }
            if (!md->enabled || md->mode == mail::MailDomainMode::Disabled) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"mail_domain_disabled\","
                    "\"message\":\"Mail Domain exists but is disabled. Enable it in Mail → Domains first.\"}";
                return r;
            }

            // 2. Regenerate compose with mail_active=true
            auto comp = regenerate_compose(true);
            if (!comp.success) {
                rollback_enable_mail(site_id, site->domain);
                r.status_code = 500;
                r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(comp.message) + "\"}";
                return r;
            }

            // 3. Create credentials, msmtprc, connect network, sync
            auto result = s.mail_orchestrator().enable_mail(site_id, site->domain);
            if (!result.success) {
                rollback_enable_mail(site_id, site->domain);
                r.status_code = 500;
                r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(result.message) + "\"}";
                return r;
            }

            // 4. Pull latest PHP image and recreate container
            {
                runtime::CommandExecutor exec;
                exec.run({
                    "docker", "compose", "-f",
                    s.config().sites_dir() + site->domain + "/docker-compose.yml",
                    "pull", "php"
                });
                auto recreate = exec.run({
                    "docker", "compose", "-f",
                    s.config().sites_dir() + site->domain + "/docker-compose.yml",
                    "up", "-d", "--force-recreate", "php"
                });
                if (recreate.exit_code != 0) {
                    rollback_enable_mail(site_id, site->domain);
                    r.status_code = 500;
                    r.body = "{\"success\":false,\"error\":\"Failed to recreate PHP container: "
                        + JsonFormatter::escape(recreate.err) + "\"}";
                    return r;
                }
            }

            site->php_mail_enabled = true;
            s.save();
            r.body = "{\"success\":true,\"data\":{\"message\":\"Mail enabled for site " + site->domain + "\"}}";
            return r;
        }

        if (action == "disable-mail") {
            // 1. Remove credentials, msmtprc, disconnect network FIRST
            auto result = s.mail_orchestrator().disable_mail(site_id, site->domain);
            if (!result.success) {
                r.status_code = 500;
                r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(result.message) + "\"}";
                return r;
            }

            // 2. Regenerate compose without mail
            auto comp = regenerate_compose(false);
            if (!comp.success) {
                s.logger().warning("MAIL", "Compose regeneration after disable failed: " + comp.message);
            }

            site->php_mail_enabled = false;

            s.save();
            r.body = "{\"success\":true,\"data\":{\"message\":\"Mail disabled for site " + site->domain + "\"}}";
            return r;
        }

        if (action == "send-test-email") {
            // Tests the msmtp → SMTP submission chain (not PHP internally).
            // Sends a pre-formed email via msmtp inside the PHP container.
            runtime::CommandExecutor exec;

            // Validate recipient email
            std::string test_to = json_extract(req.body, "to");
            if (test_to.empty()) test_to = "admin@" + site->domain;
            // Basic email validation: contains @ and no shell-dangerous chars
            bool valid = test_to.find('@') != std::string::npos;
            for (char c : test_to) {
                if (!std::isalnum(static_cast<unsigned char>(c)) &&
                    c != '@' && c != '.' && c != '-' && c != '_' && c != '+') {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Invalid recipient email address\"}";
                return r;
            }

            // Write email content to temp file (avoids shell injection in command args)
            std::string tmp_path = "/tmp/containercp-test-email-" + std::to_string(site_id);
            {
                std::ofstream tmp(tmp_path);
                if (!tmp) {
                    r.status_code = 500;
                    r.body = "{\"success\":false,\"error\":\"Failed to create temp file\"}";
                    return r;
                }
                tmp << "Subject: ContainerCP Mail Test\n"
                    << "To: " << test_to << "\n"
                    << "\n"
                    << "This is a test email from ContainerCP PHP mail integration.\n"
                    << "Sent from: " << site->domain << "\n";
            }

            // Execute msmtp with stdin from file (no shell interpretation of test_to)
            auto result = exec.run_with_stdin_file(
                {"docker", "exec", "-i", "site-" + std::to_string(site_id) + "-php",
                 "msmtp", "-t", "--", test_to},
                tmp_path);

            std::remove(tmp_path.c_str());

            if (result.exit_code != 0) {
                r.status_code = 500;
                r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(result.err) + "\"}";
            } else {
                r.body = "{\"success\":true,\"data\":{\"message\":\"Test email sent to " + test_to + "\"}}";
            }
            return r;
        }

        r.status_code = 404;
        r.body = "{\"success\":false,\"error\":\"Unknown action: " + action + "\"}";
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
    // Shared helper: validate backup path is allowed
    auto resolve_allowed_backup = [&](const std::string& basename,
                                      const std::vector<std::string>& allowed,
                                      std::string& out_error) -> std::string {
        // basename must not contain path separators
        if (basename.empty() || basename.find('/') != std::string::npos) {
            out_error = "Invalid backup filename";
            return "";
        }
        // extension must be .tar or .tar.gz
        if (basename.size() < 4) { out_error = "Invalid extension"; return ""; }
        auto ext = basename.substr(basename.size() - 4);
        bool ok_ext = (ext == ".tar");
        if (!ok_ext && basename.size() > 7) {
            ok_ext = (basename.substr(basename.size() - 7) == ".tar.gz");
        }
        if (!ok_ext) { out_error = "Only .tar and .tar.gz files are allowed"; return ""; }

        for (const auto& dir : allowed) {
            std::string candidate = dir + "/" + basename;

            // Step 1: lstat to detect symlinks BEFORE realpath
            struct stat lst;
            if (::lstat(candidate.c_str(), &lst) != 0) continue;
            if (S_ISLNK(lst.st_mode)) {
                out_error = "Symlink not allowed: " + basename;
                return "";
            }
            if (!S_ISREG(lst.st_mode)) continue;

            // Step 2: resolve canonical path
            char real[PATH_MAX];
            if (!::realpath(candidate.c_str(), real)) continue;
            std::string canon(real);

            // Step 3: verify inside allowed directory
            char ad_real[PATH_MAX];
            if (!::realpath(dir.c_str(), ad_real)) continue;
            std::string ad(ad_real);

            // Proper prefix: canon == ad OR canon starts with ad + "/"
            bool inside = (canon == ad);
            if (!inside && canon.size() > ad.size() + 1) {
                inside = (canon.substr(0, ad.size() + 1) == ad + "/");
            }
            if (!inside) continue;

            // Step 4: verify regular file via lstat (already done above)
            return canon;
        }
        out_error = "Backup file not found in allowed directories";
        return "";
    };

    router_.add("GET", "/api/migration/vesta/backups", [&s, &resolve_allowed_backup](const Request&) {
        Response r;
        std::vector<std::string> allowed_dirs = {"/backup", s.config().data_root() + "/backups"};
        std::ostringstream json;
        json << "{\"success\":true,\"data\":[";
        bool first = true;

        for (const auto& dir : allowed_dirs) {
            // Resolve allowed dir canonical path
            char ad_real[PATH_MAX];
            if (!::realpath(dir.c_str(), ad_real)) continue;
            std::string ad(ad_real);

            // Use std::filesystem::directory_iterator (C++17)
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(ad, ec)) {
                if (ec) break;
                auto path = entry.path();

                // lstat to detect symlinks
                struct stat lst;
                if (::lstat(path.c_str(), &lst) != 0) continue;
                if (S_ISLNK(lst.st_mode)) continue;
                if (!S_ISREG(lst.st_mode)) continue;

                std::string fname = path.filename().string();
                std::string ext;
                if (fname.size() > 4) ext = fname.substr(fname.size() - 4);
                if (ext != ".tar") {
                    if (fname.size() > 7 && fname.substr(fname.size() - 7) == ".tar.gz") {
                        // ok
                    } else {
                        continue;
                    }
                }

                if (!first) json << ",";
                first = false;
                json << "{\"name\":\"" << JsonFormatter::escape(fname)
                     << "\",\"size\":" << lst.st_size
                     << ",\"mtime\":" << lst.st_mtime
                     << "}";
            }
        }

        json << "]}";
        r.body = json.str();
        return r;
    });

    router_.add("POST", "/api/migration/vesta/inspect", [&s, &resolve_allowed_backup](const Request& req) {
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

        // Resolve backup path using shared helper
        std::vector<std::string> allowed_dirs = {"/backup", s.config().data_root() + "/backups"};
        std::string resolve_error;
        std::string resolved_path = resolve_allowed_backup(backup, allowed_dirs, resolve_error);

        if (resolved_path.empty()) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(resolve_error) + "\"}";
            return r;
        }

        // Run inspect
        runtime::CommandExecutor exec;
        migration::VestaSiteImporter importer(exec, s.filesystem(), s.config(), s.logger(),
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
             << ",\"migration_marker_found\":" << (manifest.migration_marker_found ? "true" : "false")
             << ",\"migration_stage\":" << manifest.migration_stage
             << ",\"files_pending\":" << (manifest.files_pending ? "true" : "false")
             << ",\"files_imported\":" << (manifest.files_imported ? "true" : "false")
             << ",\"sql_pending\":" << (manifest.sql_pending ? "true" : "false")
             << ",\"can_import_files\":" << (manifest.can_import_files ? "true" : "false")
             << ",\"can_import_sql\":" << (manifest.can_import_sql ? "true" : "false")
             << ",\"migration_completed\":" << (manifest.migration_completed ? "true" : "false")
             << ",\"migration_site_id\":" << manifest.migration_site_id
             << ",\"migration_owner\":\"" << JsonFormatter::escape(manifest.migration_owner)
             << "\",\"files_status\":\"" << JsonFormatter::escape(manifest.files_status)
             << "\",\"sql_status\":\"" << JsonFormatter::escape(manifest.sql_status)
             << "\",\"marker_error\":\"" << JsonFormatter::escape(manifest.marker_error)
             << "\",\"available_disk_mb\":" << manifest.available_disk_mb
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

    router_.add("POST", "/api/migration/vesta/create-site", [&s, &resolve_allowed_backup](const Request& req) {
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

        // Step 1: validate backup file (re-inspect, don't trust browser data)
        std::vector<std::string> allowed_dirs = {"/backup", s.config().data_root() + "/backups"};
        std::string resolve_error;
        std::string resolved_path = resolve_allowed_backup(backup, allowed_dirs, resolve_error);

        if (resolved_path.empty()) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(resolve_error) + "\"}";
            return r;
        }

        // Step 2: re-run inspect to validate
        runtime::CommandExecutor exec;
        migration::VestaSiteImporter importer(exec, s.filesystem(), s.config(), s.logger(),
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

        if (!manifest.errors.empty()) {
            std::ostringstream err;
            err << "Validation failed: ";
            for (const auto& e : manifest.errors) err << e << "; ";
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(err.str()) + "\"}";
            return r;
        }

        if (manifest.site_exists) {
            r.status_code = 409;
            r.body = "{\"success\":false,\"error\":\"Site already exists. Import not possible.\"}";
            return r;
        }

        // Step 3: find default node
        auto* node = s.nodes().find("local");
        if (!node) {
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"No node available\"}";
            return r;
        }

        // Step 4: create site via SiteCreateOperation (same code as POST /api/sites/create)
        operations::SiteCreateOperation site_op(s.sites(), s.domains(),
            s.databases(), s.reverse_proxies(),
            s.proxy_provider(),
            s.filesystem(), s.config(), s.hosting_provider());

        auto result = site_op.execute(owner, domain, *node, false, "", nullptr, 0);

        if (!result.success) {
            s.save();
            r.status_code = 500;
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(result.message) + "\"}";
            return r;
        }

        s.save();

        // Find created site BEFORE writing marker (need real site_id)
        auto* created_site = s.sites().find(domain);

        // Create migration marker for Stage 2 validation with real site_id
        if (created_site) {
            std::string marker_path = s.config().sites_dir() + domain + "/.containercp-migration.json";
            std::string marker = "{\"domain\":\"" + domain
                + "\",\"owner\":\"" + owner
                + "\",\"site_id\":" + std::to_string(created_site->id)
                + ",\"stage\":1,\"files_pending\":true,\"files_imported\":false,\"sql_pending\":true}";
            s.filesystem().create_file(marker_path, marker);
        } else {
            s.logger().warning("MIGRATION", "SiteRecord not found after create — marker not written");
        }
        std::string site_id_str = created_site ? std::to_string(created_site->id) : "unknown";
        std::string db_name, db_user;
        for (const auto& d : s.databases().list()) {
            if (created_site && d.site_id == created_site->id) {
                db_name = d.db_name;
                db_user = d.db_user;
                break;
            }
        }

        std::ostringstream json;
        json << "{\"success\":true,\"data\":{"
             << "\"message\":\"Stage 1 completed — site created\""
             << ",\"site_id\":" << site_id_str
             << ",\"domain\":\"" << JsonFormatter::escape(domain)
             << "\",\"database_name\":\"" << JsonFormatter::escape(db_name)
             << "\",\"database_user\":\"" << JsonFormatter::escape(db_user)
             << "\",\"document_root\":\"" << JsonFormatter::escape(s.config().sites_dir() + domain + "/public")
             << "\",\"status\":{"
             << "\"site\":\"created\""
             << ",\"database\":\"created\""
             << ",\"docker_stack\":\"created\""
             << ",\"files_import\":\"pending\""
             << ",\"sql_import\":\"pending\""
             << "}}}";
        r.body = json.str();
        return r;
    });

    router_.add("POST", "/api/migration/vesta/import-files", [&s, &resolve_allowed_backup](const Request& req) {
        Response r;

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

        std::string backup = extract("backup");
        std::string domain = extract("domain");
        std::string owner = extract("owner");
        bool keep_staging = extract_bool("keep_staging");

        if (backup.empty() || domain.empty() || owner.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"backup, domain and owner are required\"}";
            return r;
        }

        // Validate backup
        std::vector<std::string> allowed_dirs = {"/backup", s.config().data_root() + "/backups"};
        std::string resolve_error;
        std::string resolved_path = resolve_allowed_backup(backup, allowed_dirs, resolve_error);
        if (resolved_path.empty()) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(resolve_error) + "\"}";
            return r;
        }

        // Run import
        runtime::CommandExecutor exec;
        migration::VestaSiteImporter importer(exec, s.filesystem(), s.config(), s.logger(),
                                              &s.sites(), &s.domains());
        migration::Options opts;
        opts.backup_path = resolved_path;
        opts.domain = domain;
        opts.owner = owner;
        opts.keep_staging = keep_staging;
        opts.dry_run = false;

        auto import_result = importer.import_files(opts);

        if (!import_result.success) {
            r.status_code = 500;
            std::ostringstream err;
            err << "{\"success\":false,\"error\":\"";
            for (const auto& e : import_result.errors) err << JsonFormatter::escape(e) << "; ";
            err << "\"}";
            r.body = err.str();
            return r;
        }

        std::ostringstream json;
        json << "{\"success\":true,\"data\":{"
             << "\"message\":\"Stage 2 completed — files imported\""
             << ",\"web_root\":\"" << JsonFormatter::escape(import_result.web_root_type)
             << "\",\"destination\":\"" << JsonFormatter::escape(s.config().sites_dir() + domain + "/public")
             << "\",\"files_count\":" << import_result.files_count
             << ",\"bytes_copied\":" << import_result.bytes_copied
             << ",\"warnings\":[";

        bool first_w = true;
        for (const auto& w : import_result.warnings) {
            if (!first_w) json << ",";
            first_w = false;
            json << "\"" << JsonFormatter::escape(w) << "\"";
        }
        json << "],\"errors\":[";

        bool first_e = true;
        for (const auto& e : import_result.errors) {
            if (!first_e) json << ",";
            first_e = false;
            json << "\"" << JsonFormatter::escape(e) << "\"";
        }

        json << "],\"status\":{"
             << "\"files\":\"imported\""
             << ",\"sql_import\":\"pending\""
             << ",\"wp_config_update\":\"pending\""
             << "}}}";
        r.body = json.str();
        return r;
    });

    router_.add("POST", "/api/migration/vesta/import-sql", [&s, &resolve_allowed_backup](const Request& req) {
        Response r;

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

        std::string backup = extract("backup");
        std::string domain = extract("domain");
        std::string owner = extract("owner");
        bool keep_staging = extract_bool("keep_staging");

        if (backup.empty() || domain.empty() || owner.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"backup, domain and owner are required\"}";
            return r;
        }

        std::vector<std::string> allowed_dirs = {"/backup", s.config().data_root() + "/backups"};
        std::string resolve_error;
        std::string resolved_path = resolve_allowed_backup(backup, allowed_dirs, resolve_error);
        if (resolved_path.empty()) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(resolve_error) + "\"}";
            return r;
        }

        // Create async job
        auto& jobs = s.jobs();
        uint64_t job_id = jobs.create("migration_import_sql", {
            "Reading marker", "Finding SQL dump", "Extracting dump",
            "Safety backup", "Dropping tables", "Importing SQL",
            "Verifying database", "Updating wp-config", "Health check",
            "Finalizing"
        });
        jobs.update(job_id, "queued", 0);

        // Capture by copy for thread safety
        auto opts_ptr = std::make_shared<migration::Options>();
        opts_ptr->backup_path = resolved_path;
        opts_ptr->domain = domain;
        opts_ptr->owner = owner;
        opts_ptr->keep_staging = keep_staging;

        // Run in separate thread
        std::thread([&s, opts_ptr, job_id]() {
            s.jobs().update(job_id, "running", 5, "Starting SQL import");

            runtime::CommandExecutor exec;
            migration::VestaSiteImporter importer(exec, s.filesystem(), s.config(), s.logger(),
                                                  &s.sites(), &s.domains());
            auto import_result = importer.import_sql(*opts_ptr);

            if (!import_result.success) {
                std::string err_msg;
                for (const auto& e : import_result.errors) err_msg += e + "; ";
                s.jobs().update(job_id, "failed", 0, err_msg);
                return;
            }

            s.jobs().update(job_id, "completed", 100, "SQL import completed");
        }).detach();

        r.status_code = 202;
        r.body = "{\"success\":true,\"data\":{\"job_id\":" + std::to_string(job_id)
            + ",\"state\":\"queued\",\"message\":\"SQL import started\"}}";
        return r;
    });

    router_.add("POST", "/api/migration/vesta/migrate", [&s, &resolve_allowed_backup, &issue_ssl_certificate](const Request& req) {
        Response r;

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

        std::string backup = extract("backup");
        std::string domain = extract("domain");
        std::string owner = extract("owner");
        std::string database = extract("database");
        bool skip_db = extract_bool("skip_db");
        bool keep_staging = extract_bool("keep_staging");

        if (backup.empty() || domain.empty() || owner.empty()) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"backup, domain and owner are required\"}";
            return r;
        }

        std::vector<std::string> allowed_dirs = {"/backup", s.config().data_root() + "/backups"};
        std::string resolve_error;
        std::string resolved_path = resolve_allowed_backup(backup, allowed_dirs, resolve_error);
        if (resolved_path.empty()) {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"" + JsonFormatter::escape(resolve_error) + "\"}";
            return r;
        }

        auto& jobs = s.jobs();
        uint64_t job_id = jobs.create("migration-vesta", {
            "Analyze backup", "Create Site", "Create Database", "Deploy Containers",
            "Import Files", "Import SQL", "Configure WordPress", "Configure Proxy",
            "Issue SSL", "Health Checks", "Migration Complete"
        });
        jobs.update(job_id, "pending", 0, "Migration queued");

        auto opts_ptr = std::make_shared<migration::Options>();
        opts_ptr->backup_path = resolved_path;
        opts_ptr->domain = domain;
        opts_ptr->owner = owner;
        opts_ptr->database = database;
        opts_ptr->skip_db = skip_db;
        opts_ptr->keep_staging = keep_staging;

        bool queued = s.job_executor().submit(job_id, [&s, opts_ptr, &issue_ssl_certificate](jobs::JobManager& jobs, uint64_t queued_job_id) {
            auto mark_steps = [&](int current, bool complete = false) {
                auto* job = jobs.find(queued_job_id);
                if (!job) return;
                auto steps = job->step_details;
                for (std::size_t i = 0; i < steps.size(); ++i) {
                    const bool touched = complete || static_cast<int>(i) <= current;
                    steps[i].skipped = !touched;
                    steps[i].started = touched;
                    steps[i].completed = complete || static_cast<int>(i) < current;
                    steps[i].failed = false;
                    if (steps[i].completed) steps[i].result = "success";
                }
                jobs.update_step_details(queued_job_id, steps);
            };
            auto fail = [&](int progress, int current, const std::string& message) {
                auto* job = jobs.find(queued_job_id);
                if (job) {
                    auto steps = job->step_details;
                    for (std::size_t i = 0; i < steps.size(); ++i) {
                        const bool touched = static_cast<int>(i) <= current;
                        steps[i].skipped = !touched;
                        steps[i].started = touched;
                        steps[i].completed = static_cast<int>(i) < current;
                        steps[i].failed = static_cast<int>(i) == current;
                        if (steps[i].failed) {
                            steps[i].result = "failed";
                            steps[i].message = message;
                        } else if (steps[i].completed) {
                            steps[i].result = "success";
                        }
                    }
                    jobs.update_step_details(queued_job_id, steps);
                }
                jobs.update(queued_job_id, "failed", progress, message);
            };

            runtime::CommandExecutor exec;
            migration::VestaSiteImporter importer(exec, s.filesystem(), s.config(), s.logger(),
                                                  &s.sites(), &s.domains());

            jobs.update(queued_job_id, "running", 5, "Analyzing backup");
            mark_steps(0);
            auto manifest = importer.inspect(*opts_ptr);
            if (!manifest.errors.empty()) {
                fail(5, 0, "Analyze failed: " + manifest.errors.front());
                return;
            }

            if (!manifest.site_exists) {
                jobs.update(queued_job_id, "running", 12, "Creating Site");
                mark_steps(1);
                auto* node = s.nodes().find("local");
                if (!node) {
                    fail(12, 1, "No node available");
                    return;
                }

                operations::SiteCreateOperation site_op(s.sites(), s.domains(),
                    s.databases(), s.reverse_proxies(),
                    s.proxy_provider(),
                    s.filesystem(), s.config(), s.hosting_provider());

                auto result = site_op.execute(opts_ptr->owner, opts_ptr->domain, *node, false, "", nullptr, 0);
                s.save();
                if (!result.success) {
                    fail(20, 1, "Site creation failed: " + result.message);
                    return;
                }

                auto* created_site = s.sites().find(opts_ptr->domain);
                if (!created_site) {
                    fail(20, 1, "Site record not found after create");
                    return;
                }

                std::string marker_path = s.config().sites_dir() + opts_ptr->domain + "/.containercp-migration.json";
                std::string marker = "{\"domain\":\"" + opts_ptr->domain
                    + "\",\"owner\":\"" + opts_ptr->owner
                    + "\",\"site_id\":" + std::to_string(created_site->id)
                    + ",\"stage\":1,\"files_pending\":true,\"files_imported\":false,\"sql_pending\":true}";
                if (!s.filesystem().create_file(marker_path, marker)) {
                    fail(25, 1, "Site created but migration marker could not be written");
                    return;
                }
                mark_steps(3);
            } else if (!manifest.migration_marker_found) {
                fail(12, 1, "Existing site is not an active myVesta migration");
                return;
            }

            jobs.update(queued_job_id, "running", 35, "Resolving migration state");
            manifest = importer.inspect(*opts_ptr);
            if (!manifest.errors.empty()) {
                fail(35, 0, "Analyze failed after Site create: " + manifest.errors.front());
                return;
            }

            if (manifest.can_import_files) {
                jobs.update(queued_job_id, "running", 45, "Importing files");
                mark_steps(4);
                auto files = importer.import_files(*opts_ptr);
                if (!files.success) {
                    std::string err = files.errors.empty() ? "File import failed" : files.errors.front();
                    fail(45, 4, err);
                    return;
                }
                mark_steps(5);
            } else if (manifest.migration_stage == 1) {
                std::string reason = manifest.marker_error.empty() ? "File import is not available" : manifest.marker_error;
                fail(45, 4, reason);
                return;
            }

            jobs.update(queued_job_id, "running", 65, "Resolving SQL import state");
            manifest = importer.inspect(*opts_ptr);
            if (!manifest.errors.empty()) {
                fail(65, 5, "Analyze failed before SQL import: " + manifest.errors.front());
                return;
            }

            if (manifest.can_import_sql) {
                jobs.update(queued_job_id, "running", 72, "Importing SQL and configuring WordPress");
                mark_steps(5);
                auto sql = importer.import_sql(*opts_ptr);
                if (!sql.success) {
                    std::string err = sql.errors.empty() ? "SQL import failed" : sql.errors.front();
                    fail(72, 5, err);
                    return;
                }
                mark_steps(8);
            } else if (manifest.migration_stage != 3) {
                std::string reason = manifest.marker_error.empty() ? "SQL import is not available" : manifest.marker_error;
                fail(72, 5, reason);
                return;
            }

            jobs.update(queued_job_id, "running", 85, "Issuing SSL certificate");
            mark_steps(8);
            auto* site = s.sites().find(opts_ptr->domain);
            if (!site) {
                fail(85, 8, "Site record not found before SSL issuance");
                return;
            }
            auto existing_ssl = s.cert_store().load_metadata(site->id);
            bool ssl_already_active = false;
            if (existing_ssl.success && existing_ssl.metadata.status == "active" && existing_ssl.metadata.https_enabled) {
                auto validation = s.cert_store().validate(site->id);
                ssl_already_active = validation.valid;
            }
            if (ssl_already_active) {
                jobs.update(queued_job_id, "running", 90, "SSL already active");
            } else {
                auto ssl_result = issue_ssl_certificate(opts_ptr->domain, site->id, "letsencrypt");
                if (!ssl_result.success) {
                    fail(85, 8, "SSL issuance failed: " + ssl_result.message);
                    return;
                }
            }

            jobs.update(queued_job_id, "running", 95, "Finalizing migration");
            mark_steps(9);
            manifest = importer.inspect(*opts_ptr);
            if (!manifest.errors.empty()) {
                fail(95, 9, "Final analysis failed: " + manifest.errors.front());
                return;
            }
            if (!manifest.migration_completed && manifest.migration_stage != 3) {
                fail(95, 9, "Migration did not reach completed state");
                return;
            }

            mark_steps(10, true);
            jobs.update(queued_job_id, "completed", 100, "Migration completed");
        });

        if (!queued) {
            jobs.update(job_id, "failed", 0, "Task queue full");
            r.status_code = 503;
            r.body = "{\"success\":false,\"error\":\"Task queue full\"}";
            return r;
        }

        r.status_code = 202;
        r.body = "{\"success\":true,\"data\":{\"job_id\":" + std::to_string(job_id)
            + ",\"status\":\"pending\",\"status_url\":\"/api/jobs?id=" + std::to_string(job_id)
            + "\",\"message\":\"Migration queued\"}}";
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
