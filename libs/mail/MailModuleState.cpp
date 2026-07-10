#include "MailModuleState.h"

namespace containercp::mail {

std::string mail_module_state_to_string(MailModuleState state) {
    switch (state) {
        case MailModuleState::Inactive: return "inactive";
        case MailModuleState::Active:   return "active";
        case MailModuleState::Error:    return "error";
    }
    return "inactive";
}

MailModuleState mail_module_state_from_string(const std::string& s) {
    if (s == "active")  return MailModuleState::Active;
    if (s == "error")   return MailModuleState::Error;
    return MailModuleState::Inactive;
}

} // namespace containercp::mail
