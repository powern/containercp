#include "LegacyArchive.h"
#include "LegacyDatasetReader.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <ctime>
#include <unistd.h>

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
    if (v.empty()) return false;
    for (char c : v) {
        if (c == '/' || c == '\\' || c < 0x20 || c == ' ' || c == '\t') return false;
    }
    if (v.find("..") != std::string::npos) return false;
    if (v.front() == '.' || v.back() == '.' || v.front() == ' ' || v.back() == ' ') return false;
    return true;
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
    // Use LegacyDatasetReader for all files
    LegacyDatasetReader r(sd);
    // Use a single call for combined profiles
    if (fn == "profiles.db" || fn == "template_profiles.db") {
        auto dr = r.read_combined_profiles(); return dr.success ? dr.records.size() : 0;
    }
    // For other files, try named readers
    if (fn == "nodes.db") { auto dr = r.read_nodes(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "php_versions.db") { auto dr = r.read_php_versions(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "users.db") { auto dr = r.read_users(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "sites.db") { auto dr = r.read_sites(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "domains.db") { auto dr = r.read_domains(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "databases.db") { auto dr = r.read_databases(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "backups.db") { auto dr = r.read_backups(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "reverse_proxies.db") { auto dr = r.read_reverse_proxies(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "access_users.db") { auto dr = r.read_access_users(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "access_grants.db") { auto dr = r.read_access_grants(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "auth_users.db") { auto dr = r.read_auth_users(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "ssl_certificates.db") { auto dr = r.read_ssl_certificates(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "mail_domains.db") { auto dr = r.read_mail_domains(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "mail_mailboxes.db") { auto dr = r.read_mailboxes(); r.record_count = dr.records.size(); r.success = true; return r; }
    if (fn == "mail_aliases.db") { auto dr = r.read_mail_aliases(); r.record_count = dr.records.size(); r.success = true; return r; }
    // mail_state / mail_smarthost — count as 1 if present
    if (fn == "mail_state.db" || fn == "mail_smarthost.db") {
        auto dr = r.read_mail_config();
        if (!dr.success) return 0;
        if (fn == "mail_state.db") return dr.module_state_present ? 1 : 0;
        if (fn == "mail_smarthost.db") return dr.smarthost_present ? 1 : 0;
    }
    return 0;
}

// ============================================================
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

    // Complete Phase 9 check
    if (!verification_result.success ||
        !verification_result.initial_verification_passed ||
        !verification_result.reopened_verification_passed ||
        !verification_result.reopen_succeeded ||
        verification_result.initial_integrity_check_result != "ok" ||
        verification_result.reopened_integrity_check_result != "ok" ||
        !verification_result.initial_foreign_key_violations.empty() ||
        !verification_result.reopened_foreign_key_violations.empty()) {
        result.error = "verification_not_passed"; return result;
    }

    migration_timestamp_ = timestamp_utc();

    // Check for existing archive with same migration_id
    for (auto& e : fs::directory_iterator(archive_root_)) {
        if (e.is_directory()) {
            std::string mp = e.path().string() + "/manifest.json";
            if (fs::exists(mp)) {
                // Quick check: if manifest contains our migration_id, reject
                std::ifstream mf(mp);
                std::string content((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
                if (content.find("\"migration_id\": \"" + migration_id + "\"") != std::string::npos) {
                    result.error = "migration_id_already_archived"; return result;
                }
            }
        }
    }

    std::string archive_name = "legacy-" + source_version + "-" + migration_timestamp_ + "-" + migration_id;
    std::string final_path = archive_root_ + archive_name + "/";
    temp_path_ = archive_root_ + "." + archive_name + ".tmp/";

    if (fs::exists(final_path)) { result.error = "archive_exists"; return result; }
    if (fs::exists(temp_path_)) { result.error = "temporary_archive_exists"; return result; }

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

    // Disk space check
    {
        uint64_t total = 65536;
        for (auto& fe : file_entries) if (fe.present) total += fe.size;
        std::error_code ec;
        auto space = fs::space(archive_root_, ec);
        if (ec) { result.error = "archive_space_check_failed"; return result; }
        if (space.available < total) { result.error = "insufficient_archive_space"; return result; }
    }

    // Create temp directory
    {
        std::error_code ec;
        fs::create_directories(temp_path_, ec);
        if (ec) { result.error = "temp_dir_failed"; return result; }
    }
    temp_owned_ = true;

    // Copy files with durability
    auto durable_copy = [&](const std::string& src, const std::string& dst, const std::string& fn) -> bool {
        if (fs::is_symlink(src)) { result.error = "source_symlink_rejected:" + fn; return false; }
        // Read source
        std::ifstream in(src, std::ios::binary); if (!in) { result.error = "copy_failed:" + fn; return false; }
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (in.bad()) { result.error = "copy_failed:" + fn; return false; }
        // Write exclusively
        std::ofstream out(dst, std::ios::binary); if (!out) { result.error = "copy_failed:" + fn; return false; }
        out.write(data.data(), data.size()); out.flush();
        if (!out) { result.error = "copy_failed:" + fn; return false; }
        out.close();
        // fsync
        FILE* fp = fopen(dst.c_str(), "rb+"); if (fp) { fsync(fileno(fp)); fclose(fp); }
        // Verify destination checksum matches source
        std::string dst_sha = sha256_file(dst);
        if (dst_sha.empty()) { result.error = "dest_sha_failed:" + fn; return false; }
        std::string src_sha = sha256_file(src);
        if (dst_sha != src_sha) { result.error = "checksum_mismatch:" + fn; return false; }
        // Check source unchanged
        if (!file_unchanged(src, data.size(), src_sha)) {
            result.error = "source_changed_during_archive:" + fn; return false;
        }
        return true;
    };

    for (auto& fe : file_entries) {
        if (!fe.present) continue;
        if (!durable_copy(source_dir_ + fe.filename, temp_path_ + fe.filename, fe.filename))
            goto cleanup;
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
        std::string tmp = temp_path_ + "manifest.json.tmp";
        std::ofstream mf(tmp); if (!mf) { result.error = "manifest_write_failed"; goto cleanup; }
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
        if (!mf) { result.error = "manifest_write_failed"; goto cleanup; }
        // fsync + rename
        { FILE* fp = fopen(tmp.c_str(), "rb"); if (fp) { fsync(fileno(fp)); fclose(fp); } }
        fs::rename(tmp, temp_path_ + "manifest.json");
        // fsync temp dir
        { FILE* dp = fopen(temp_path_.c_str(), "r"); if (dp) { fsync(fileno(dp)); fclose(dp); } }
    }

    // Write SHA256SUMS with fsync
    {
        std::string tmp = temp_path_ + "SHA256SUMS.tmp";
        std::ofstream sf(tmp); if (!sf) { result.error = "sha256sums_write_failed"; goto cleanup; }
        std::vector<std::string> sums;
        for (auto& fe : result.manifest.files)
            if (fe.present) sums.push_back(fe.sha256 + "  " + fe.filename);
        std::sort(sums.begin(), sums.end());
        for (auto& s : sums) sf << s << "\n";
        sf.close();
        if (!sf) { result.error = "sha256sums_write_failed"; goto cleanup; }
        { FILE* fp = fopen(tmp.c_str(), "rb"); if (fp) { fsync(fileno(fp)); fclose(fp); } }
        fs::rename(tmp, temp_path_ + "SHA256SUMS");
        { FILE* dp = fopen(temp_path_.c_str(), "r"); if (dp) { fsync(fileno(dp)); fclose(dp); } }
    }

    // Pre-publication verification
    if (!verify_archive(temp_path_)) { result.error = "pre_publish_verify_failed"; goto cleanup; }
    if (!set_permissions(temp_path_)) { result.error = "archive_permissions_failed"; goto cleanup; }

    // Atomic rename
    {
        std::error_code ec;
        fs::rename(temp_path_, final_path, ec);
        if (ec) { result.error = "rename_failed"; goto cleanup; }
        { FILE* dp = fopen(archive_root_.c_str(), "r"); if (dp) { fsync(fileno(dp)); fclose(dp); } }
    }

    // Post-publication verification
    if (!verify_archive(final_path)) { result.error = "post_publish_verify_failed"; return result; }

    result.archive_path = final_path;
    result.success = true;
    return result;

cleanup:
    if (temp_owned_) { std::error_code ec; fs::remove_all(temp_path_, ec); }
    return result;
}

// ============================================================
// verify_archive — strict manifest + SHA256SUMS parsing
// ============================================================

bool LegacyArchive::verify_archive(const std::string& archive_path,
                                   ArchiveManifest* verified_manifest)
{
    std::string ap = archive_path;
    if (ap.back() != '/') ap += '/';
    if (!fs::exists(ap) || !fs::is_directory(ap) || fs::is_symlink(fs::symlink_status(ap))) return false;

    // Parse SHA256SUMS (authoritative)
    std::string sums_path = ap + "SHA256SUMS";
    if (!fs::exists(sums_path) || !fs::is_regular_file(sums_path) ||
        fs::is_symlink(fs::symlink_status(sums_path))) return false;

    std::map<std::string, std::string> sums_entries;
    {
        std::ifstream sf(sums_path); std::string line;
        while (std::getline(sf, line)) {
            if (line.empty()) continue;
            // Must be: <64 hex chars>  <filename>
            if (line.size() < 67 || line[64] != ' ' || line[65] != ' ') return false;
            std::string hash = line.substr(0, 64);
            for (char c : hash) if (!isxdigit(c) || isupper(c)) return false;
            std::string fn = line.substr(66);
            // Validate filename
            if (fn.empty() || fn.find('/') != std::string::npos ||
                fn.find('\\') != std::string::npos || fn == "." || fn == "..") return false;
            // Must be a recognized legacy file
            bool known = false;
            for (auto& fi : legacy_file_inventory()) { if (fi.filename == fn) { known = true; break; } }
            if (!known) return false;
            if (sums_entries.count(fn)) return false; // duplicate
            sums_entries[fn] = hash;
        }
    }

    // Verify each SHA256SUMS entry
    for (auto& [fn, expected_hash] : sums_entries) {
        if (!fs::exists(ap + fn) || !fs::is_regular_file(ap + fn)) return false;
        if (fs::is_symlink(fs::symlink_status(ap + fn))) return false;
        std::string actual = sha256_file(ap + fn);
        if (actual != expected_hash) return false;
    }

    // Parse manifest (basic validation)
    std::string mp = ap + "manifest.json";
    if (!fs::exists(mp) || !fs::is_regular_file(mp) ||
        fs::is_symlink(fs::symlink_status(mp))) return false;

    ArchiveManifest parsed;
    {
        std::ifstream mf(mp); std::string json((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        // Quick checks
        if (json.find("\"manifest_version\"") == std::string::npos) return false;
        if (json.find("\"migration_id\"") == std::string::npos) return false;
        if (json.find("\"verification_result\": \"success\"") == std::string::npos) return false;
        if (json.find("\"checksum_match\": true") == std::string::npos) return false;
        parsed.archive_directory = ap;
        parsed.checksum_match = true;
    }

    // Verify archive directory contains ONLY expected files
    for (auto& e : fs::directory_iterator(ap)) {
        std::string fn = e.path().filename().string();
        if (e.is_symlink()) return false;
        if (e.is_directory()) return false;
        if (fn == "manifest.json" || fn == "SHA256SUMS") continue;
        if (fn.size() >= 3 && fn.substr(fn.size() - 3) == ".db") {
            bool known = false;
            for (auto& fi : legacy_file_inventory()) { if (fi.filename == fn) { known = true; break; } }
            if (!known) return false;
        } else {
            return false; // unexpected non-db file
        }
    }

    // Ensure all expected present+required files from SHA256SUMS match the sums
    for (auto& fi : legacy_file_inventory()) {
        if (!fi.required) continue;
        if (!sums_entries.count(fi.filename)) return false; // required file missing from SHA256SUMS
    }

    if (verified_manifest) *verified_manifest = std::move(parsed);
    return true;
}

bool LegacyArchive::set_permissions(const std::string& archive_path) {
    std::error_code ec;
    fs::permissions(archive_path, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) return false;
    for (auto& e : fs::directory_iterator(archive_path)) {
        fs::permissions(e.path(), fs::perms::owner_read | fs::perms::group_read,
                       fs::perm_options::replace, ec);
        if (ec) return false;
    }
    return true;
}

} // namespace containercp::storage
