#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_DETECTOR_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_DETECTOR_H

#include "WordPressConfigTypes.h"

#include <filesystem>
#include <string>

namespace containercp::wordpress {

struct WordPressConfigPathSafety {
    bool safe = false;
    WordPressCredentialStatus status = WordPressCredentialStatus::UnsafePath;
    std::string code;
    std::string message;
    std::filesystem::path site_root;
    std::filesystem::path config_path;
};

class WordPressConfigDetector {
public:
    WordPressConfigInspection inspect_content(const std::string& content) const;
    WordPressConfigPathSafety inspect_config_path(const std::filesystem::path& site_root,
                                                  const std::filesystem::path& candidate_path) const;

    static bool is_active_config_filename(const std::filesystem::path& path);
};

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_DETECTOR_H
