#include "wordpress/WordPressCliAudit.h"

#include "logger/Logger.h"

#include <cctype>
#include <sstream>

namespace containercp::wordpress {
namespace {

std::string audit_value(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (unsigned char c : value) {
        if (std::iscntrl(c) != 0 || std::isspace(c) != 0 || c == '=') {
            sanitized.push_back('_');
        } else {
            sanitized.push_back(static_cast<char>(c));
        }
    }
    return sanitized;
}

} // namespace

std::string WordPressCliAuditLogger::format(const WordPressCliAuditEvent& event) {
    std::ostringstream out;
    out << "wordpress_cli"
        << " job_id=" << event.job_id
        << " site_id=" << event.site_id
        << " domain=" << audit_value(event.domain)
        << " admin_username=" << audit_value(event.admin_username)
        << " operation=" << audit_value(event.operation)
        << " result=" << audit_value(event.result)
        << " timed_out=" << (event.timed_out ? "true" : "false")
        << " cleanup_succeeded=" << (event.cleanup_succeeded ? "true" : "false");
    if (!event.package_id.empty()) {
        out << " package_id=" << audit_value(event.package_id);
    }
    if (!event.error_code.empty()) {
        out << " error_code=" << audit_value(event.error_code);
    }
    return out.str();
}

void WordPressCliAuditLogger::log(const WordPressCliAuditEvent& event) {
    const auto message = format(event);
    switch (event.level) {
    case WordPressCliAuditEvent::Level::Info:
        logger::Logger::instance().info("AUDIT", message);
        break;
    case WordPressCliAuditEvent::Level::Warning:
        logger::Logger::instance().warning("AUDIT", message);
        break;
    case WordPressCliAuditEvent::Level::Error:
        logger::Logger::instance().error("AUDIT", message);
        break;
    }
}

} // namespace containercp::wordpress
