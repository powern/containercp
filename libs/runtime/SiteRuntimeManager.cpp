#include "SiteRuntimeManager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

namespace containercp::runtime {

namespace {

std::string trim(const std::string& s) {
    size_t start = 0, end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\n' || s[start] == '\r')) ++start;
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\n' || s[end-1] == '\r')) --end;
    return s.substr(start, end - start);
}

std::string json_string_field(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            val += json[pos + 1];
            pos += 2;
        } else {
            val += json[pos++];
        }
    }
    return val;
}

bool json_bool_field(const std::string& json, const std::string& key, bool def) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return def;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (json.substr(pos, 4) == "true") return true;
    if (json.substr(pos, 5) == "false") return false;
    return def;
}

} // anonymous namespace

std::string SiteRuntimeManager::path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    bool a_has_slash = (a.back() == '/');
    bool b_has_slash = (b.front() == '/');
    if (a_has_slash && b_has_slash) return a + b.substr(1);
    if (!a_has_slash && !b_has_slash) return a + '/' + b;
    return a + b;
}

SiteRuntimeManager::SiteRuntimeManager(logger::Logger& logger,
                                       const std::string& sites_root,
                                       const std::string& ssl_root)
    : logger_(logger)
    , sites_root_(sites_root)
    , ssl_root_(ssl_root)
{
    // Normalize: strip trailing slash for consistent joins
    if (!sites_root_.empty() && sites_root_.back() == '/') {
        sites_root_.pop_back();
    }
    if (!ssl_root_.empty() && ssl_root_.back() == '/') {
        ssl_root_.pop_back();
    }
}

std::string SiteRuntimeManager::container_status(const std::string& compose_dir,
                                                  const std::string& service) const {
    auto ps_result = executor_.run({
        "docker", "compose", "--project-directory", compose_dir,
        "ps", "--format", "{{.Name}}", service
    });

    if (ps_result.exit_code != 0) {
        logger_.error("SITE_RT",
            "docker compose ps failed for " + compose_dir + "/" + service +
            " exit=" + std::to_string(ps_result.exit_code) +
            " stderr=" + trim(ps_result.err));
        return "Error";
    }

    std::string container_name = trim(ps_result.out);
    if (container_name.empty()) {
        return "Stopped";
    }

    auto inspect_result = executor_.run({
        "docker", "inspect", container_name,
        "--format", "{{.State.Status}}|{{.State.Health.Status}}"
    });

    if (inspect_result.exit_code != 0 || inspect_result.out.empty()) {
        logger_.error("SITE_RT",
            "docker inspect failed for " + container_name +
            " exit=" + std::to_string(inspect_result.exit_code) +
            " stderr=" + trim(inspect_result.err));
        return "Error";
    }

    std::string combined = trim(inspect_result.out);
    size_t sep = combined.find('|');
    std::string state = (sep != std::string::npos) ? combined.substr(0, sep) : combined;
    std::string health = (sep != std::string::npos) ? combined.substr(sep + 1) : "";

    if (state == "running") {
        if (health == "unhealthy") return "Unhealthy";
        if (health == "starting") return "Starting";
        return "Running";
    }
    if (state == "exited" || state == "paused" || state == "removing") return "Stopped";
    if (state == "restarting") return "Starting";
    if (state == "created") return "Stopped";
    return "Unknown";
}

std::string SiteRuntimeManager::https_status_from_metadata(const std::string& ssl_root,
                                                            uint64_t site_id) {
    // Versioned layout: <ssl_root>/<site_id>/current/metadata.json
    // Flat layout fallback:  <ssl_root>/<site_id>/metadata.json
    std::string site_dir = path_join(ssl_root, std::to_string(site_id));
    std::string path = path_join(path_join(site_dir, "current"), "metadata.json");

    std::ifstream f(path);
    if (!f.is_open()) {
        // Fallback: flat layout (legacy)
        path = path_join(site_dir, "metadata.json");
        f.open(path);
        if (!f.is_open()) {
            return "Disabled";
        }
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();

    std::string status = json_string_field(json, "status");
    if (status == "error") {
        return "Error";
    }
    if (status == "issuing") {
        return "Issuing";
    }

    if (status.empty() || status == "http_only" || status == "disabled") {
        return "Disabled";
    }

    bool https_enabled = json_bool_field(json, "https_enabled", false);
    if (!https_enabled) {
        return "Disabled";
    }

    if (status == "active" || status == "issued") {
        std::string expires = json_string_field(json, "expires_at");
        if (!expires.empty()) {
            struct tm tm = {};
            int y, M, d, h, m;
            double s;
            if (sscanf(expires.c_str(), "%d-%d-%dT%d:%d:%lf", &y, &M, &d, &h, &m, &s) >= 3) {
                tm.tm_year = y - 1900;
                tm.tm_mon = M - 1;
                tm.tm_mday = d;
                tm.tm_hour = h;
                tm.tm_min = m;
                tm.tm_sec = static_cast<int>(s);
                time_t expiry_t = timegm(&tm);
                time_t now = time(nullptr);
                double days_remaining = difftime(expiry_t, now) / 86400.0;
                if (days_remaining < 0) return "Expired";
                if (days_remaining < 30) return "Expiring";
            }
        }
        return "Active";
    }

    return "Disabled";
}

SiteRuntimeStatus SiteRuntimeManager::get_status(uint64_t site_id,
                                                  const std::string& domain) const {
    SiteRuntimeStatus s;
    std::string compose_dir = path_join(sites_root_, domain);

    s.web.status = container_status(compose_dir, "web");
    s.php.status = container_status(compose_dir, "php");
    s.web.name = domain + "-web";
    s.php.name = domain + "-php";

    s.https_status = https_status_from_metadata(ssl_root_, site_id);

    return s;
}

} // namespace containercp::runtime
