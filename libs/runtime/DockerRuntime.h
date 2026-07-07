#ifndef CONTAINERCP_RUNTIME_DOCKER_RUNTIME_H
#define CONTAINERCP_RUNTIME_DOCKER_RUNTIME_H

#include "runtime/Runtime.h"
#include "logger/Logger.h"

namespace containercp::runtime {

class DockerRuntime : public Runtime {
public:
    explicit DockerRuntime(logger::Logger& logger);

    void create_site_stack(const std::string& domain) override;
    void start_site(const std::string& domain) override;
    void stop_site(const std::string& domain) override;
    void remove_site(const std::string& domain) override;
    void status(const std::string& domain) override;

private:
    logger::Logger& logger_;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_DOCKER_RUNTIME_H
