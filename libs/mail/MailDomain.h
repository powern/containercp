#ifndef CONTAINERCP_MAIL_MAIL_DOMAIN_H
#define CONTAINERCP_MAIL_MAIL_DOMAIN_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::mail {

// Domain modes for the mail subsystem.
// New modes can be added without changing existing code.
enum class MailDomainMode {
    Disabled,       // Mail not handled by ContainerCP
    LocalPrimary,   // ContainerCP is the primary mail server
    ExternalRelay,  // External provider (Google Workspace, etc.)
    SplitM365,      // Microsoft 365 hybrid/split delivery
};

// Convert mode enum to string.
std::string mail_domain_mode_to_string(MailDomainMode mode);

// Strict validation for API input. Returns true only for known mode strings.
bool is_valid_mail_domain_mode(const std::string& s);

// Parse mode string to enum. Returns Disabled for unknown values (tolerant).
MailDomainMode mail_domain_mode_from_string(const std::string& s);

struct MailDomain : core::Resource {
    uint64_t domain_id = 0;             // FK to ContainerCP Domain (0 = external)
    uint64_t site_id = 0;               // FK to Site (derived from Domain, 0 = external)
    std::string domain_name;            // e.g. "example.com" (duplicated from Domain for convenience)
    MailDomainMode mode = MailDomainMode::Disabled;

    // External relay settings
    std::string relay_host;             // SMTP relay host for external-relay mode

    // DKIM
    std::string dkim_selector = "dkim";
    std::string dkim_private_key_path;
    std::string dkim_public_key_dns;

    // Limits
    uint64_t max_mailboxes = 0;         // 0 = unlimited
    uint64_t max_aliases = 0;           // 0 = unlimited

    // Catch-all
    std::string catch_all;

    // Flags
    bool enabled = true;

    // Timestamps
    std::string created_at;
    std::string updated_at;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_MAIL_DOMAIN_H
