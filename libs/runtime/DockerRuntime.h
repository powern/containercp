#ifndef CONTAINERCP_RUNTIME_DOCKER_RUNTIME_H
#define CONTAINERCP_RUNTIME_DOCKER_RUNTIME_H

#include "runtime/Runtime.h"
#include "logger/Logger.h"

#include <string>

namespace containercp::runtime {

class DockerRuntime : public Runtime {
public:
    DockerRuntime(logger::Logger& logger, const std::string& sites_root);

    core::OperationResult create_site_stack(const std::string& domain) override;
    core::OperationResult start_site(const std::string& domain) override;
    core::OperationResult stop_site(const std::string& domain) override;
    core::OperationResult remove_site(const std::string& domain) override;
    core::OperationResult status(const std::string& domain) override;

private:
    bool check_docker();
    core::OperationResult run_command(const std::string& site_dir, const std::string& command);

    logger::Logger& logger_;
    std::string sites_root_;
    bool docker_checked_ = false;
    bool docker_available_ = false;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_DOCKER_RUNTIME_H
