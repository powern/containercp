#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_UPDATER_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_UPDATER_H

#include <string>

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

class WordPressConfigUpdater {
public:
    WordPressConfigUpdateResult render_update(const std::string& content,
                                              WordPressConfigUpdateField field,
                                              const std::string& new_value) const;
};

std::string wordpress_update_field_name(WordPressConfigUpdateField field);

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_UPDATER_H
