#ifndef CONTAINERCP_MAIL_MAIL_MODULE_STATE_H
#define CONTAINERCP_MAIL_MAIL_MODULE_STATE_H

#include <string>

namespace containercp::mail {

enum class MailModuleState {
    Inactive,   // Mail module available but not enabled (default)
    Active,     // Mail module is enabled and running
    Error,      // Mail module encountered an error
};

std::string mail_module_state_to_string(MailModuleState state);
MailModuleState mail_module_state_from_string(const std::string& s);

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_MAIL_MODULE_STATE_H
