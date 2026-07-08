#include "ApiServer.h"
#include "api/JsonFormatter.h"
#include "core/Version.h"
#include "operations/SiteCreateOperation.h"
#include "operations/SiteRemoveOperation.h"

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

    router_.add("GET", "/api/ssl", [&s](const Request&) {
        Response r;
        r.body = JsonFormatter::success(JsonFormatter::ssl_certificates(s.ssl().list()));
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
            "Generating configuration", "Starting containers"
        });
        jobs.update(job_id, "running", 10);

        operations::SiteCreateOperation op(s.sites(), s.domains(),
            s.databases(), s.reverse_proxies(),
            s.proxy_provider(),
            s.filesystem(), s.config(), s.hosting_provider());
        auto result = op.execute(owner, domain, *node);

        if (result.success) {
            s.save();
            jobs.update(job_id, "completed", 100, "Site created successfully");
            std::ostringstream json;
            json << "{\"success\":true,\"data\":{\"domain\":\"" << domain << "\",\"message\":\"Site created\"}}";
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
        cert->enabled = enable;
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
