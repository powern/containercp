#include "CertificateStore.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace containercp::ssl {

CertificateStore::CertificateStore(logger::Logger& logger, const std::string& ssl_root)
    : logger_(logger)
    , ssl_root_(ssl_root)
{
}

std::string CertificateStore::site_dir(uint64_t site_id) const {
    return ssl_root_ + "/" + std::to_string(site_id);
}

std::string CertificateStore::metadata_path(uint64_t site_id) const {
    return site_dir(site_id) + "/metadata.json";
}

std::string CertificateStore::fullchain_path(uint64_t site_id) const {
    return site_dir(site_id) + "/fullchain.pem";
}

std::string CertificateStore::privkey_path(uint64_t site_id) const {
    return site_dir(site_id) + "/privkey.pem";
}

std::string CertificateStore::chain_path(uint64_t site_id) const {
    return site_dir(site_id) + "/chain.pem";
}

bool CertificateStore::ensure_site_dir(uint64_t site_id) {
    std::string dir = site_dir(site_id);
    if (::mkdir(dir.c_str(), 0700) == 0) {
        return true;
    }
    if (errno == EEXIST) {
        // Directory already exists — ensure correct permissions
        ::chmod(dir.c_str(), 0700);
        return true;
    }
    logger_.error("CertificateStore", "Failed to create directory " + dir);
    return false;
}

bool CertificateStore::metadata_exists(uint64_t site_id) const {
    struct stat st;
    return ::stat(metadata_path(site_id).c_str(), &st) == 0;
}

bool CertificateStore::certificate_files_exist(uint64_t site_id) const {
    struct stat st;
    return ::stat(fullchain_path(site_id).c_str(), &st) == 0
        && ::stat(privkey_path(site_id).c_str(), &st) == 0;
}

bool CertificateStore::atomic_write(const std::string& path, const std::string& content, int mode) {
    std::string tmp_path = path + ".tmp";

    // Write to temporary file
    int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) {
        logger_.error("CertificateStore", "Failed to open temp file: " + tmp_path);
        return false;
    }

    const char* data = content.data();
    size_t remaining = content.size();
    while (remaining > 0) {
        ssize_t written = ::write(fd, data, remaining);
        if (written <= 0) {
            ::close(fd);
            ::unlink(tmp_path.c_str());
            logger_.error("CertificateStore", "Failed to write temp file: " + tmp_path);
            return false;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }

    // fsync before rename
    if (::fsync(fd) < 0) {
        ::close(fd);
        ::unlink(tmp_path.c_str());
        logger_.error("CertificateStore", "fsync failed for: " + tmp_path);
        return false;
    }
    ::close(fd);

    // Atomic rename
    if (::rename(tmp_path.c_str(), path.c_str()) < 0) {
        ::unlink(tmp_path.c_str());
        logger_.error("CertificateStore", "rename failed: " + tmp_path + " -> " + path);
        return false;
    }

    // fsync the directory to ensure metadata is written
    std::string dir = path.substr(0, path.rfind('/'));
    int dir_fd = ::open(dir.c_str(), O_RDONLY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }

    return true;
}

std::string CertificateStore::read_file(const std::string& path) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool CertificateStore::save_metadata(uint64_t site_id, const Metadata& meta) {
    if (!ensure_site_dir(site_id)) {
        return false;
    }
    std::string json = metadata_to_json(meta);
    return atomic_write(metadata_path(site_id), json, 0644);
}

CertificateStore::Metadata CertificateStore::load_metadata(uint64_t site_id) {
    Metadata meta;
    std::string json = read_file(metadata_path(site_id));
    if (json.empty()) {
        return meta;
    }
    try {
        meta = metadata_from_json(json);
    } catch (...) {
        logger_.error("CertificateStore", "Failed to parse metadata for site " + std::to_string(site_id));
    }
    return meta;
}

bool CertificateStore::save_fullchain(uint64_t site_id, const std::string& pem_data) {
    if (!ensure_site_dir(site_id)) return false;
    return atomic_write(fullchain_path(site_id), pem_data, 0644);
}

bool CertificateStore::save_privkey(uint64_t site_id, const std::string& pem_data) {
    if (!ensure_site_dir(site_id)) return false;
    return atomic_write(privkey_path(site_id), pem_data, 0600);
}

bool CertificateStore::save_chain(uint64_t site_id, const std::string& pem_data) {
    if (!ensure_site_dir(site_id)) return false;
    return atomic_write(chain_path(site_id), pem_data, 0644);
}

core::OperationResult CertificateStore::save_all(uint64_t site_id, const Metadata& meta,
                                                  const std::string& fullchain_pem,
                                                  const std::string& privkey_pem,
                                                  const std::string& chain_pem) {
    if (!ensure_site_dir(site_id)) {
        return {false, "Failed to create site directory"};
    }
    if (!save_fullchain(site_id, fullchain_pem)) {
        return {false, "Failed to save fullchain.pem"};
    }
    if (!save_privkey(site_id, privkey_pem)) {
        return {false, "Failed to save privkey.pem"};
    }
    if (!save_chain(site_id, chain_pem)) {
        return {false, "Failed to save chain.pem"};
    }
    if (!save_metadata(site_id, meta)) {
        return {false, "Failed to save metadata.json"};
    }
    return {true, ""};
}

std::string CertificateStore::load_fullchain(uint64_t site_id) {
    return read_file(fullchain_path(site_id));
}

std::string CertificateStore::load_privkey(uint64_t site_id) {
    return read_file(privkey_path(site_id));
}

std::string CertificateStore::load_chain(uint64_t site_id) {
    return read_file(chain_path(site_id));
}

bool CertificateStore::remove_all(uint64_t site_id) {
    std::string dir = site_dir(site_id);

    ::unlink(metadata_path(site_id).c_str());
    ::unlink(fullchain_path(site_id).c_str());
    ::unlink(privkey_path(site_id).c_str());
    ::unlink(chain_path(site_id).c_str());

    // Remove temp files if any
    ::unlink((metadata_path(site_id) + ".tmp").c_str());
    ::unlink((fullchain_path(site_id) + ".tmp").c_str());
    ::unlink((privkey_path(site_id) + ".tmp").c_str());
    ::unlink((chain_path(site_id) + ".tmp").c_str());

    if (::rmdir(dir.c_str()) == 0) {
        return true;
    }
    // Directory may not exist — not an error
    if (errno == ENOENT) {
        return true;
    }
    logger_.warning("CertificateStore", "Could not remove directory: " + dir);
    return false;
}

std::vector<uint64_t> CertificateStore::enumerate() {
    std::vector<uint64_t> result;
    DIR* dir = ::opendir(ssl_root_.c_str());
    if (!dir) {
        return result;
    }
    struct dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;

            // Check if directory name is a numeric site ID
            bool is_numeric = true;
            for (char c : name) {
                if (!std::isdigit(c)) { is_numeric = false; break; }
            }
            if (is_numeric) {
                result.push_back(std::stoull(name));
            }
        }
    }
    ::closedir(dir);
    std::sort(result.begin(), result.end());
    return result;
}

CertificateStore::ValidationResult CertificateStore::validate(uint64_t site_id) {
    ValidationResult result;
    std::string dir = site_dir(site_id);

    // Check directory exists
    struct stat dir_st;
    if (::stat(dir.c_str(), &dir_st) != 0) {
        result.valid = false;
        result.errors.push_back("Site directory does not exist");
        return result;
    }

    // Check directory permissions
    if ((dir_st.st_mode & 0777) != 0700) {
        result.warnings.push_back("Directory permissions should be 0700");
    }

    // Check metadata.json
    struct stat meta_st;
    if (::stat(metadata_path(site_id).c_str(), &meta_st) != 0) {
        result.valid = false;
        result.errors.push_back("metadata.json not found");
    } else {
        if ((meta_st.st_mode & 0777) != 0644) {
            result.warnings.push_back("metadata.json permissions should be 0644");
        }

        Metadata meta = load_metadata(site_id);
        if (meta.version < 1) {
            result.warnings.push_back("metadata.json has invalid version");
        }
        if (meta.site_id != site_id) {
            result.warnings.push_back("metadata.json site_id mismatch");
        }
        if (meta.domains.empty()) {
            result.warnings.push_back("metadata.json has no domains");
        }
    }

    // Check certificate files if status is active or issuing
    Metadata meta = load_metadata(site_id);
    if (meta.status == "active" || meta.status == "issuing" || meta.status == "disabled") {
        struct stat st;

        if (::stat(fullchain_path(site_id).c_str(), &st) != 0) {
            result.errors.push_back("fullchain.pem not found");
        } else if ((st.st_mode & 0777) != 0644) {
            result.warnings.push_back("fullchain.pem permissions should be 0644");
        }

        if (::stat(privkey_path(site_id).c_str(), &st) != 0) {
            result.errors.push_back("privkey.pem not found");
        } else if ((st.st_mode & 0777) != 0600) {
            result.warnings.push_back("privkey.pem permissions should be 0600");
        }

        if (::stat(chain_path(site_id).c_str(), &st) != 0) {
            result.warnings.push_back("chain.pem not found (optional)");
        }

        // Check for empty files
        auto check_not_empty = [&](const std::string& path, const std::string& label) {
            struct stat fs;
            if (::stat(path.c_str(), &fs) == 0 && fs.st_size == 0) {
                result.errors.push_back(label + " is empty");
            }
        };
        check_not_empty(fullchain_path(site_id), "fullchain.pem");
        check_not_empty(privkey_path(site_id), "privkey.pem");
    }

    if (!result.errors.empty()) {
        result.valid = false;
    }
    return result;
}

// --- JSON helpers ---

std::string CertificateStore::escape_json(const std::string& s) const {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

void CertificateStore::skip_whitespace(const std::string& json, size_t& pos) const {
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'
           || json[pos] == '\n' || json[pos] == '\r')) {
        ++pos;
    }
}

std::string CertificateStore::parse_json_string(const std::string& json, size_t& pos) const {
    skip_whitespace(json, pos);
    if (pos >= json.size() || json[pos] != '"') {
        return "";
    }
    ++pos; // skip opening quote
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\') {
            ++pos;
            if (pos < json.size()) {
                switch (json[pos]) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default: result += json[pos];
                }
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }
    if (pos < json.size()) {
        ++pos; // skip closing quote
    }
    return result;
}

std::string CertificateStore::parse_json_value(const std::string& json, size_t& pos) const {
    skip_whitespace(json, pos);
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        return parse_json_string(json, pos);
    }
    if (json[pos] == '[') {
        // Array — return as raw substring for now
        size_t start = pos;
        int depth = 0;
        while (pos < json.size()) {
            if (json[pos] == '[') ++depth;
            if (json[pos] == ']') --depth;
            if (depth == 0) { ++pos; break; }
            ++pos;
        }
        return json.substr(start, pos - start);
    }
    // Number, boolean, or null — read until comma, ], whitespace
    size_t start = pos;
    while (pos < json.size() && json[pos] != ',' && json[pos] != ']'
           && json[pos] != '}' && json[pos] != ' ' && json[pos] != '\n'
           && json[pos] != '\r' && json[pos] != '\t') {
        ++pos;
    }
    return json.substr(start, pos - start);
}

std::string CertificateStore::metadata_to_json(const Metadata& meta) const {
    std::string json;
    json += "{\n";

    auto kv = [&](const std::string& key, const std::string& val, bool last = false) {
        json += "    \"" + key + "\": \"" + escape_json(val) + "\"";
        if (!last) json += ",";
        json += "\n";
    };
    auto kv_bool = [&](const std::string& key, bool val, bool last = false) {
        json += "    \"" + key + "\": " + (val ? "true" : "false");
        if (!last) json += ",";
        json += "\n";
    };
    auto kv_int = [&](const std::string& key, int val, bool last = false) {
        json += "    \"" + key + "\": " + std::to_string(val);
        if (!last) json += ",";
        json += "\n";
    };

    json += "    \"version\": " + std::to_string(meta.version) + ",\n";
    kv("site_id", std::to_string(meta.site_id));
    kv("provider_id", meta.provider_id);
    kv("certificate_type", meta.certificate_type);
    kv("status", meta.status);

    // domains array
    json += "    \"domains\": [";
    for (size_t i = 0; i < meta.domains.size(); ++i) {
        if (i > 0) json += ", ";
        json += "\"" + escape_json(meta.domains[i]) + "\"";
    }
    json += "],\n";

    kv("issued_at", meta.issued_at);
    kv("expires_at", meta.expires_at);
    kv("renew_after", meta.renew_after);

    kv_bool("https_enabled", meta.https_enabled);
    kv_bool("redirect_enabled", meta.redirect_enabled);
    kv_bool("auto_renew", meta.auto_renew);

    kv("challenge_type", meta.challenge_type);
    kv("last_validation", meta.last_validation);
    kv("last_error", meta.last_error);
    kv_int("renew_attempts", meta.renew_attempts);

    kv("fingerprint_sha256", meta.fingerprint_sha256);
    kv("serial_number", meta.serial_number);
    kv("issuer", meta.issuer);
    kv("subject", meta.subject);

    kv("created_at", meta.created_at);
    kv("updated_at", meta.updated_at, true);

    json += "}\n";
    return json;
}

CertificateStore::Metadata CertificateStore::metadata_from_json(const std::string& json) const {
    Metadata meta;
    size_t pos = 0;

    skip_whitespace(json, pos);
    if (pos >= json.size() || json[pos] != '{') return meta;
    ++pos; // skip {

    while (pos < json.size()) {
        skip_whitespace(json, pos);
        if (pos >= json.size() || json[pos] == '}') break;

        std::string key = parse_json_string(json, pos);
        skip_whitespace(json, pos);
        if (pos < json.size() && json[pos] == ':') ++pos;

        if (key == "version") {
            std::string val = parse_json_value(json, pos);
            meta.version = std::stoi(val);
        } else if (key == "site_id") {
            std::string val = parse_json_value(json, pos);
            meta.site_id = std::stoull(val);
        } else if (key == "provider_id") {
            meta.provider_id = parse_json_string(json, pos);
        } else if (key == "certificate_type") {
            meta.certificate_type = parse_json_string(json, pos);
        } else if (key == "status") {
            meta.status = parse_json_string(json, pos);
        } else if (key == "domains") {
            // Parse JSON array
            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == '[') {
                ++pos; // skip [
                while (pos < json.size()) {
                    skip_whitespace(json, pos);
                    if (pos >= json.size() || json[pos] == ']') break;
                    std::string d = parse_json_string(json, pos);
                    if (!d.empty()) {
                        meta.domains.push_back(d);
                    }
                    skip_whitespace(json, pos);
                    if (pos < json.size() && json[pos] == ',') ++pos;
                }
                if (pos < json.size() && json[pos] == ']') ++pos;
            }
        } else if (key == "issued_at") {
            meta.issued_at = parse_json_string(json, pos);
        } else if (key == "expires_at") {
            meta.expires_at = parse_json_string(json, pos);
        } else if (key == "renew_after") {
            meta.renew_after = parse_json_string(json, pos);
        } else if (key == "https_enabled") {
            std::string val = parse_json_value(json, pos);
            meta.https_enabled = (val == "true");
        } else if (key == "redirect_enabled") {
            std::string val = parse_json_value(json, pos);
            meta.redirect_enabled = (val == "true");
        } else if (key == "auto_renew") {
            std::string val = parse_json_value(json, pos);
            meta.auto_renew = (val == "true");
        } else if (key == "challenge_type") {
            meta.challenge_type = parse_json_string(json, pos);
        } else if (key == "last_validation") {
            meta.last_validation = parse_json_string(json, pos);
        } else if (key == "last_error") {
            meta.last_error = parse_json_string(json, pos);
        } else if (key == "renew_attempts") {
            std::string val = parse_json_value(json, pos);
            meta.renew_attempts = std::stoi(val);
        } else if (key == "fingerprint_sha256") {
            meta.fingerprint_sha256 = parse_json_string(json, pos);
        } else if (key == "serial_number") {
            meta.serial_number = parse_json_string(json, pos);
        } else if (key == "issuer") {
            meta.issuer = parse_json_string(json, pos);
        } else if (key == "subject") {
            meta.subject = parse_json_string(json, pos);
        } else if (key == "created_at") {
            meta.created_at = parse_json_string(json, pos);
        } else if (key == "updated_at") {
            meta.updated_at = parse_json_string(json, pos);
        } else {
            // Unknown field — skip value
            parse_json_value(json, pos);
        }

        skip_whitespace(json, pos);
        if (pos < json.size() && json[pos] == ',') ++pos;
    }

    return meta;
}

std::string CertificateStore::timestamp_utc() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    gmtime_r(&time_t_now, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

std::string CertificateStore::domains_to_string(const std::vector<std::string>& domains) {
    std::string result;
    for (size_t i = 0; i < domains.size(); ++i) {
        if (i > 0) result += ",";
        result += domains[i];
    }
    return result;
}

std::vector<std::string> CertificateStore::string_to_domains(const std::string& str) {
    std::vector<std::string> result;
    if (str.empty()) return result;
    std::istringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (!token.empty()) {
            result.push_back(token);
        }
    }
    return result;
}

} // namespace containercp::ssl
