#include "database/DatabaseCredentialRotationAudit.h"

#include "logger/Logger.h"

#include <cctype>
#include <sstream>

namespace containercp::database {
namespace {

std::string field_value(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (unsigned char c : value) {
        if (std::iscntrl(c) != 0 || std::isspace(c) != 0) {
            sanitized.push_back('_');
        } else {
            sanitized.push_back(static_cast<char>(c));
        }
    }
    return sanitized;
}

const char* bool_value(bool value) {
    return value ? "true" : "false";
}

} // namespace

std::string DatabaseCredentialRotationAuditLogger::format(const DatabaseCredentialRotationAuditEvent& event) {
    std::ostringstream out;
    out << "wordpress_db_credential_rotation"
        << " job_id=" << event.job_id
        << " site_id=" << event.site_id
        << " domain=" << field_value(event.domain)
        << " database_id=" << event.database_id
        << " stage=" << field_value(event.stage)
        << " result=" << field_value(event.result);
    if (!event.error_code.empty()) {
        out << " error_code=" << field_value(event.error_code);
    }
    if (event.compensation_started.has_value()) {
        out << " compensation_started=" << bool_value(*event.compensation_started);
    }
    if (!event.compensation_result.empty()) {
        out << " compensation_result=" << field_value(event.compensation_result);
    }
    if (event.manual_recovery_required.has_value()) {
        out << " manual_recovery_required=" << bool_value(*event.manual_recovery_required);
    }
    if (event.duration_ms.has_value()) {
        out << " duration_ms=" << *event.duration_ms;
    }
    return out.str();
}

void DatabaseCredentialRotationAuditLogger::log(const DatabaseCredentialRotationAuditEvent& event) {
    const auto message = format(event);
    switch (event.level) {
    case DatabaseCredentialRotationAuditEvent::Level::Info:
        logger::Logger::instance().info("AUDIT", message);
        break;
    case DatabaseCredentialRotationAuditEvent::Level::Warning:
        logger::Logger::instance().warning("AUDIT", message);
        break;
    case DatabaseCredentialRotationAuditEvent::Level::Error:
        logger::Logger::instance().error("AUDIT", message);
        break;
    }
}

} // namespace containercp::database
