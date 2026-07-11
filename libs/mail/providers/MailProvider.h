#ifndef CONTAINERCP_MAIL_PROVIDERS_MAIL_PROVIDER_H
#define CONTAINERCP_MAIL_PROVIDERS_MAIL_PROVIDER_H

#include "core/OperationResult.h"
#include "mail/MailDomain.h"
#include "mail/MailboxManager.h"
#include "mail/MailAliasManager.h"

#include <string>
#include <vector>

namespace containercp::mail {

// Result of a transactional config apply.
// failed_stage identifies where the pipeline stopped.
struct ApplyResult {
    bool success = false;
    std::string message;
    std::string failed_stage;  // "generate", "validate", "reload", "health"
};

// Abstract interface for mail server implementations.
//
// The API and UI depend on this interface, not on specific
// mail server software.  Future providers (external relay,
// Microsoft 365, Google Workspace, Podman) implement this
// interface without changes to the API layer.
//
// Lifecycle:
//   write_configs() → start() → [reload()] → stop()
//   Config generation is separate from runtime management.
class MailProvider {
public:
    virtual ~MailProvider() = default;

    // Write configuration files from the current data model.
    // Does NOT start or restart services.
    // Called before start() and by regenerate endpoint.
    virtual core::OperationResult write_configs(
        const std::vector<MailDomain>& domains,
        const MailboxManager& mailboxes,
        const MailAliasManager& aliases) = 0;

    // Prepare runtime resources (directories, network, etc.)
    // Called before start().  Idempotent — safe to call multiple times.
    virtual core::OperationResult prepare_environment() = 0;

    // Start the mail service.  Creates containers, starts daemons.
    // Requires write_configs() and prepare_environment() to have
    // been called first.
    virtual core::OperationResult start() = 0;

    // Stop the mail service.  Preserves configuration and data.
    virtual core::OperationResult stop() = 0;

    // Reload configuration without full restart.
    virtual core::OperationResult reload() = 0;

    // Transactional config apply: generate → validate → reload.
    // Validates with postfix check before reloading.
    // On validation or reload failure, rolls back to previous config.
    // Thread-safe.
    virtual ApplyResult apply_config(
        const std::vector<MailDomain>& domains,
        const MailboxManager& mailboxes,
        const MailAliasManager& aliases) = 0;

    // Check if the mail service is running.
    virtual bool is_running() const = 0;

    // Get a human-readable status description.
    virtual std::string status_description() const = 0;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_PROVIDERS_MAIL_PROVIDER_H
