#ifndef CONTAINERCP_MAIL_PROVIDERS_DOCKER_MAIL_PROVIDER_H
#define CONTAINERCP_MAIL_PROVIDERS_DOCKER_MAIL_PROVIDER_H

#include "mail/providers/MailProvider.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <string>

namespace containercp::mail {

// Docker Compose-based mail provider.
//
// Implements MailProvider using Docker containers for Postfix,
// Dovecot, and Redis.  Configuration is generated from the
// ContainerCP data model, not edited manually.
class DockerMailProvider : public MailProvider {
public:
    explicit DockerMailProvider(logger::Logger& logger,
                                const std::string& data_root);

    // MailProvider interface
    core::OperationResult write_configs(
        const std::vector<MailDomain>& domains,
        const MailboxManager& mailboxes,
        const MailAliasManager& aliases) override;

    core::OperationResult prepare_environment() override;
    core::OperationResult start() override;
    core::OperationResult stop() override;
    core::OperationResult reload() override;
    bool is_running() const override;
    std::string status_description() const override;

    // Paths
    std::string compose_dir() const;
    std::string config_dir() const;

    // Generate DKIM key pair for a domain.
    // Returns the DKIM DNS record text (TXT value).
    static std::string generate_dkim_key(const std::string& dkim_dir,
                                          const std::string& domain_name,
                                          const std::string& selector);

private:
    std::string compose_project_flag() const;
    core::OperationResult write_docker_compose();
    core::OperationResult write_postfix_config(
        const std::vector<MailDomain>& domains,
        const MailboxManager& mailboxes);
    core::OperationResult write_dovecot_config(
        const std::vector<MailDomain>& domains,
        const MailboxManager& mailboxes);
    core::OperationResult write_transport_maps(
        const std::vector<MailDomain>& domains);

    logger::Logger& logger_;
    std::string data_root_;
    runtime::CommandExecutor executor_;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_PROVIDERS_DOCKER_MAIL_PROVIDER_H
