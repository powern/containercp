#include "database/DatabaseLifecycleAudit.h"

#include "logger/Logger.h"

#include <cctype>
#include <sstream>

namespace containercp::database {
namespace {

std::string audit_value(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (unsigned char c : value) {
        sanitized.push_back((std::iscntrl(c) != 0 || std::isspace(c) != 0) ? '_' : static_cast<char>(c));
    }
    return sanitized;
}

} // namespace

std::string DatabaseLifecycleAuditLogger::format(const DatabaseLifecycleAuditEvent& event) {
    std::ostringstream out;
    out << "database_lifecycle"
        << " operation=" << audit_value(event.operation)
        << " stage=" << audit_value(event.stage)
        << " result=" << audit_value(event.result)
        << " job_id=" << event.job_id
        << " site_id=" << event.site_id
        << " database_id=" << event.database_id
        << " domain=" << audit_value(event.domain);
    if (!event.error_code.empty()) {
        out << " error_code=" << audit_value(event.error_code);
    }
    if (event.manual_recovery_required) {
        out << " manual_recovery_required=true";
    }
    return out.str();
}

void DatabaseLifecycleAuditLogger::log(const DatabaseLifecycleAuditEvent& event) {
    const auto message = format(event);
    switch (event.level) {
    case DatabaseLifecycleAuditEvent::Level::Info:
        logger::Logger::instance().info("AUDIT", message);
        break;
    case DatabaseLifecycleAuditEvent::Level::Warning:
        logger::Logger::instance().warning("AUDIT", message);
        break;
    case DatabaseLifecycleAuditEvent::Level::Error:
        logger::Logger::instance().error("AUDIT", message);
        break;
    }
}

} // namespace containercp::database
