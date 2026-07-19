#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_UPDATER_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_UPDATER_H

#include <string>
#include <filesystem>

namespace containercp::wordpress {

enum class WordPressConfigUpdateField {
    DbName,
    DbUser,
    DbPassword,
    DbHost
};

struct WordPressConfigUpdateResult {
    bool success = false;
    std::string code;
    std::string message;
    std::string content;
};

struct WordPressConfigRollbackHandle {
    bool valid = false;
    std::filesystem::path site_root;
    std::filesystem::path config_path;
    std::string previous_content;
    unsigned int mode = 0;
    unsigned int uid = 0;
    unsigned int gid = 0;
};

struct WordPressConfigFileUpdateResult {
    bool success = false;
    std::string code;
    std::string message;
    WordPressConfigRollbackHandle rollback;
};

class WordPressConfigUpdater {
public:
    WordPressConfigUpdateResult render_update(const std::string& content,
                                              WordPressConfigUpdateField field,
                                              const std::string& new_value) const;
    WordPressConfigFileUpdateResult update_file_atomic(const std::filesystem::path& site_root,
                                                       const std::filesystem::path& config_path,
                                                       WordPressConfigUpdateField field,
                                                       const std::string& new_value) const;
    WordPressConfigFileUpdateResult rollback_file(const WordPressConfigRollbackHandle& rollback) const;
};

std::string wordpress_update_field_name(WordPressConfigUpdateField field);

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_UPDATER_H
