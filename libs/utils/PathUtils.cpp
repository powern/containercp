#include "PathUtils.h"

namespace containercp::utils {

std::filesystem::path normalize_path_for_comparison(const std::filesystem::path& path) {
    const std::filesystem::path normalized = path.lexically_normal();
    std::filesystem::path cleaned;

    for (const auto& part : normalized) {
        if (part.empty() || part == ".") {
            continue;
        }
        cleaned /= part;
    }

    if (!cleaned.empty()) {
        return cleaned;
    }
    if (normalized.has_root_path()) {
        return normalized.root_path();
    }
    return {};
}

bool path_has_prefix(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto normalized_path = normalize_path_for_comparison(path);
    const auto normalized_root = normalize_path_for_comparison(root);

    if (normalized_path.empty() || normalized_root.empty()) {
        return false;
    }

    auto path_it = normalized_path.begin();
    auto root_it = normalized_root.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++path_it) {
        if (path_it == normalized_path.end() || *path_it != *root_it) {
            return false;
        }
    }
    return true;
}

} // namespace containercp::utils
