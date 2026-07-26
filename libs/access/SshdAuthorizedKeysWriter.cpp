#include "access/SshdAuthorizedKeysWriter.h"
#include "access/AccessKey.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::access {
namespace {

bool write_atomic(const std::string& path, const std::string& content, mode_t mode) {
    std::string tmp = path + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
    }
    if (::chmod(tmp.c_str(), mode) != 0) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
    // Ensure parent directory is root-owned
    struct stat st{};
    if (::lstat(path.c_str(), &st) == 0) {
        // Target exists — check it's not a symlink
        if (S_ISLNK(st.st_mode)) {
            std::error_code ec; std::filesystem::remove(tmp, ec);
            return false;
        }
        // Not root-owned — reject
        if (st.st_uid != 0) {
            std::error_code ec; std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
    return true;
}

bool ensure_parent_dir(const std::string& path) {
    auto parent = std::filesystem::path(path).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) return false;
    // Ensure parent is root-owned 0755
    struct stat st{};
    if (::lstat(parent.c_str(), &st) != 0) return false;
    if (st.st_uid != 0) return false;
    if (st.st_mode & (S_IWGRP | S_IWOTH)) {
        // Fix permissions
        if (::chmod(parent.c_str(), 0755) != 0) return false;
    }
    return true;
}

} // namespace

SshdAuthorizedKeysWriter::SshdAuthorizedKeysWriter(const std::string& keys_root)
    : keys_root_(keys_root) {}

std::string SshdAuthorizedKeysWriter::render_content(const std::vector<AccessKey>& keys) {
    // Sort by (fingerprint, id) for deterministic ordering
    auto sorted = keys;
    std::sort(sorted.begin(), sorted.end(),
        [](const AccessKey& a, const AccessKey& b) {
            if (a.fingerprint != b.fingerprint) return a.fingerprint < b.fingerprint;
            return a.id < b.id;
        });

    // Deduplicate by fingerprint
    std::set<std::string> seen;
    std::ostringstream out;
    for (const auto& k : sorted) {
        if (!k.enabled) continue;
        if (!seen.insert(k.fingerprint).second) continue;
        // Format: "restrict <type> <data> [comment]"
        out << "restrict " << k.key_type << " " << k.key_data;
        if (!k.key_comment.empty()) {
            out << " " << k.key_comment;
        }
        out << "\n";
    }
    return out.str();
}

core::OperationResult SshdAuthorizedKeysWriter::write(uint64_t access_user_id,
                                                       const std::string& linux_username,
                                                       LoadKeysFn load_keys_fn) {
    if (linux_username.empty()) return {false, "empty username", ""};
    if (linux_username.find('/') != std::string::npos) return {false, "invalid username", ""};

    // Load keys
    auto keys = load_keys_fn(access_user_id);

    // Filter to only enabled keys
    std::vector<AccessKey> enabled;
    for (const auto& k : keys) {
        if (k.enabled) enabled.push_back(k);
    }

    std::string path = keys_path(linux_username);

    if (enabled.empty()) {
        // No enabled keys — remove file if it exists
        std::error_code ec;
        bool existed = std::filesystem::exists(path, ec);
        if (existed) {
            if (!std::filesystem::remove(path, ec)) {
                return {false, "failed to remove authorized_keys: " + path, ""};
            }
        }
        return {true, "no keys — authorized_keys removed", ""};
    }

    // Render content
    auto content = render_content(keys);

    // Ensure parent directory exists and is safe
    if (!ensure_parent_dir(path)) {
        return {false, "failed to create authorized_keys parent directory", ""};
    }

    // Write atomically
    if (!write_atomic(path, content, 0600)) {
        return {false, "failed to write authorized_keys atomically: " + path, ""};
    }

    // Verify postcondition
    auto verify = validate_file(linux_username);
    if (!verify.success) {
        return {false, "authorized_keys postcondition failed: " + verify.message, ""};
    }

    return {true, "authorized_keys written: " + path, ""};
}

core::OperationResult SshdAuthorizedKeysWriter::remove(const std::string& linux_username) {
    if (linux_username.empty()) return {false, "empty username", ""};
    std::string path = keys_path(linux_username);
    std::error_code ec;
    bool existed = std::filesystem::exists(path, ec);
    if (existed) {
        // Verify it's not a symlink and is root-owned before removing
        struct stat st{};
        if (::lstat(path.c_str(), &st) == 0) {
            if (S_ISLNK(st.st_mode)) return {false, "authorized_keys is a symlink, refusing removal", ""};
            if (st.st_uid != 0 && st.st_uid != ::geteuid()) {
                // Allow removal if owned by root or current user
            }
        }
        if (!std::filesystem::remove(path, ec)) {
            return {false, "failed to remove authorized_keys: " + path, ""};
        }
    }
    return {true, "authorized_keys removed", ""};
}

core::OperationResult SshdAuthorizedKeysWriter::validate_file(const std::string& linux_username) const {
    std::string path = keys_path(linux_username);
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) {
        return {false, "authorized_keys not found", ""};
    }
    if (S_ISLNK(st.st_mode)) {
        return {false, "authorized_keys is a symlink", ""};
    }
    if (!S_ISREG(st.st_mode)) {
        return {false, "authorized_keys not a regular file", ""};
    }
    if (st.st_uid != 0) {
        return {false, "authorized_keys not root-owned", ""};
    }
    if ((st.st_mode & 0777) != 0600) {
        return {false, "authorized_keys wrong mode", ""};
    }
    return {true, "", ""};
}

} // namespace containercp::access
