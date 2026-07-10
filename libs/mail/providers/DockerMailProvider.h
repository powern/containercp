#ifndef CONTAINERCP_MAIL_PROVIDERS_DOCKER_MAIL_PROVIDER_H
#define CONTAINERCP_MAIL_PROVIDERS_DOCKER_MAIL_PROVIDER_H

#include "mail/providers/MailProvider.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <string>

namespace containercp::mail {

class DockerMailProvider : public MailProvider {
public:
    explicit DockerMailProvider(logger::Logger& logger,
                                const std::string& data_root);

    core::OperationResult apply_config(
        const std::vector<MailDomain>& domains,
        const MailboxManager& mailboxes,
        const MailAliasManager& aliases) override;

    core::OperationResult start() override;
    core::OperationResult stop() override;
    bool is_running() const override;
    std::string status_description() const override;

    // Generate docker-compose.yml for the mail stack.
    std::string generate_compose() const;

    // Directories
    std::string compose_dir() const;
    std::string config_dir() const;

    // Container name
    static std::string container_name(const std::string& service);

private:
    core::OperationResult ensure_directories();
    core::OperationResult ensure_network();

    logger::Logger& logger_;
    std::string data_root_;
    runtime::CommandExecutor executor_;
};

} // namespace containercp::mail

#endif // CONTAINERCP_MAIL_PROVIDERS_DOCKER_MAIL_PROVIDER_H
