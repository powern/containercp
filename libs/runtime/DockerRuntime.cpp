#include "DockerRuntime.h"

namespace containercp::runtime {

DockerRuntime::DockerRuntime(logger::Logger& logger)
    : logger_(logger)
{
}

void DockerRuntime::create_site_stack(const std::string& domain) {
    logger_.info("Creating stack for " + domain);
}

void DockerRuntime::start_site(const std::string& domain) {
    logger_.info("Starting stack for " + domain);
}

void DockerRuntime::stop_site(const std::string& domain) {
    logger_.info("Stopping stack for " + domain);
}

void DockerRuntime::remove_site(const std::string& domain) {
    logger_.info("Removing stack for " + domain);
}

void DockerRuntime::status(const std::string& domain) {
    logger_.info("Status for " + domain);
}

} // namespace containercp::runtime
