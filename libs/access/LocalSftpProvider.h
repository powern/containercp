#ifndef CONTAINERCP_ACCESS_LOCAL_SFTP_PROVIDER_H
#define CONTAINERCP_ACCESS_LOCAL_SFTP_PROVIDER_H

#include "access/AccessProvider.h"
#include "logger/Logger.h"

namespace containercp::access {

class LocalSftpProvider : public AccessProvider {
public:
    explicit LocalSftpProvider(logger::Logger& logger);

    core::OperationResult create_user(const AccessUser& user) override;
    core::OperationResult remove_user(const AccessUser& user) override;
    core::OperationResult enable_user(const AccessUser& user) override;
    core::OperationResult disable_user(const AccessUser& user) override;
    core::OperationResult list_users() override;
    core::OperationResult show_user(const AccessUser& user) override;

private:
    logger::Logger& logger_;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_LOCAL_SFTP_PROVIDER_H
