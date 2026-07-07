#ifndef CONTAINERCP_ACCESS_ACCESS_PROVIDER_H
#define CONTAINERCP_ACCESS_ACCESS_PROVIDER_H

#include "core/OperationResult.h"
#include "access/AccessUser.h"

namespace containercp::access {

class AccessProvider {
public:
    virtual ~AccessProvider() = default;

    virtual core::OperationResult create_user(const AccessUser& user) = 0;
    virtual core::OperationResult remove_user(const AccessUser& user) = 0;
    virtual core::OperationResult enable_user(const AccessUser& user) = 0;
    virtual core::OperationResult disable_user(const AccessUser& user) = 0;
    virtual core::OperationResult list_users() = 0;
    virtual core::OperationResult show_user(const AccessUser& user) = 0;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_ACCESS_PROVIDER_H
