#include "LocalSftpProvider.h"

namespace containercp::access {

LocalSftpProvider::LocalSftpProvider(logger::Logger& logger)
    : logger_(logger)
{
}

core::OperationResult LocalSftpProvider::create_user(const AccessUser& user) {
    logger_.info("LocalSftpProvider: Creating user " + user.username + " for " + user.domain);
    return {true, ""};
}

core::OperationResult LocalSftpProvider::remove_user(const AccessUser& user) {
    logger_.info("LocalSftpProvider: Removing user " + user.username);
    return {true, ""};
}

core::OperationResult LocalSftpProvider::enable_user(const AccessUser& user) {
    logger_.info("LocalSftpProvider: Enabling user " + user.username);
    return {true, ""};
}

core::OperationResult LocalSftpProvider::disable_user(const AccessUser& user) {
    logger_.info("LocalSftpProvider: Disabling user " + user.username);
    return {true, ""};
}

core::OperationResult LocalSftpProvider::list_users() {
    logger_.info("LocalSftpProvider: Listing users");
    return {true, ""};
}

core::OperationResult LocalSftpProvider::show_user(const AccessUser& user) {
    logger_.info("LocalSftpProvider: Showing user " + user.username);
    return {true, ""};
}

} // namespace containercp::access
