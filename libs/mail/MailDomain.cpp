#include "MailDomain.h"

#include <algorithm>
#include <cctype>

namespace containercp::mail {

std::string mail_domain_mode_to_string(MailDomainMode mode) {
    switch (mode) {
        case MailDomainMode::Disabled:       return "disabled";
        case MailDomainMode::LocalPrimary:   return "local-primary";
        case MailDomainMode::ExternalRelay:  return "external-relay";
        case MailDomainMode::SplitM365:      return "split-m365";
    }
    return "disabled";
}

MailDomainMode mail_domain_mode_from_string(const std::string& s) {
    if (s == "local-primary")   return MailDomainMode::LocalPrimary;
    if (s == "external-relay")  return MailDomainMode::ExternalRelay;
    if (s == "split-m365")      return MailDomainMode::SplitM365;
    return MailDomainMode::Disabled;
}

} // namespace containercp::mail
