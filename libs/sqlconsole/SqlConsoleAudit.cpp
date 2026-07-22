#include "sqlconsole/SqlConsoleAudit.h"

#include "logger/Logger.h"

#include <cctype>
#include <sstream>
#include <string>

namespace containercp::sqlconsole {
namespace {

bool sensitive_key(const std::string& lower, std::size_t pos) {
    const char* keys[] = {"password", "secret", "token", "credential", "cookie"};
    for (const char* key : keys) {
        const std::string needle(key);
        if (lower.compare(pos, needle.size(), needle) == 0) {
            return true;
        }
    }
    return false;
}

std::string redact_sensitive_fragments(const std::string& value) {
    std::string lower;
    lower.reserve(value.size());
    for (unsigned char c : value) {
        lower.push_back(static_cast<char>(std::tolower(c)));
    }

    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size();) {
        if (sensitive_key(lower, i)) {
            const std::size_t key_end = lower.find_first_of("=:", i);
            if (key_end != std::string::npos) {
                bool only_key_chars = true;
                for (std::size_t j = i; j < key_end; ++j) {
                    const char c = lower[j];
                    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
                        only_key_chars = false;
                        break;
                    }
                }
                if (only_key_chars) {
                    out.append(value.substr(i, key_end - i + 1));
                    out.append("<redacted>");
                    i = key_end + 1;
                    while (i < value.size()) {
                        const unsigned char c = static_cast<unsigned char>(value[i]);
                        if (std::isspace(c) != 0 || value[i] == '&' || value[i] == ',' || value[i] == ';') {
                            break;
                        }
                        ++i;
                    }
                    continue;
                }
            }
        }
        out.push_back(value[i++]);
    }
    return out;
}

std::string audit_value(const std::string& value) {
    const std::string redacted = redact_sensitive_fragments(value);
    std::string sanitized;
    sanitized.reserve(redacted.size());
    for (unsigned char c : redacted) {
        sanitized.push_back((std::iscntrl(c) != 0 || std::isspace(c) != 0) ? '_' : static_cast<char>(c));
    }
    return sanitized;
}

} // namespace

std::string SqlConsoleAuditLogger::format(const SqlConsoleAuditEvent& event) {
    std::ostringstream out;
    out << "sql_console"
        << " operation=" << audit_value(event.operation)
        << " stage=" << audit_value(event.stage)
        << " result=" << audit_value(event.result)
        << " launch_id=" << audit_value(event.launch_id)
        << " database_id=" << event.database_id
        << " site_id=" << event.site_id
        << " admin_username=" << audit_value(event.admin_username)
        << " provider=" << audit_value(event.provider)
        << " status=" << audit_value(event.status);
    if (!event.error_code.empty()) {
        out << " error_code=" << audit_value(event.error_code);
    }
    return out.str();
}

void SqlConsoleAuditLogger::log(const SqlConsoleAuditEvent& event) {
    const auto message = format(event);
    switch (event.level) {
    case SqlConsoleAuditEvent::Level::Info:
        logger::Logger::instance().info("AUDIT", message);
        break;
    case SqlConsoleAuditEvent::Level::Warning:
        logger::Logger::instance().warning("AUDIT", message);
        break;
    case SqlConsoleAuditEvent::Level::Error:
        logger::Logger::instance().error("AUDIT", message);
        break;
    }
}

} // namespace containercp::sqlconsole
