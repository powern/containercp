#include "access/ManagedPathValidator.h"

#include <filesystem>
#include <system_error>

namespace containercp::access {
namespace {

bool contains_control_chars(const std::string& s) {
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

bool has_disallowed_component(const std::string& s, std::string& error) {
    if (s.find("//") != std::string::npos) {
        error = "repeated separator";
        return true;
    }
    std::string::size_type pos = 0;
    while (pos < s.size()) {
        auto slash = s.find('/', pos);
        std::string comp = (slash == std::string::npos) ? s.substr(pos) : s.substr(pos, slash - pos);
        if (comp == ".") {
            error = "path contains .";
            return true;
        }
        if (comp == "..") {
            error = "path contains ..";
            return true;
        }
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    return false;
}

} // namespace

PathValidation validate_managed_path(const std::string& path, const std::string& managed_root) {
    PathValidation v;
    if (path.empty() || managed_root.empty()) { v.error = "empty path"; return v; }
    if (path[0] != '/') { v.error = "relative path"; return v; }
    if (contains_control_chars(path) || contains_control_chars(managed_root)) { v.error = "control chars"; return v; }
    if (has_disallowed_component(path, v.error)) return v;
    if (has_disallowed_component(managed_root, v.error)) return v;

    std::error_code ec;
    auto abs = std::filesystem::absolute(path, ec);
    if (ec) { v.error = "path resolution failed"; return v; }

    auto root_abs = std::filesystem::absolute(managed_root, ec);
    if (ec) { v.error = "root resolution failed"; return v; }

    // No symlink at final target. Check the caller-provided absolute path, not
    // only the canonical target, otherwise a final symlink would be resolved
    // before is_symlink() sees it.
    if (std::filesystem::is_symlink(abs, ec)) { v.error = "final path is symlink"; return v; }
    if (ec && ec != std::errc::no_such_file_or_directory) { v.error = "symlink check failed"; return v; }

    // Validate each existing parent component from the original absolute path.
    // Canonical paths resolve symlinks; walking the lexical path catches
    // symlink escapes before mutation.
    std::filesystem::path current;
    for (const auto& part : abs) {
        current /= part;
        if (current == abs) break;
        if (std::filesystem::is_symlink(current, ec)) { v.error = "parent component is symlink"; return v; }
        if (ec && ec != std::errc::no_such_file_or_directory) { v.error = "symlink check failed"; return v; }
    }

    auto canon = std::filesystem::weakly_canonical(abs, ec);
    if (ec) { v.error = "canonical resolution failed"; return v; }
    if (canon.empty()) { v.error = "empty canonical"; return v; }

    auto root_canon = std::filesystem::weakly_canonical(root_abs, ec);
    if (ec || root_canon.empty()) { v.error = "root canonical failed"; return v; }

    std::string root_exact = root_canon.string();
    std::string path_str = canon.string();
    if (path_str == root_exact) { v.error = "path equals managed root"; return v; }

    std::string root_prefix = root_exact;
    if (!root_prefix.empty() && root_prefix.back() != '/') root_prefix += '/';
    if (path_str.rfind(root_prefix, 0) != 0) { v.error = "outside managed root"; return v; }

    v.ok = true; v.canonical = path_str;
    return v;
}

} // namespace containercp::access
