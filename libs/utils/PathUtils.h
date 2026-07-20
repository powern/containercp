#ifndef CONTAINERCP_UTILS_PATHUTILS_H
#define CONTAINERCP_UTILS_PATHUTILS_H

#include <filesystem>

namespace containercp::utils {

std::filesystem::path normalize_path_for_comparison(const std::filesystem::path& path);
bool path_has_prefix(const std::filesystem::path& path, const std::filesystem::path& root);

} // namespace containercp::utils

#endif // CONTAINERCP_UTILS_PATHUTILS_H
