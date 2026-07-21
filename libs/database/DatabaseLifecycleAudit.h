#ifndef CONTAINERCP_DATABASE_DATABASE_LIFECYCLE_AUDIT_H
#define CONTAINERCP_DATABASE_DATABASE_LIFECYCLE_AUDIT_H

#include <cstdint>
#include <string>

namespace containercp::database {

struct DatabaseLifecycleAuditEvent {
    enum class Level { Info, Warning, Error };

    std::string operation;
    std::string stage;
    std::string result;
    std::string error_code;
    uint64_t job_id = 0;
    uint64_t site_id = 0;
    uint64_t database_id = 0;
    std::string domain;
    bool manual_recovery_required = false;
    Level level = Level::Info;
};

class DatabaseLifecycleAuditLogger {
public:
    static void log(const DatabaseLifecycleAuditEvent& event);
    static std::string format(const DatabaseLifecycleAuditEvent& event);
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_LIFECYCLE_AUDIT_H
