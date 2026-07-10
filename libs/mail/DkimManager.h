#ifndef CONTAINERCP_MAIL_DKIM_MANAGER_H
#define CONTAINERCP_MAIL_DKIM_MANAGER_H

#include "core/OperationResult.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <string>

namespace containercp::mail {

// DKIM key management for mail domains.
//
// Handles key generation and DNS record preparation using
// CommandExecutor (safe fork/exec with poll) — no std::system,
// no fixed temp files.
//
// Independent from any specific mail provider or runtime.
// A future MailProvider consumes the generated keys rather
// than owning the creation process.
//
// Design: regular class (not static helper) so future versions
// can inject Logger, CommandExecutor, or crypto dependencies
// without changing the public API.
class DkimManager {
public:
    explicit DkimManager(logger::Logger& logger);

    // Generate a 2048-bit RSA DKIM key pair for a domain.
    //   dkim_dir   — base directory for DKIM key storage
    //   domain     — the domain name (e.g. example.com)
    //   selector   — DNS selector (e.g. "dkim")
    //
    // Returns the DKIM DNS TXT record value on success.
    // Returns empty string on failure.  Logs errors via logger.
    std::string generate_key(const std::string& dkim_dir,
                              const std::string& domain,
                              const std::string& selector);

private:
    // Validate that a domain or selector contains only safe characters.
    // Returns empty string on success, error message on failure.
    static std::string validate_label(const std::string& label,
                                       const std::string& name);

    logger::Logger& logger_;
    runtime::CommandExecutor executor_;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_DKIM_MANAGER_H
