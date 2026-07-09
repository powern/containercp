#include "ApiServer.h"
#include "api/JsonFormatter.h"
#include "core/Version.h"
#include "jobs/JobManager.h"
#include "operations/SiteCreateOperation.h"
#include "operations/SiteRemoveOperation.h"

#include "ssl/CertificateStore.h"
#include "ssl/CertificateProvider.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace containercp::api {

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

    router_.add("GET", "/api/health", [](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::health(true));
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
        r.body = JsonFormatter::success(JsonFormatter::domains(s.domains().list()));
        return r;
    });

    router_.add("GET", "/api/proxy", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::proxies(s.reverse_proxies().list()));
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
            r.status_code = 404;
            r.body = json_error("NOT_FOUND", "Site not found: " + domain);
            return r;
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

        // Find site
        uint64_t site_id = 0;
        for (const auto& site : s.sites().list()) {
            if (site.name == domain) {
                site_id = site.id;
                break;
            }
        }
        if (site_id == 0) {
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
            auto proxy_result = s.proxy_provider().attach_certificate(domain, cert_path, key_path);
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
            auto proxy_result = s.proxy_provider().attach_certificate(domain, cert_path, key_path);
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
