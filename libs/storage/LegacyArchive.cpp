#include "LegacyArchive.h"
#include "LegacyDatasetReader.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <ctime>
#include <cerrno>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace containercp::storage {
namespace fs = std::filesystem;

// ============================================================
// Utilities
// ============================================================

LegacyArchive::LegacyArchive(const std::string& source_directory,
                             const std::string& archive_root)
    : source_dir_(source_directory), archive_root_(archive_root)
{
    if (!source_dir_.empty() && source_dir_.back() != '/') source_dir_ += '/';
    if (!archive_root_.empty() && archive_root_.back() != '/') archive_root_ += '/';
}

std::string LegacyArchive::generate_uuid() {
    std::random_device rd; std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string uuid(36, '-');
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        uuid[i] = hex[dis(gen)];
    }
    uuid[14] = '4'; // UUID v4 version
    // Variant: position 19 = 8,9,a,b
    uuid[19] = hex[8 + dis(gen) % 4];
    return uuid;
}

std::string LegacyArchive::timestamp_utc() {
    time_t now = time(nullptr); struct tm* utc = gmtime(&now);
    std::ostringstream ss; ss << std::put_time(utc, "%Y%m%dT%H%M%SZ"); return ss.str();
}

std::string LegacyArchive::json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: if (c < 0x20) { char buf[8]; snprintf(buf, 8, "\\u%04X", c); out += buf; }
                 else out += c;
        }
    }
    return out;
}

bool LegacyArchive::valid_migration_id(const std::string& id) {
    if (id.size() != 36) return false;
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (id[i] != '-') return false; }
        else if (id[i] >= '0' && id[i] <= '9') continue;
        else if (id[i] >= 'a' && id[i] <= 'f') continue; // lowercase only
        else return false;
    }
    if (id[14] != '4') return false; // UUID v4 version
    char vc = id[19];
    if (vc != '8' && vc != '9' && vc != 'a' && vc != 'b') return false; // variant
    return true;
}

bool LegacyArchive::safe_version(const std::string& v) {
    if (v.empty() || v[0] != 'v') return false;
    // Format: v<num>.<num>.<num>[-suffix]
    size_t pos = 1; // after 'v'
    auto parse_num = [&]() -> bool {
        if (pos >= v.size() || !isdigit(v[pos])) return false;
        while (pos < v.size() && isdigit(v[pos])) ++pos;
        return true;
    };
    if (!parse_num()) return false;
    if (pos >= v.size() || v[pos] != '.') return false; ++pos;
    if (!parse_num()) return false;
    if (pos >= v.size() || v[pos] != '.') return false; ++pos;
    if (!parse_num()) return false;
    // Optional suffix: -alphanumeric
    if (pos < v.size()) {
        if (v[pos] != '-') return false; ++pos;
        if (pos >= v.size()) return false;
        while (pos < v.size()) {
            char c = v[pos];
            if (!isalnum(c) && c != '.' && c != '-') return false;
            ++pos;
        }
    }
    return pos == v.size();
}

std::string LegacyArchive::sha256_file(const std::string& path) {
    unsigned char hash[SHA256_DIGEST_LENGTH]; SHA256_CTX ctx;
    SHA256_Init(&ctx);
    std::ifstream f(path, std::ios::binary); if (!f) return "";
    char buf[8192];
    while (f.read(buf, sizeof(buf)).gcount() > 0) SHA256_Update(&ctx, buf, f.gcount());
    if (f.bad()) return "";
    SHA256_Final(hash, &ctx); std::string out; out.reserve(64);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        out += "0123456789abcdef"[(hash[i] >> 4) & 0xf];
        out += "0123456789abcdef"[hash[i] & 0xf];
    }
    return out;
}

static uint64_t file_size_or_zero(const std::string& path) {
    std::error_code ec; auto sz = fs::file_size(path, ec); return ec ? 0 : sz;
}

static bool file_unchanged(const std::string& path, uint64_t sz, const std::string& sha) {
    return file_size_or_zero(path) == sz && LegacyArchive::sha256_file(path) == sha;
}

LegacyArchive::RecordCountResult LegacyArchive::count_records(const std::string& sd, const std::string& fn) {
    RecordCountResult res;
    LegacyDatasetReader reader(sd);

    std::string sp = sd + fn;
    std::error_code ec;
    if (!fs::exists(sp, ec) || ec) { res.success = true; res.record_count = 0; return res; }
    if (fs::file_size(fs::path(sp), ec) == 0 && !ec) { res.success = true; res.record_count = 0; return res; }

    res.success = true;

    if (fn == "profiles.db") { auto dr = reader.read_profiles_only(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); return res; }
    if (fn == "template_profiles.db") { auto dr = reader.read_templates_only(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); return res; }

    if (fn == "mail_state.db") { auto dr = reader.read_mail_config(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.module_state_present ? 1 : 0; return res; }
    if (fn == "mail_smarthost.db") { auto dr = reader.read_mail_config(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.smarthost_present ? 1 : 0; return res; }

    if (fn == "nodes.db") { auto dr = reader.read_nodes(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "php_versions.db") { auto dr = reader.read_php_versions(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "users.db") { auto dr = reader.read_users(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "sites.db") { auto dr = reader.read_sites(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "domains.db") { auto dr = reader.read_domains(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "databases.db") { auto dr = reader.read_databases(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "backups.db") { auto dr = reader.read_backups(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "reverse_proxies.db") { auto dr = reader.read_reverse_proxies(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "access_users.db") { auto dr = reader.read_access_users(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "access_grants.db") { auto dr = reader.read_access_grants(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "auth_users.db") { auto dr = reader.read_auth_users(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "ssl_certificates.db") { auto dr = reader.read_ssl_certificates(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "mail_domains.db") { auto dr = reader.read_mail_domains(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "mail_mailboxes.db") { auto dr = reader.read_mailboxes(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else if (fn == "mail_aliases.db") { auto dr = reader.read_mail_aliases(); if (!dr.success) { res.error = "parse_failed"; res.success = false; return res; } res.record_count = dr.records.size(); }
    else { res.error = "unknown_file"; res.success = false; }
    return res;
}

// ============================================================
namespace {

struct ParsedFileEntry {
    std::string filename;
    uint64_t size = 0;
    std::string sha256;
    uint64_t record_count = 0;
    bool optional = false;
    bool present = false;
    bool valid = false;
};

struct JsonError { const char* msg; int pos; };

class ManifestParser {
    const std::string& json_;
    size_t pos_ = 0;

    char peek() const { return pos_ < json_.size() ? json_[pos_] : '\0'; }
    char next() { return pos_ < json_.size() ? json_[pos_++] : '\0'; }
    void skip_ws() { while (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r') next(); }

    JsonError error(const char* msg) { return {msg, static_cast<int>(pos_)}; }

    // Parse JSON string with escaping
    bool parse_string(std::string& out) {
        if (next() != '"') return false;
        out.clear();
        while (peek() != '"' && peek() != '\0') {
            if (peek() == '\\') { next();
                switch (next()) {
                case '"': out += '"'; break; case '\\': out += '\\'; break;
                case '/': out += '/'; break; case 'n': out += '\n'; break;
                case 'r': out += '\r'; break; case 't': out += '\t'; break;
                case 'u': { // \uXXXX — accept 4 hex digits
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char c = next();
                        if (c >= '0' && c <= '9') cp = cp * 16 + (c - '0');
                        else if (c >= 'a' && c <= 'f') cp = cp * 16 + (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') cp = cp * 16 + (c - 'A' + 10);
                        else return false;
                    }
                    if (cp <= 0x7F) out += static_cast<char>(cp);
                    else if (cp <= 0x7FF) { out += static_cast<char>(0xC0 | (cp>>6)); out += static_cast<char>(0x80 | (cp&0x3F)); }
                    else { out += static_cast<char>(0xE0 | (cp>>12)); out += static_cast<char>(0x80 | ((cp>>6)&0x3F)); out += static_cast<char>(0x80 | (cp&0x3F)); }
                    break;
                }
                default: return false;
                }
            } else if (peek() < 0x20) return false; // control character
            else out += next();
        }
        if (next() != '"') return false;
        return true;
    }

    // Parse integer
    bool parse_int(int64_t& out) {
        skip_ws();
        if (peek() == '\0') return false;
        bool neg = false;
        if (peek() == '-') { neg = true; next(); }
        if (peek() < '0' || peek() > '9') return false;
        out = 0;
        while (peek() >= '0' && peek() <= '9') {
            int64_t prev = out;
            out = out * 10 + (next() - '0');
            if (out < prev) return false; // overflow
        }
        if (neg) out = -out;
        return true;
    }

    // Parse boolean
    bool parse_bool(bool& out) {
        skip_ws();
        if (json_.compare(pos_, 4, "true") == 0) { out = true; pos_ += 4; return true; }
        if (json_.compare(pos_, 5, "false") == 0) { out = false; pos_ += 5; return true; }
        return false;
    }

public:
    explicit ManifestParser(const std::string& json) : json_(json) {}

    // Parse the manifest object: returns true and fills map on success
    bool parse_manifest(std::map<std::string, std::string>& strings,
                        std::map<std::string, int64_t>& ints,
                        std::map<std::string, bool>& bools,
                        std::vector<ParsedFileEntry>& file_entries)
    {
        skip_ws();
        if (next() != '{') return false;

        std::set<std::string> seen_keys;
        bool first = true;
        while (true) {
            skip_ws();
            if (peek() == '}') { if (first) { next(); break; } else { next(); break; } }
            if (!first) { if (peek() != ',') return false; next(); skip_ws(); }
            first = false;

            // Parse key
            if (peek() != '"') return false;
            std::string key;
            if (!parse_string(key)) return false;
            if (seen_keys.count(key)) return false; // duplicate key
            seen_keys.insert(key);

            skip_ws();
            if (next() != ':') return false;
            skip_ws();

            // Parse value
            if (key == "files") {
                if (next() != '[') return false;
                bool fa_first = true;
                while (true) {
                    skip_ws();
                    if (peek() == ']') { next(); break; }
                    if (!fa_first) { if (peek() != ',') return false; next(); skip_ws(); }
                    fa_first = false;
                    if (next() != '{') return false;
                    ParsedFileEntry pfe;
                    bool fe_first = true;
                    std::set<std::string> file_seen;
                    while (true) {
                        skip_ws();
                        if (peek() == '}') { next(); break; }
                        if (!fe_first) { if (peek() != ',') return false; next(); skip_ws(); }
                        fe_first = false;
                        std::string fk;
                        if (!parse_string(fk)) return false;
                        if (file_seen.count(fk)) return false;
                        file_seen.insert(fk);
                        skip_ws();
                        if (next() != ':') return false;
                        skip_ws();
                        // Parse typed value
                        if (fk == "filename") {
                            if (peek() != '"') return false;
                            if (!parse_string(pfe.filename)) return false;
                        } else if (fk == "sha256") {
                            if (peek() != '"') return false;
                            if (!parse_string(pfe.sha256)) return false;
                        } else if (fk == "size" || fk == "record_count") {
                            int64_t n;
                            if (peek() == '"') return false; // string not allowed
                            if (!parse_int(n) || n < 0) return false;
                            if (fk == "size") pfe.size = static_cast<uint64_t>(n);
                            else pfe.record_count = static_cast<uint64_t>(n);
                        } else if (fk == "optional" || fk == "present") {
                            bool b;
                            if (!parse_bool(b)) return false;
                            if (fk == "optional") pfe.optional = b;
                            else pfe.present = b;
                        } else { return false; } // unknown field
                    }
                    static const std::set<std::string> kRequired = {"filename","sha256","size","record_count","optional","present"};
                    if (file_seen.size() != kRequired.size()) return false;
                    for (auto& r : kRequired) if (!file_seen.count(r)) return false;
                    pfe.valid = true;
                }
            } else if (peek() == '"') {
                std::string val;
                if (!parse_string(val)) return false;
                strings[key] = val;
                // String keys: only the 10 known string fields
                static const char* kStr[] = {"manifest_version","migration_id","source_version","target_version","migration_timestamp","source_directory","archive_directory","verification_result","initial_integrity_check","reopened_integrity_check",nullptr};
                bool known = false; for (int ki = 0; kStr[ki]; ++ki) if (kStr[ki] == key) { known = true; break; }
                if (!known) return false;
            } else if (peek() == 't' || peek() == 'f') {
                bool val;
                if (!parse_bool(val)) return false;
                bools[key] = val;
                if (key != "checksum_match") return false;
            } else {
                int64_t val;
                if (!parse_int(val)) return false;
                ints[key] = val;
                if (key != "initial_fk_violations" && key != "reopened_fk_violations") return false;
            }
        }
        skip_ws();
        return pos_ == json_.size(); // no trailing garbage
    }
};

} // namespace
// create_archive
// ============================================================

ArchiveResult LegacyArchive::create_archive(
    const std::string& migration_id,
    const std::string& source_version,
    const std::string& target_version,
    const DatabaseVerificationResult& verification_result)
{
    ArchiveResult result;

    // Validate inputs
    if (!valid_migration_id(migration_id)) { result.error = "invalid_migration_id"; return result; }
    if (!safe_version(source_version)) { result.error = "invalid_source_version"; return result; }
    if (!safe_version(target_version)) { result.error = "invalid_target_version"; return result; }

    // Top-level filesystem exception boundary
    try {

    // Complete Phase 9 check: summaries + per-resource validation
    if (!verification_result.success ||
        !verification_result.initial_verification_passed ||
        !verification_result.reopened_verification_passed ||
        !verification_result.reopen_succeeded ||
        verification_result.initial_integrity_check_result != "ok" ||
        verification_result.reopened_integrity_check_result != "ok" ||
        !verification_result.initial_foreign_key_violations.empty() ||
        !verification_result.reopened_foreign_key_violations.empty() ||
        verification_result.resources.size() != 17) {
        result.error = "verification_not_passed"; return result;
    }
    // Validate each resource: name uniqueness, known name, success
    {
        static const std::set<std::string> kExpected = {
            "nodes","php_versions","profiles","users","sites","domains","databases",
            "backups","reverse_proxies","access_users","access_grants","auth_users",
            "ssl_certificates","mail_domains","mail_mailboxes","mail_aliases","mail_config"
        };
        std::set<std::string> seen;
        for (auto& res : verification_result.resources) {
            if (!kExpected.count(res.resource_type)) { result.error = "verification_not_passed"; return result; }
            if (seen.count(res.resource_type)) { result.error = "verification_not_passed"; return result; }
            if (!res.success) { result.error = "verification_not_passed"; return result; }
            seen.insert(res.resource_type);
        }
    }
    if (verification_result.reopened_resources.size() != 17) {
        result.error = "verification_not_passed"; return result;
    }

    // Validate archive root
    if (archive_root_.empty()) { result.error = "invalid_archive_root"; return result; }
    {
        std::error_code ec;
        auto st = fs::symlink_status(archive_root_, ec);
        if (ec) { result.error = "archive_root_inaccessible"; return result; }
        if (fs::is_symlink(st)) { result.error = "archive_root_symlink_rejected"; return result; }
        if (!fs::exists(st)) {
            fs::create_directories(archive_root_, ec);
            if (ec) { result.error = "archive_root_create_failed"; return result; }
        } else if (!fs::is_directory(st)) {
            result.error = "archive_root_not_directory"; return result;
        }
    }

    migration_timestamp_ = timestamp_utc();

    // Check for existing archive with same migration_id (verified idempotency)
    for (auto& e : fs::directory_iterator(archive_root_)) {
        std::error_code ec;
        if (!e.is_directory(ec)) continue;
        std::string mp = e.path().string() + "/manifest.json";
        if (!fs::exists(mp)) continue;
        // Parse manifest with proper JSON parser
        std::ifstream mf(mp);
        std::string json((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        std::map<std::string, std::string> strings;
        std::map<std::string, int64_t> ints;
        std::map<std::string, bool> bools;
        std::vector<ParsedFileEntry> file_entries;
        ManifestParser parser(json);
        if (!parser.parse_manifest(strings, ints, bools, file_entries)) continue;
        if (strings["migration_id"] == migration_id) {
            // Verify existing archive integrity
            if (verify_archive(e.path().string())) {
                result.error = "migration_id_already_archived";
            } else {
                result.error = "existing_archive_invalid";
            }
            return result;
        }
    }

    std::string archive_name = "legacy-" + source_version + "-" + migration_timestamp_ + "-" + migration_id;
    std::string final_path = archive_root_ + archive_name + "/";
    std::string temp_path = archive_root_ + "." + archive_name + ".tmp/";

    if (fs::exists(final_path)) { result.error = "archive_exists"; return result; }
    if (fs::exists(temp_path)) { result.error = "temporary_archive_exists"; return result; }

    // Create temp directory exclusively before activating guard
    {
        std::error_code ec;
        if (!fs::create_directory(temp_path, ec)) {
            if (ec) { result.error = "temp_dir_failed"; return result; }
            result.error = "temporary_archive_exists"; return result;
        }
    }

    // RAII cleanup — active only after this invocation created the directory
    struct TempGuard {
        std::string path; bool owned;
        TempGuard(const std::string& p) : path(p), owned(true) {}
        ~TempGuard() { if (owned) { std::error_code ec; fs::remove_all(path, ec); } }
        void release() { owned = false; }
    };
    TempGuard temp_guard(temp_path);

    // Build inventory from shared inventory
    std::vector<ArchiveFileEntry> file_entries;
    for (const auto& fi : legacy_file_inventory()) {
        ArchiveFileEntry fe;
        fe.filename = fi.filename;
        fe.optional = !fi.required;
        std::string sp = source_dir_ + fi.filename;
        if (fs::exists(sp) && fs::is_regular_file(sp) && !fs::is_symlink(fs::symlink_status(sp))) {
            fe.present = true;
            fe.size = file_size_or_zero(sp);
            fe.sha256 = sha256_file(sp);
            if (fe.sha256.empty()) { result.error = "sha_failed:" + fi.filename; return result; }
            auto cr = count_records(source_dir_, fi.filename);
            if (!cr.success) { result.error = "parse_failed:" + fi.filename; return result; }
            fe.record_count = cr.record_count;
        } else {
            fe.present = false;
            if (!fe.optional) { result.error = "required_file_missing:" + fi.filename; return result; }
        }
        file_entries.push_back(fe);
    }

    // Disk space check with overflow protection
    {
        uint64_t total = 65536; // metadata overhead
        for (auto& fe : file_entries) if (fe.present) {
            if (total > UINT64_MAX - fe.size) { result.error = "archive_size_overflow"; return result; }
            total += fe.size;
        }
        uint64_t margin = total / 10;
        if (margin < 65536) margin = 65536; // minimum metadata overhead
        if (margin > UINT64_MAX - total) { result.error = "archive_size_overflow"; return result; }
        uint64_t needed = total + margin;
        std::error_code ec;
        auto space = fs::space(archive_root_, ec);
        if (ec) { result.error = "archive_space_check_failed"; return result; }
        if (space.available < needed) {
            result.error = "insufficient_archive_space"; return result;
        }
    }


    // Durable copy — destination verified against inventory SHA
    auto durable_copy = [&](const std::string& src, const std::string& dst, const std::string& fn,
                            const std::string& expected_sha) -> bool {
        // Open source with O_NOFOLLOW to reject symlinks atomically
        int src_fd = open(src.c_str(), O_RDONLY | O_NOFOLLOW);
        if (src_fd < 0) { result.error = "source_open_failed:" + fn; return false; }

        // fstat the opened fd for race-free type/size/inode check
        struct stat st;
        if (fstat(src_fd, &st) != 0) { close(src_fd); result.error = "source_fstat_failed:" + fn; return false; }
        if (!S_ISREG(st.st_mode)) { close(src_fd); result.error = "source_not_regular:" + fn; return false; }

        uint64_t src_size = st.st_size;
        time_t src_mtime = st.st_mtime;
        ino_t src_inode = st.st_ino;

        // Fast-forward through source to verify size (no separate SHA pass)
        uint64_t total_read = 0;
        while (true) {
            char buf[65536];
            ssize_t n = read(src_fd, buf, sizeof(buf));
            if (n < 0) { close(src_fd); result.error = "source_read_failed:" + fn; return false; }
            if (n == 0) break;
            total_read += n;
        }
        if (total_read != src_size) { close(src_fd); result.error = "source_size_changed:" + fn; return false; }

        // Seek back for copy
        if (lseek(src_fd, 0, SEEK_SET) != 0) { close(src_fd); result.error = "source_seek_failed:" + fn; return false; }

        // Exclusive create destination
        int dst_fd = open(dst.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0640);
        if (dst_fd < 0) { close(src_fd); result.error = "dest_create_failed:" + fn; return false; }

        // Stream copy with partial write + EINTR handling
        char cbuf[65536]; bool copy_ok = true;
        while (true) {
            ssize_t n;
            do { n = read(src_fd, cbuf, sizeof(cbuf)); } while (n < 0 && errno == EINTR);
            if (n < 0) { copy_ok = false; break; }
            if (n == 0) break;
            // Write all bytes, handling partial writes and EINTR
            ssize_t written_total = 0;
            while (written_total < n) {
                ssize_t w;
                do { w = write(dst_fd, cbuf + written_total, n - written_total); } while (w < 0 && errno == EINTR);
                if (w < 0) { copy_ok = false; break; }
                written_total += w;
            }
            if (!copy_ok) break;
        }
        close(src_fd);
        if (!copy_ok) { close(dst_fd); unlink(dst.c_str()); result.error = "copy_failed:" + fn; return false; }

        // fsync + close destination
        if (fsync(dst_fd) != 0) { close(dst_fd); unlink(dst.c_str()); result.error = "archive_file_fsync_failed:" + fn; return false; }
        if (close(dst_fd) != 0) { unlink(dst.c_str()); result.error = "close_failed:" + fn; return false; }

        // Verify destination SHA matches inventory snapshot (NOT recomputed source)
        std::string dst_sha = sha256_file(dst);
        if (dst_sha.empty()) { result.error = "dest_sha_failed:" + fn; return false; }
        if (dst_sha != expected_sha) { result.error = "checksum_mismatch:" + fn; return false; }
        // Also verify against freshly read source (detect source mutation during copy)
        { int check_fd = open(src.c_str(), O_RDONLY); if (check_fd < 0) {
            result.error = "source_recheck_failed:" + fn; return false; }
          close(check_fd); }
        std::string src_now = sha256_file(src);
        if (src_now != expected_sha) { result.error = "source_changed_during_archive:" + fn; return false; }

        // Verify source unchanged (stat after fd is closed)
        struct stat st2;
        if (stat(src.c_str(), &st2) != 0) { result.error = "source_recheck_failed:" + fn; return false; }
        if (static_cast<uint64_t>(st2.st_size) != src_size ||
            st2.st_mtime != src_mtime || st2.st_ino != src_inode) {
            result.error = "source_changed_during_archive:" + fn; return false;
        }
        return true;
    };

    for (auto& fe : file_entries) {
        if (!fe.present) continue;
        if (!durable_copy(source_dir_ + fe.filename, temp_path + fe.filename, fe.filename, fe.sha256))
            return result;
    }

    // Build manifest
    {
        ArchiveManifest m;
        m.migration_id = migration_id;
        m.source_version = source_version;
        m.target_version = target_version;
        m.migration_timestamp = migration_timestamp_;
        m.source_directory = source_dir_;
        m.archive_directory = final_path;
        m.files = std::move(file_entries);
        m.checksum_match = true;
        m.initial_integrity_check = verification_result.initial_integrity_check_result;
        m.reopened_integrity_check = verification_result.reopened_integrity_check_result;
        m.initial_fk_violations = static_cast<int>(verification_result.initial_foreign_key_violations.size());
        m.reopened_fk_violations = static_cast<int>(verification_result.reopened_foreign_key_violations.size());
        m.verification_result = "success";
        result.manifest = std::move(m);
    }

    // Write manifest with JSON escaping and fsync
    {
        std::string tmp = temp_path + "manifest.json.tmp";
        std::ofstream mf(tmp); if (!mf) { result.error = "manifest_write_failed"; return result; }
        auto& m = result.manifest;
        #define JKV(k, v) mf << "\"" << k << "\": \"" << json_escape(v) << "\""
        #define JKN(k, v) mf << "\"" << k << "\": " << v
        mf << "{\n";
        JKV("manifest_version", m.manifest_version); mf << ",\n";
        JKV("migration_id", m.migration_id); mf << ",\n";
        JKV("source_version", m.source_version); mf << ",\n";
        JKV("target_version", m.target_version); mf << ",\n";
        JKV("migration_timestamp", m.migration_timestamp); mf << ",\n";
        JKV("source_directory", m.source_directory); mf << ",\n";
        JKV("archive_directory", m.archive_directory); mf << ",\n";
        mf << "\"files\": [\n";
        for (size_t i = 0; i < m.files.size(); ++i) {
            auto& fe = m.files[i];
            mf << "  {\"filename\": \"" << json_escape(fe.filename) << "\",";
            mf << "\"size\": " << fe.size << ",";
            mf << "\"sha256\": \"" << fe.sha256 << "\",";
            mf << "\"record_count\": " << fe.record_count << ",";
            mf << "\"optional\": " << (fe.optional ? "true" : "false") << ",";
            mf << "\"present\": " << (fe.present ? "true" : "false") << "}";
            if (i < m.files.size() - 1) mf << ",";
            mf << "\n";
        }
        mf << "],\n";
        JKN("checksum_match", "true"); mf << ",\n";
        JKV("initial_integrity_check", m.initial_integrity_check); mf << ",\n";
        JKV("reopened_integrity_check", m.reopened_integrity_check); mf << ",\n";
        JKN("initial_fk_violations", m.initial_fk_violations); mf << ",\n";
        JKN("reopened_fk_violations", m.reopened_fk_violations); mf << ",\n";
        JKV("verification_result", m.verification_result); mf << "\n";
        mf << "}\n";
        mf.close();
        if (!mf) { result.error = "manifest_write_failed"; return result; }
        // fsync + rename
        { int fd = open(tmp.c_str(), O_RDONLY); if (fd < 0) { result.error = "manifest_fsync_open_failed"; return result; }
          if (fsync(fd) != 0) { close(fd); result.error = "manifest_fsync_failed"; return result; } close(fd); }
        { std::error_code ec; fs::rename(tmp, temp_path + "manifest.json", ec);
          if (ec) { result.error = "manifest_local_rename_failed"; return result; } }
        // fsync temp dir
        int dd = open(temp_path.c_str(), O_RDONLY);
        if (dd < 0) { result.error = "temp_directory_fsync_open_failed"; return result; }
        if (fsync(dd) != 0) { close(dd); result.error = "temp_directory_fsync_failed"; return result; }
        if (close(dd) != 0) { result.error = "temp_directory_close_failed"; return result; }
    }

    // Write SHA256SUMS with fsync
    {
        std::string tmp = temp_path + "SHA256SUMS.tmp";
        std::ofstream sf(tmp); if (!sf) { result.error = "sha256sums_write_failed"; return result; }
        std::vector<std::string> sums;
        for (auto& fe : result.manifest.files)
            if (fe.present) sums.push_back(fe.sha256 + "  " + fe.filename);
        std::sort(sums.begin(), sums.end());
        for (auto& s : sums) sf << s << "\n";
        sf.close();
        if (!sf) { result.error = "sha256sums_write_failed"; return result; }
        { int fd = open(tmp.c_str(), O_RDONLY); if (fd < 0) { result.error = "checksum_fsync_open_failed"; return result; }
          if (fsync(fd) != 0) { close(fd); result.error = "checksum_file_fsync_failed"; return result; } close(fd); }
        { std::error_code ec; fs::rename(tmp, temp_path + "SHA256SUMS", ec);
          if (ec) { result.error = "checksum_local_rename_failed"; return result; } }
        int dd = open(temp_path.c_str(), O_RDONLY);
        if (dd < 0) { result.error = "temp_directory_fsync_open_failed"; return result; }
        if (fsync(dd) != 0) { close(dd); result.error = "temp_directory_fsync_failed"; return result; }
        if (close(dd) != 0) { result.error = "temp_directory_close_failed"; return result; }
    }

    // Pre-publication verification
    if (!verify_archive(temp_path)) { result.error = "pre_publish_verify_failed"; return result; }
    if (!set_permissions(temp_path)) { result.error = "archive_permissions_failed"; return result; }

    // Atomic rename with fsync
    {
        std::error_code ec;
        fs::rename(temp_path, final_path, ec);
        if (ec) { result.error = "rename_failed"; return result; }
    }

    // After rename, temp_path no longer exists — any failure must quarantine final_path
    temp_guard.release();

    // Archive-root fsync after rename
    {
        int dd = open(archive_root_.c_str(), O_RDONLY);
        if (dd < 0) goto quarantine;
        if (fsync(dd) != 0) { close(dd); goto quarantine; }
        if (close(dd) != 0) goto quarantine;
    }

    // Post-publication verification
    if (verify_archive(final_path)) {
        result.archive_path = final_path;
        result.success = true;
        return result;
    }

quarantine:
    {
        std::string quarantine = archive_root_ + ".failed-" + archive_name + "/";
        std::error_code ec;
        if (fs::exists(quarantine, ec)) { result.error = "post_publish_verify_failed"; return result; }
        fs::rename(final_path, quarantine, ec);
        if (ec) { result.error = "post_publish_verify_failed"; return result; }
        if (fs::exists(final_path, ec)) { result.error = "post_publish_verify_failed"; return result; }
        int qd = open(archive_root_.c_str(), O_RDONLY);
        if (qd < 0) { result.error = "archive_root_fsync_open_failed"; return result; }
        if (fsync(qd) != 0) { close(qd); result.error = "archive_root_fsync_failed"; return result; }
        if (close(qd) != 0) { result.error = "archive_root_close_failed"; return result; }
        result.error = "post_publish_verify_failed"; return result;
    }
    } catch (const std::exception&) { result.error = "filesystem_exception"; return result; }
      catch (...) { result.error = "filesystem_exception"; return result; }
}

// ============================================================
// Proper JSON parser for manifest validation
// ============================================================


bool LegacyArchive::verify_archive(const std::string& archive_path,
                                   ArchiveManifest* verified_manifest)
{
    std::string ap = archive_path;
    if (ap.empty()) return false;
    if (ap.back() != '/') ap += '/';

    // Root safety
    { std::error_code ec; auto st = fs::symlink_status(ap, ec); if (ec) return false;
      if (fs::is_symlink(st)) return false;
      if (!fs::exists(st) || !fs::is_directory(st)) return false; }

    // Parse SHA256SUMS
    std::string sums_path = ap + "SHA256SUMS";
    if (!fs::exists(sums_path) || !fs::is_regular_file(sums_path) ||
        fs::is_symlink(fs::symlink_status(sums_path))) return false;

    std::map<std::string, std::string> sums_entries;
    {
        std::ifstream sf(sums_path); std::string line; bool any_present = false;
        for (auto& fi : legacy_file_inventory()) {
            std::string sp = ap + fi.filename;
            if (fs::exists(sp)) any_present = true;
        }
        while (std::getline(sf, line)) {
            if (line.empty()) continue;
            if (line.size() < 67 || line[64] != ' ' || line[65] != ' ') return false;
            std::string hash(64, ' '); bool ok = true;
            for (int i = 0; i < 64; ++i) {
                if (!isxdigit(line[i])) { ok = false; break; }
                if (isupper(line[i])) { ok = false; break; }
                hash[i] = line[i];
            }
            if (!ok) return false;
            std::string fn = line.substr(66);
            if (fn.empty() || fn.find('/') != std::string::npos || fn.find('\\') != std::string::npos ||
                fn == "." || fn == "..") return false;
            bool known = false;
            for (auto& fi : legacy_file_inventory()) { if (fi.filename == fn) { known = true; break; } }
            if (!known) return false;
            if (sums_entries.count(fn)) return false;
            sums_entries[fn] = hash;
        }
        if (any_present && sums_entries.empty()) return false;
    }

    // Verify SHA256SUMS entries against disk
    for (auto& [fn, expected_hash] : sums_entries) {
        if (!fs::exists(ap + fn) || !fs::is_regular_file(ap + fn)) return false;
        if (fs::is_symlink(fs::symlink_status(ap + fn))) return false;
        if (sha256_file(ap + fn) != expected_hash) return false;
    }

    // Parse manifest with proper JSON parser
    std::string mp = ap + "manifest.json";
    if (!fs::exists(mp) || !fs::is_regular_file(mp) || fs::is_symlink(fs::symlink_status(mp))) return false;
    std::string json;
    { std::ifstream mf(mp); json.assign(std::istreambuf_iterator<char>(mf), std::istreambuf_iterator<char>()); }

    std::map<std::string, std::string> strings;
    std::map<std::string, int64_t> ints;
    std::map<std::string, bool> bools;
    std::vector<ParsedFileEntry> file_entries;

    ManifestParser parser(json);
    if (!parser.parse_manifest(strings, ints, bools, file_entries)) return false;

    // Validate required string fields
    if (strings["manifest_version"] != "1.0") return false;
    std::string mid = strings["migration_id"];
    if (!valid_migration_id(mid)) return false;
    if (!safe_version(strings["source_version"])) return false;
    if (!safe_version(strings["target_version"])) return false;
    if (strings["verification_result"] != "success") return false;
    if (strings["initial_integrity_check"] != "ok") return false;
    if (strings["reopened_integrity_check"] != "ok") return false;

    // Validate boolean
    if (!bools.count("checksum_match") || !bools["checksum_match"]) return false;

    // Validate integers
    if (!ints.count("initial_fk_violations") || ints["initial_fk_violations"] != 0) return false;
    if (!ints.count("reopened_fk_violations") || ints["reopened_fk_violations"] != 0) return false;

    // Cross-check file entries using typed ParsedFileEntry fields
    if (file_entries.size() != 19) return false;
    std::set<std::string> manifest_filenames;
    for (auto& pfe : file_entries) {
        if (pfe.filename.empty() || !pfe.valid) return false;
        if (manifest_filenames.count(pfe.filename)) return false;
        manifest_filenames.insert(pfe.filename);

        bool optional = false;
        for (auto& fi : legacy_file_inventory()) { if (fi.filename == pfe.filename) { optional = !fi.required; break; } }
        if (pfe.optional != optional) return false;

        std::string disk_path = ap + pfe.filename;
        bool disk_exists = fs::exists(disk_path) && fs::is_regular_file(disk_path) &&
                          !fs::is_symlink(fs::symlink_status(disk_path));

        if (pfe.present) {
            if (!disk_exists) return false;
            if (!sums_entries.count(pfe.filename)) return false;
            if (pfe.sha256 != sums_entries[pfe.filename]) return false;
            std::string disk_sha = sha256_file(disk_path);
            if (disk_sha.empty() || pfe.sha256 != disk_sha) return false;
            uint64_t disk_size = 0;
            { std::error_code ec; disk_size = fs::file_size(disk_path, ec); if (ec) return false; }
            if (pfe.size != disk_size) return false;
        } else {
            if (disk_exists) return false;
            if (sums_entries.count(pfe.filename)) return false;
            if (!optional) return false;
            if (!pfe.sha256.empty()) return false;
            if (pfe.size != 0 || pfe.record_count != 0) return false;
        }
    }
    // Verify all required files present in manifest
    for (auto& fi : legacy_file_inventory()) {
        if (fi.required && !manifest_filenames.count(fi.filename)) return false;
    }

    // Exact directory content verification
    for (auto& e : fs::directory_iterator(ap)) {
        std::string fn = e.path().filename().string();
        auto st = fs::symlink_status(e.path());
        if (fs::is_symlink(st)) return false;
        switch (st.type()) {
        case fs::file_type::regular: break;
        case fs::file_type::directory: return false;
        default: return false; // socket, FIFO, device, etc.
        }
        if (fn == "manifest.json" || fn == "SHA256SUMS") continue;
        bool known = false;
        for (auto& fi : legacy_file_inventory()) { if (fi.filename == fn) { known = true; break; } }
        if (!known) return false;
    }

    if (verified_manifest) {
        ArchiveManifest m;
        m.manifest_version = strings["manifest_version"];
        m.migration_id = mid;
        m.source_version = strings["source_version"];
        m.target_version = strings["target_version"];
        m.migration_timestamp = strings["migration_timestamp"];
        m.source_directory = strings["source_directory"];
        m.archive_directory = ap;
        m.checksum_match = bools["checksum_match"];
        m.initial_integrity_check = strings["initial_integrity_check"];
        m.reopened_integrity_check = strings["reopened_integrity_check"];
        m.initial_fk_violations = static_cast<int>(ints["initial_fk_violations"]);
        m.reopened_fk_violations = static_cast<int>(ints["reopened_fk_violations"]);
        m.verification_result = strings["verification_result"];
        // Rebuild file entries from parsed typed data
        for (auto& pfe : file_entries) {
            ArchiveFileEntry afe;
            afe.filename = pfe.filename;
            afe.present = pfe.present;
            afe.optional = pfe.optional;
            if (pfe.present) {
                afe.sha256 = pfe.sha256;
                afe.size = pfe.size;
                afe.record_count = pfe.record_count;
            }
            m.files.push_back(std::move(afe));
        }
        *verified_manifest = std::move(m);
    }
    return true;
}

// ============================================================
// Permissions with read-back verification
// ============================================================

bool LegacyArchive::set_permissions(const std::string& archive_path) {
    std::error_code ec;
    fs::permissions(archive_path, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) return false;

    // Read back and verify
    auto st = fs::status(archive_path, ec);
    if (ec) return false;
    if ((st.permissions() & fs::perms::mask) != fs::perms::owner_all) return false;

    for (auto& e : fs::directory_iterator(archive_path)) {
        auto p = e.path();
        fs::permissions(p, fs::perms::owner_read | fs::perms::group_read, fs::perm_options::replace, ec);
        if (ec) return false;
        // Read back
        auto fs_st = fs::status(p, ec);
        if (ec) return false;
        auto expected = fs::perms::owner_read | fs::perms::group_read;
        if ((fs_st.permissions() & fs::perms::mask) != expected) return false;
    }
    return true;
}

} // namespace containercp::storage
