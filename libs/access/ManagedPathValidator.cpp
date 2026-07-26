#include "access/ManagedPathValidator.h"

#include <filesystem>
#include <system_error>

namespace containercp::access {

PathValidation validate_managed_path(const std::string& path, const std::string& managed_root) {
    PathValidation v;
    if (path.empty() || managed_root.empty()) { v.error = "empty path"; return v; }
    if (path[0] != '/') { v.error = "relative path"; return v; }
    if (path.find("..") != std::string::npos) { v.error = "path contains .."; return v; }

    std::error_code ec;
    auto abs = std::filesystem::absolute(path, ec);
    if (ec) { v.error = "path resolution failed"; return v; }
    auto canon = std::filesystem::weakly_canonical(abs, ec);
    if (ec) { v.error = "canonical resolution failed"; return v; }
    if (canon.empty()) { v.error = "empty canonical"; return v; }

    auto root_abs = std::filesystem::absolute(managed_root, ec);
    if (ec) { v.error = "root resolution failed"; return v; }
    auto root_canon = std::filesystem::weakly_canonical(root_abs, ec);
    if (ec || root_canon.empty()) { v.error = "root canonical failed"; return v; }

    std::string root_str = root_canon.string();
    if (!root_str.empty() && root_str.back() != '/') root_str += '/';
    std::string path_str = canon.string();

    if (path_str.rfind(root_str, 0) != 0) { v.error = "outside managed root"; return v; }
    if (path_str == root_str) { v.error = "path equals managed root"; return v; }

    // No symlink at final target
    if (std::filesystem::is_symlink(canon, ec)) { v.error = "final path is symlink"; return v; }
    if (ec && ec != std::errc::no_such_file_or_directory) { v.error = "symlink check failed"; return v; }

    // Validate each parent component is not a symlink
    auto parent = canon.parent_path();
    while (parent != root_canon && parent != parent.parent_path()) {
        if (std::filesystem::is_symlink(parent, ec)) { v.error = "parent component is symlink"; return v; }
        if (ec && ec != std::errc::no_such_file_or_directory) break;
        parent = parent.parent_path();
    }

    v.ok = true; v.canonical = path_str;
    return v;
}

} // namespace containercp::access
