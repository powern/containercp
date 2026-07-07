#ifndef CONTAINERCP_PROVIDER_HOSTING_PROVIDER_H
#define CONTAINERCP_PROVIDER_HOSTING_PROVIDER_H

#include "core/OperationResult.h"
#include "site/Site.h"

namespace containercp::provider {

class HostingProvider {
public:
    virtual ~HostingProvider() = default;

    virtual core::OperationResult create_site(site::Site& site) = 0;
    virtual core::OperationResult remove_site(site::Site& site) = 0;
    virtual core::OperationResult start_site(site::Site& site) = 0;
    virtual core::OperationResult stop_site(site::Site& site) = 0;
    virtual core::OperationResult status(site::Site& site) = 0;
};

} // namespace containercp::provider

#endif // CONTAINERCP_PROVIDER_HOSTING_PROVIDER_H
