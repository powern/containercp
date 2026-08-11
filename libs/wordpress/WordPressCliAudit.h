#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_CLI_AUDIT_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_CLI_AUDIT_H

#include <cstdint>
#include <string>

namespace containercp::wordpress {

struct WordPressCliAuditEvent {
    enum class Level { Info, Warning, Error };

    uint64_t job_id = 0;
    uint64_t site_id = 0;
    std::string domain;
    std::string admin_username;
    std::string operation;
    std::string package_id;
    std::string result;
    std::string error_code;
    bool timed_out = false;
    bool cleanup_succeeded = false;
    Level level = Level::Info;
};

class WordPressCliAuditLogger {
public:
    static void log(const WordPressCliAuditEvent& event);
    static std::string format(const WordPressCliAuditEvent& event);
};

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_CLI_AUDIT_H
