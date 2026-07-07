#ifndef CONTAINERCP_PROVIDER_DOCKER_COMPOSE_PROVIDER_H
#define CONTAINERCP_PROVIDER_DOCKER_COMPOSE_PROVIDER_H

#include "provider/HostingProvider.h"
#include "config/Config.h"
#include "docker/ComposeGenerator.h"
#include "filesystem/Filesystem.h"
#include "runtime/Runtime.h"

namespace containercp::provider {

class DockerComposeProvider : public HostingProvider {
public:
    DockerComposeProvider(filesystem::Filesystem& fs, config::Config& cfg, runtime::Runtime& rt);

    core::OperationResult create_site(site::Site& site) override;
    core::OperationResult remove_site(site::Site& site) override;
    core::OperationResult start_site(site::Site& site) override;
    core::OperationResult stop_site(site::Site& site) override;
    core::OperationResult status(site::Site& site) override;

private:
    filesystem::Filesystem& fs_;
    config::Config& cfg_;
    runtime::Runtime& rt_;
};

} // namespace containercp::provider

#endif // CONTAINERCP_PROVIDER_DOCKER_COMPOSE_PROVIDER_H
