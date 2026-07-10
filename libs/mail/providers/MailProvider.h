#ifndef CONTAINERCP_MAIL_PROVIDERS_MAIL_PROVIDER_H
#define CONTAINERCP_MAIL_PROVIDERS_MAIL_PROVIDER_H

#include "core/OperationResult.h"
#include "mail/MailDomain.h"
#include "mail/MailboxManager.h"
#include "mail/MailAliasManager.h"

#include <string>

namespace containercp::mail {

// Abstract interface for mail server implementations.
//
// The API and UI depend on this interface, not on specific
// mail server software.  Future providers (external relay,
// Microsoft 365, Google Workspace) implement this interface.
class MailProvider {
public:
    virtual ~MailProvider() = default;

    // Apply the current configuration to the mail server.
    // Called after any domain/mailbox/alias change.
    // Implementations should regenerate configs and reload services.
    virtual core::OperationResult apply_config(
        const std::vector<MailDomain>& domains,
        const MailboxManager& mailboxes,
        const MailAliasManager& aliases) = 0;

    // Start the mail server (create containers, start services).
    virtual core::OperationResult start() = 0;

    // Stop the mail server (stop containers, preserve config).
    virtual core::OperationResult stop() = 0;

    // Check if the mail server is running.
    virtual bool is_running() const = 0;

    // Get a human-readable status description.
    virtual std::string status_description() const = 0;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_PROVIDERS_MAIL_PROVIDER_H
