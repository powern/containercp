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

std::string CertificateStore::staging_dir(uint64_t site_id, const std::string& stamp) const {
    return site_dir(site_id) + "/.staging-" + stamp;
}

bool CertificateStore::ensure_site_dir(uint64_t site_id) {
    std::string dir = site_dir(site_id);
    if (::mkdir(dir.c_str(), 0700) == 0) {
        return true;
    }
    if (errno == EEXIST) {
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

    if (::fsync(fd) < 0) {
        ::close(fd);
        ::unlink(tmp_path.c_str());
        logger_.error("CertificateStore", "fsync failed for: " + tmp_path);
        return false;
    }
    ::close(fd);

    if (::rename(tmp_path.c_str(), path.c_str()) < 0) {
        ::unlink(tmp_path.c_str());
        logger_.error("CertificateStore", "rename failed: " + tmp_path + " -> " + path);
        return false;
    }

    std::string dir = path.substr(0, path.rfind('/'));
    int dir_fd = ::open(dir.c_str(), O_RDONLY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }

    return true;
}

bool CertificateStore::atomic_write_in_dir(const std::string& dir, const std::string& filename,
                                            const std::string& content, int mode) {
    return atomic_write(dir + "/" + filename, content, mode);
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

CertificateStore::MetadataLoadResult CertificateStore::load_metadata(uint64_t site_id) {
    MetadataLoadResult result;
    std::string path = metadata_path(site_id);

    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        result.error = LoadError::NOT_FOUND;
        result.message = "metadata.json not found for site " + std::to_string(site_id);
        return result;
    }

    std::string json = read_file(path);
    if (json.empty()) {
        result.error = LoadError::IO_ERROR;
        result.message = "metadata.json is empty for site " + std::to_string(site_id);
        return result;
    }

    try {
        result.metadata = metadata_from_json(json);
    } catch (const std::exception& e) {
        result.error = LoadError::INVALID_JSON;
        result.message = std::string("Failed to parse metadata.json: ") + e.what();
        return result;
    } catch (...) {
        result.error = LoadError::INVALID_JSON;
        result.message = "Failed to parse metadata.json: unknown error";
        return result;
    }

    if (result.metadata.version < 1) {
        result.error = LoadError::INVALID_JSON;
        result.message = "Unsupported metadata version: " + std::to_string(result.metadata.version);
        return result;
    }

    if (result.metadata.site_id != site_id) {
        result.error = LoadError::INVALID_SCHEMA;
        result.message = "metadata.json site_id mismatch: expected " + std::to_string(site_id)
                        + ", got " + std::to_string(result.metadata.site_id);
        return result;
    }

    result.success = true;
    return result;
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

    // Use a unique staging directory for transactional writes
    std::string stamp = std::to_string(::time(nullptr)) + "-" + std::to_string(::getpid());
    std::string sdir = staging_dir(site_id, stamp);

    if (::mkdir(sdir.c_str(), 0700) != 0) {
        logger_.error("CertificateStore", "Failed to create staging directory: " + sdir);
        return {false, "Failed to create staging directory"};
    }

    // Write all files to staging directory first
    bool all_ok = true;
    all_ok = all_ok && atomic_write_in_dir(sdir, "fullchain.pem", fullchain_pem, 0644);
    all_ok = all_ok && atomic_write_in_dir(sdir, "privkey.pem", privkey_pem, 0600);
    all_ok = all_ok && atomic_write_in_dir(sdir, "chain.pem", chain_pem, 0644);
    all_ok = all_ok && atomic_write_in_dir(sdir, "metadata.json", metadata_to_json(meta), 0644);

    if (!all_ok) {
        // Rollback: remove staging directory and all its contents
        ::unlink((sdir + "/fullchain.pem").c_str());
        ::unlink((sdir + "/privkey.pem").c_str());
        ::unlink((sdir + "/chain.pem").c_str());
        ::unlink((sdir + "/metadata.json").c_str());
        ::rmdir(sdir.c_str());
        logger_.error("CertificateStore", "save_all failed, staging directory rolled back: " + sdir);
        return {false, "Failed to write certificate files"};
    }

    // Fsync the staging directory to ensure all data is on disk
    int dir_fd = ::open(sdir.c_str(), O_RDONLY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }

    // Atomically move each file from staging to final location
    auto move_file = [&](const std::string& filename) -> bool {
        std::string src = sdir + "/" + filename;
        std::string dst = site_dir(site_id) + "/" + filename;
        if (::rename(src.c_str(), dst.c_str()) != 0) {
            logger_.error("CertificateStore", "rename failed: " + src + " -> " + dst);
            return false;
        }
        return true;
    };

    bool move_ok = true;
    move_ok = move_ok && move_file("fullchain.pem");
    move_ok = move_ok && move_file("privkey.pem");
    move_ok = move_ok && move_file("chain.pem");
    move_ok = move_ok && move_file("metadata.json");

    // Remove staging directory (should be empty after renames)
    ::rmdir(sdir.c_str());

    if (!move_ok) {
        // Critical failure: rename of one or more files failed.
        // Try to restore files that were already renamed back to staging.
        // Best-effort rollback of the staging directory contents.
        ::unlink((sdir + "/fullchain.pem").c_str());
        ::unlink((sdir + "/privkey.pem").c_str());
        ::unlink((sdir + "/chain.pem").c_str());
        ::unlink((sdir + "/metadata.json").c_str());
        ::rmdir(sdir.c_str());
        logger_.error("CertificateStore", "save_all rename failed, staging cleaned up");
        return {false, "Failed to finalize certificate files"};
    }

    // Fsync the site directory to commit the renames
    int site_fd = ::open(site_dir(site_id).c_str(), O_RDONLY);
    if (site_fd >= 0) {
        ::fsync(site_fd);
        ::close(site_fd);
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

    // Remove any staging directories
    DIR* d = ::opendir(dir.c_str());
    if (d) {
        struct dirent* entry;
        while ((entry = ::readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name.find(".staging-") == 0) {
                // Remove contents then directory
                std::string sub = dir + "/" + name;
                ::unlink((sub + "/fullchain.pem").c_str());
                ::unlink((sub + "/privkey.pem").c_str());
                ::unlink((sub + "/chain.pem").c_str());
                ::unlink((sub + "/metadata.json").c_str());
                ::rmdir(sub.c_str());
            }
        }
        ::closedir(d);
    }

    if (::rmdir(dir.c_str()) == 0) {
        return true;
    }
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
            if (name == "." || name == ".." || name.find(".staging-") == 0) continue;

            bool is_numeric = true;
            for (char c : name) {
                if (!std::isdigit(static_cast<unsigned char>(c))) { is_numeric = false; break; }
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

    struct stat dir_st;
    if (::stat(dir.c_str(), &dir_st) != 0) {
        result.valid = false;
        result.errors.push_back("Site directory does not exist");
        return result;
    }

    if ((dir_st.st_mode & 0777) != 0700) {
        result.warnings.push_back("Directory permissions should be 0700");
    }

    // Load metadata with proper error handling
    auto load_result = load_metadata(site_id);
    if (!load_result.success) {
        result.valid = false;
        result.errors.push_back(load_error_string(load_result.error) + ": " + load_result.message);
        return result;
    }

    const Metadata& meta = load_result.metadata;

    if (meta.domains.empty()) {
        result.warnings.push_back("metadata.json has no domains");
    }

    // Check certificate files if status expects them
    if (meta.status == "active" || meta.status == "issuing" || meta.status == "disabled") {
        auto check_file = [&](const std::string& path, const std::string& label, mode_t expected_mode) {
            struct stat st;
            if (::stat(path.c_str(), &st) != 0) {
                result.errors.push_back(label + " not found");
                return;
            }
            if ((st.st_mode & 0777) != expected_mode) {
                result.warnings.push_back(label + " permissions should be " +
                    std::to_string(expected_mode));
            }
            if (st.st_size == 0) {
                result.errors.push_back(label + " is empty");
            }
        };

        check_file(fullchain_path(site_id), "fullchain.pem", 0644);
        check_file(privkey_path(site_id), "privkey.pem", 0600);

        struct stat chain_st;
        if (::stat(chain_path(site_id).c_str(), &chain_st) != 0) {
            result.warnings.push_back("chain.pem not found (optional)");
        }
    }

    if (!result.errors.empty()) {
        result.valid = false;
    }
    return result;
}

std::string CertificateStore::load_error_string(LoadError err) {
    switch (err) {
        case LoadError::NONE: return "OK";
        case LoadError::NOT_FOUND: return "NOT_FOUND";
        case LoadError::INVALID_JSON: return "INVALID_JSON";
        case LoadError::UNSUPPORTED_VERSION: return "UNSUPPORTED_VERSION";
        case LoadError::IO_ERROR: return "IO_ERROR";
        case LoadError::INVALID_SCHEMA: return "INVALID_SCHEMA";
    }
    return "UNKNOWN";
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
    ++pos;
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
        ++pos;
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
    bool parsed_any = false;

    skip_whitespace(json, pos);
    if (pos >= json.size() || json[pos] != '{') return meta;
    ++pos;

    while (pos < json.size()) {
        skip_whitespace(json, pos);
        if (pos >= json.size() || json[pos] == '}') break;

        std::string key = parse_json_string(json, pos);
        if (key.empty()) {
            // Malformed key — skip to next comma or closing brace
            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == ':') ++pos;
            parse_json_value(json, pos);
            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == ',') ++pos;
            continue;
        }
        skip_whitespace(json, pos);
        if (pos < json.size() && json[pos] == ':') ++pos;

        if (key == "version") {
            parsed_any = true;
            std::string val = parse_json_value(json, pos);
            meta.version = std::stoi(val);
        } else if (key == "site_id") {
            parsed_any = true;
            std::string val = parse_json_value(json, pos);
            meta.site_id = std::stoull(val);
        } else if (key == "provider_id") {
            parsed_any = true;
            meta.provider_id = parse_json_string(json, pos);
        } else if (key == "certificate_type") {
            parsed_any = true;
            meta.certificate_type = parse_json_string(json, pos);
        } else if (key == "status") {
            parsed_any = true;
            meta.status = parse_json_string(json, pos);
        } else if (key == "domains") {
            parsed_any = true;
            skip_whitespace(json, pos);
            if (pos < json.size() && json[pos] == '[') {
                ++pos;
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
            parsed_any = true;
            meta.issued_at = parse_json_string(json, pos);
        } else if (key == "expires_at") {
            parsed_any = true;
            meta.expires_at = parse_json_string(json, pos);
        } else if (key == "renew_after") {
            parsed_any = true;
            meta.renew_after = parse_json_string(json, pos);
        } else if (key == "https_enabled") {
            parsed_any = true;
            std::string val = parse_json_value(json, pos);
            meta.https_enabled = (val == "true");
        } else if (key == "redirect_enabled") {
            parsed_any = true;
            std::string val = parse_json_value(json, pos);
            meta.redirect_enabled = (val == "true");
        } else if (key == "auto_renew") {
            parsed_any = true;
            std::string val = parse_json_value(json, pos);
            meta.auto_renew = (val == "true");
        } else if (key == "challenge_type") {
            parsed_any = true;
            meta.challenge_type = parse_json_string(json, pos);
        } else if (key == "last_validation") {
            parsed_any = true;
            meta.last_validation = parse_json_string(json, pos);
        } else if (key == "last_error") {
            parsed_any = true;
            meta.last_error = parse_json_string(json, pos);
        } else if (key == "renew_attempts") {
            parsed_any = true;
            std::string val = parse_json_value(json, pos);
            meta.renew_attempts = std::stoi(val);
        } else if (key == "fingerprint_sha256") {
            parsed_any = true;
            meta.fingerprint_sha256 = parse_json_string(json, pos);
        } else if (key == "serial_number") {
            parsed_any = true;
            meta.serial_number = parse_json_string(json, pos);
        } else if (key == "issuer") {
            parsed_any = true;
            meta.issuer = parse_json_string(json, pos);
        } else if (key == "subject") {
            parsed_any = true;
            meta.subject = parse_json_string(json, pos);
        } else if (key == "created_at") {
            parsed_any = true;
            meta.created_at = parse_json_string(json, pos);
        } else if (key == "updated_at") {
            parsed_any = true;
            meta.updated_at = parse_json_string(json, pos);
        } else {
            // Unknown field — skip value
            parse_json_value(json, pos);
        }

        skip_whitespace(json, pos);
        if (pos < json.size() && json[pos] == ',') ++pos;
    }

    // If JSON was not empty but no known fields were parsed, it's invalid
    if (!parsed_any && json.size() > 2) {
        meta.version = 0; // Signal invalid parse
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
