#ifndef CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_AUDIT_H
#define CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_AUDIT_H

#include <cstdint>
#include <optional>
#include <string>

namespace containercp::database {

struct DatabaseCredentialRotationAuditEvent {
    enum class Level {
        Info,
        Warning,
        Error,
    };

    uint64_t job_id = 0;
    uint64_t site_id = 0;
    std::string domain;
    uint64_t database_id = 0;
    std::string stage;
    std::string result;
    std::string error_code;
    std::optional<bool> compensation_started;
    std::string compensation_result;
    std::optional<bool> manual_recovery_required;
    std::optional<uint64_t> duration_ms;
    Level level = Level::Info;
};

class DatabaseCredentialRotationAuditLogger {
public:
    static void log(const DatabaseCredentialRotationAuditEvent& event);
    static std::string format(const DatabaseCredentialRotationAuditEvent& event);
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_AUDIT_H
