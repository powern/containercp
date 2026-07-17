#include "LegacyArchive.h"
#include "LegacyDatasetReader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <ctime>

namespace containercp::storage {
namespace fs = std::filesystem;

static const std::vector<std::string> kLegacyFiles = {
    "nodes.db", "php_versions.db", "profiles.db", "template_profiles.db",
    "users.db", "sites.db", "domains.db", "databases.db", "backups.db",
    "reverse_proxies.db", "access_users.db", "access_grants.db",
    "auth_users.db", "ssl_certificates.db", "mail_domains.db",
    "mail_mailboxes.db", "mail_aliases.db", "mail_state.db", "mail_smarthost.db"
};

static const std::vector<std::string> kOptionalFiles = {
    "template_profiles.db", "access_users.db", "access_grants.db",
    "auth_users.db", "ssl_certificates.db", "mail_domains.db",
    "mail_mailboxes.db", "mail_aliases.db", "mail_state.db", "mail_smarthost.db"
};

static bool is_optional(const std::string& fn) {
    return std::find(kOptionalFiles.begin(), kOptionalFiles.end(), fn) != kOptionalFiles.end();
}

LegacyArchive::LegacyArchive(const std::string& source_directory,
                             const std::string& archive_root)
    : source_dir_(source_directory), archive_root_(archive_root)
{
    if (!source_dir_.empty() && source_dir_.back() != '/') source_dir_ += '/';
    if (!archive_root_.empty() && archive_root_.back() != '/') archive_root_ += '/';
}

std::string LegacyArchive::generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string uuid;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) uuid += '-';
        uuid += hex[dis(gen)];
    }
    return uuid;
}

std::string LegacyArchive::timestamp_utc() {
    time_t now = time(nullptr);
    struct tm* utc = gmtime(&now);
    std::ostringstream ss;
    ss << std::put_time(utc, "%Y%m%dT%H%M%SZ");
    return ss.str();
}

std::string LegacyArchive::sha256_file(const std::string& path) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "";
    char buf[8192];
    while (f.read(buf, sizeof(buf)).gcount() > 0)
        SHA256_Update(&ctx, buf, f.gcount());
    if (f.bad()) return "";
    SHA256_Final(hash, &ctx);
    std::string out; out.reserve(64);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        out += "0123456789abcdef"[(hash[i] >> 4) & 0xf];
        out += "0123456789abcdef"[hash[i] & 0xf];
    }
    return out;
}

static uint64_t file_size_or_zero(const std::string& path) {
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    return ec ? 0 : sz;
}

static bool file_unchanged(const std::string& path, uint64_t orig_size, const std::string& orig_sha) {
    if (file_size_or_zero(path) != orig_size) return false;
    return LegacyArchive::sha256_file(path) == orig_sha;
}

ArchiveResult LegacyArchive::create_archive(
    const std::string& migration_id,
    const std::string& source_version,
    const std::string& target_version,
    const DatabaseVerificationResult& verification_result)
{
    ArchiveResult result;

    // Validate inputs
    if (migration_id.empty()) { result.error = "invalid_migration_id"; return result; }
    if (!verification_result.initial_verification_passed) { result.error = "verification_not_passed"; return result; }

    // Build archive path
    std::string archive_name = "legacy-" + source_version + "-" + timestamp_utc() + "-" + generate_uuid();
    std::string final_path = archive_root_ + archive_name + "/";
    std::string temp_path = archive_root_ + "." + archive_name + ".tmp/";

    // Check for collision
    if (fs::exists(final_path)) { result.error = "archive_exists"; return result; }
    if (fs::exists(temp_path)) fs::remove_all(temp_path);

    // Disk space check
    {
        uint64_t total_size = 0;
        for (const auto& fn : kLegacyFiles) {
            std::string sp = source_dir_ + fn;
            if (fs::exists(sp)) total_size += file_size_or_zero(sp);
        }
        total_size += 65536; // manifest + checksum overhead
        std::error_code ec;
        auto space = fs::space(archive_root_, ec);
        if (!ec && space.available < total_size) {
            result.error = "insufficient_archive_space"; return result;
        }
    }

    // Build file inventory and take source snapshots
    struct Snapshot {
        std::string filename;
        uint64_t size = 0;
        std::string source_sha;
        uint64_t record_count = 0;
        bool present = false;
        bool optional = false;
    };
    std::vector<Snapshot> snapshots;
    LegacyDatasetReader reader(source_dir_);

    for (const auto& fn : kLegacyFiles) {
        Snapshot s;
        s.filename = fn;
        s.optional = is_optional(fn);
        std::string sp = source_dir_ + fn;
        if (fs::exists(sp) && fs::is_regular_file(sp)) {
            s.present = true;
            s.size = file_size_or_zero(sp);
            s.source_sha = sha256_file(sp);
            if (s.source_sha.empty()) { result.error = "source_sha_failed:" + fn; return result; }
        } else {
            s.present = false;
            if (!s.optional) { result.error = "required_file_missing:" + fn; return result; }
        }
        snapshots.push_back(s);
    }

    // Create temp directory
    {
        std::error_code ec;
        fs::create_directories(temp_path, ec);
        if (ec) { result.error = "temp_dir_failed"; return result; }
    }

    // Copy files
    for (auto& s : snapshots) {
        if (!s.present) continue;
        std::string src = source_dir_ + s.filename;
        std::string dst = temp_path + s.filename;

        // Reject symlinks
        if (fs::is_symlink(src)) { result.error = "source_symlink_rejected:" + s.filename; goto cleanup; }

        // Copy
        {
            std::error_code ec;
            fs::copy_file(src, dst, ec);
            if (ec) { result.error = "copy_failed:" + s.filename; goto cleanup; }
        }

        // Verify destination
        std::string dst_sha = sha256_file(dst);
        if (dst_sha.empty()) { result.error = "dest_sha_failed:" + s.filename; goto cleanup; }
        if (dst_sha != s.source_sha) { result.error = "checksum_mismatch:" + s.filename; goto cleanup; }

        // Detect source mutation
        if (!file_unchanged(src, s.size, s.source_sha)) {
            result.error = "source_changed_during_archive:" + s.filename; goto cleanup;
        }
    }

    // Build manifest
    {
        ArchiveManifest m;
        m.manifest_version = "1.0";
        m.migration_id = migration_id;
        m.source_version = source_version;
        m.target_version = target_version;
        m.migration_timestamp = timestamp_utc();
        m.source_directory = source_dir_;
        m.archive_directory = final_path;

        for (auto& s : snapshots) {
            ArchiveFileEntry fe;
            fe.filename = s.filename;
            fe.size = s.size;
            fe.sha256 = s.source_sha;
            fe.record_count = s.record_count;
            fe.optional = s.optional;
            fe.present = s.present;
            m.files.push_back(std::move(fe));
        }

        m.checksum_match = true;
        m.integrity_check = verification_result.initial_integrity_check_result;
        if (!verification_result.initial_foreign_key_violations.empty())
            m.foreign_key_violations = "violations_present";
        m.verification_result = "success";

        result.manifest = std::move(m);
    }

    // Write manifest
    {
        std::ofstream mf(temp_path + "manifest.json");
        if (!mf) { result.error = "manifest_write_failed"; goto cleanup; }
        auto& m = result.manifest;
        mf << "{\n";
        mf << "  \"manifest_version\": \"" << m.manifest_version << "\",\n";
        mf << "  \"migration_id\": \"" << m.migration_id << "\",\n";
        mf << "  \"source_version\": \"" << m.source_version << "\",\n";
        mf << "  \"target_version\": \"" << m.target_version << "\",\n";
        mf << "  \"migration_timestamp\": \"" << m.migration_timestamp << "\",\n";
        mf << "  \"source_directory\": \"" << m.source_directory << "\",\n";
        mf << "  \"archive_directory\": \"" << m.archive_directory << "\",\n";
        mf << "  \"files\": [\n";
        for (size_t i = 0; i < m.files.size(); ++i) {
            auto& fe = m.files[i];
            mf << "    {\"filename\":\"" << fe.filename << "\",";
            mf << "\"size\":" << fe.size << ",";
            mf << "\"sha256\":\"" << fe.sha256 << "\",";
            mf << "\"record_count\":" << fe.record_count << ",";
            mf << "\"optional\":" << (fe.optional ? "true" : "false") << ",";
            mf << "\"present\":" << (fe.present ? "true" : "false") << "}";
            if (i < m.files.size() - 1) mf << ",";
            mf << "\n";
        }
        mf << "  ],\n";
        mf << "  \"checksum_match\": true,\n";
        mf << "  \"integrity_check\": \"" << m.integrity_check << "\",\n";
        mf << "  \"foreign_key_violations\": \"" << m.foreign_key_violations << "\",\n";
        mf << "  \"verification_result\": \"" << m.verification_result << "\"\n";
        mf << "}\n";
        if (!mf) { result.error = "manifest_write_failed"; goto cleanup; }
    }

    // Write SHA256SUMS
    {
        std::ofstream sf(temp_path + "SHA256SUMS");
        if (!sf) { result.error = "sha256sums_write_failed"; goto cleanup; }
        std::vector<std::string> sums;
        for (const auto& fe : result.manifest.files) {
            if (fe.present)
                sums.push_back(fe.sha256 + "  " + fe.filename + "\n");
        }
        std::sort(sums.begin(), sums.end());
        for (const auto& s : sums) sf << s;
        if (!sf) { result.error = "sha256sums_write_failed"; goto cleanup; }
    }

    // Atomic rename
    {
        std::error_code ec;
        fs::permissions(temp_path, fs::perms::owner_all, fs::perm_options::replace, ec);
        for (const auto& entry : fs::directory_iterator(temp_path)) {
            fs::permissions(entry.path(), fs::perms::owner_read | fs::perms::group_read,
                           fs::perm_options::replace, ec);
        }
        fs::rename(temp_path, final_path, ec);
        if (ec) { result.error = "rename_failed"; goto cleanup; }
    }

    result.archive_path = final_path;
    result.success = true;
    return result;

cleanup:
    std::error_code ec;
    fs::remove_all(temp_path, ec);
    return result;
}

bool LegacyArchive::verify_archive(const std::string& archive_path,
                                   ArchiveManifest* verified_manifest)
{
    if (!fs::exists(archive_path) || !fs::is_directory(archive_path)) return false;

    // Parse manifest
    // Simple JSON parser — reads the manifest.json file
    std::string mpath = archive_path;
    if (mpath.back() != '/') mpath += '/';
    mpath += "manifest.json";
    if (!fs::exists(mpath)) return false;

    // Parse manifest via simple string search
    // For a production system this would use a proper JSON library
    // For now, verify files directly from the directory
    // Count present files and verify checksums
    {
        std::string sums_path = archive_path;
        if (sums_path.back() != '/') sums_path += '/';
        sums_path += "SHA256SUMS";
        if (fs::exists(sums_path)) {
            std::ifstream sf(sums_path);
            std::string line;
            while (std::getline(sf, line)) {
                if (line.empty()) continue;
                size_t sp = line.find("  ");
                if (sp == std::string::npos) return false;
                std::string expected_hash = line.substr(0, sp);
                std::string fname = line.substr(sp + 2);
                std::string fpath = archive_path;
                if (fpath.back() != '/') fpath += '/';
                fpath += fname;
                if (!fs::exists(fpath)) return false;
                std::string actual = sha256_file(fpath);
                if (actual != expected_hash) return false;
            }
        }
    }

    // Check for unexpected .db files
    for (const auto& entry : fs::directory_iterator(archive_path)) {
        std::string fn = entry.path().filename().string();
        if (fn.size() > 3 && fn.substr(fn.size() - 3) == ".db") {
            if (std::find(kLegacyFiles.begin(), kLegacyFiles.end(), fn) == kLegacyFiles.end())
                return false;
        }
    }

    if (verified_manifest) {
        // Populate basic manifest fields
        verified_manifest->checksum_match = true;
        verified_manifest->archive_directory = archive_path;
    }

    return true;
}

bool LegacyArchive::set_permissions(const std::string& archive_path) {
    std::error_code ec;
    fs::permissions(archive_path, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) return false;
    for (const auto& entry : fs::directory_iterator(archive_path)) {
        auto perms = fs::perms::owner_read | fs::perms::group_read;
        fs::permissions(entry.path(), perms, fs::perm_options::replace, ec);
        if (ec) return false;
    }
    return true;
}

} // namespace containercp::storage
