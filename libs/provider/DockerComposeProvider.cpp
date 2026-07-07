#include "DockerComposeProvider.h"

namespace containercp::provider {

DockerComposeProvider::DockerComposeProvider(filesystem::Filesystem& fs, config::Config& cfg, runtime::Runtime& rt)
    : fs_(fs)
    , cfg_(cfg)
    , rt_(rt)
{
}

core::OperationResult DockerComposeProvider::create_site(site::Site& site) {
    std::string site_dir = cfg_.data_root() + "/sites/" + site.domain + "/";
    fs_.create_directory(site_dir);
    fs_.create_file(site_dir + "README.txt", "This site is managed by ContainerCP.\n");

    docker::ComposeGenerator gen(fs_, cfg_.config_root() + "/templates/");
    gen.generate(site.domain, site.owner, site_dir + "docker-compose.yml");

    return rt_.create_site_stack(site.domain);
}

core::OperationResult DockerComposeProvider::remove_site(site::Site& site) {
    return rt_.remove_site(site.domain);
}

core::OperationResult DockerComposeProvider::start_site(site::Site& site) {
    return rt_.start_site(site.domain);
}

core::OperationResult DockerComposeProvider::stop_site(site::Site& site) {
    return rt_.stop_site(site.domain);
}

core::OperationResult DockerComposeProvider::status(site::Site& site) {
    return rt_.status(site.domain);
}

} // namespace containercp::provider
