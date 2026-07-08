#include "SiteRemoveOperation.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace containercp::operations {

SiteRemoveOperation::SiteRemoveOperation(site::SiteManager& sites, domain::DomainManager& domains,
                                         database::DatabaseManager& databases, backup::BackupManager& backups,
                                         ssl::SslCertificateManager& ssl, mail::MailDomainManager& mail,
                                         proxy::ReverseProxyManager& proxies,
                                         proxy::ProxyProvider& proxy_provider,
                                         runtime::PortManager& port_manager,
                                         filesystem::Filesystem& fs, config::Config& cfg, runtime::Runtime& rt)
    : sites_(sites)
    , domains_(domains)
    , databases_(databases)
    , backups_(backups)
    , ssl_(ssl)
    , mail_(mail)
    , proxies_(proxies)
    , proxy_provider_(proxy_provider)
    , port_manager_(port_manager)
    , fs_(fs)
    , cfg_(cfg)
    , rt_(rt)
{
}

core::OperationResult SiteRemoveOperation::execute(const std::string& domain) {
    auto* site = sites_.find(domain);
    if (site == nullptr) {
        return {false, "Site not found: " + domain};
    }

    uint64_t site_id = site->id;

    // Read port from .env before removing
    uint16_t nginx_port = 0;
    {
        std::string env_path = cfg_.sites_dir() + domain + "/.env";
        std::ifstream env_file(env_path);
        if (env_file.is_open()) {
            std::string line;
            while (std::getline(env_file, line)) {
                if (line.find("NGINX_PORT=") == 0) {
                    try {
                        nginx_port = static_cast<uint16_t>(
                            std::stoul(line.substr(11)));
                    } catch (...) {
                    }
                    break;
                }
            }
        }
    }

    rt_.remove_site(domain);

    // Remove proxy config BEFORE removing directory (proxy_provider_ reads config from directory)
    proxy_provider_.remove_proxy(domain);

    fs_.remove_directory(cfg_.sites_dir() + domain + "/");

    auto* rp = proxies_.find_by_domain(domain);
    if (rp != nullptr) {
        proxies_.remove(rp->id);
    }

    std::vector<uint64_t> domain_ids;
    for (const auto& d : domains_.list()) {
        if (d.site_id == site_id) {
            domain_ids.push_back(d.id);
        }
    }
    for (auto did : domain_ids) {
        auto* d = domains_.find(did);
        if (d != nullptr) {
            auto* sc = ssl_.find_by_domain(d->fqdn);
            if (sc != nullptr) ssl_.remove(sc->id);
            auto* md = mail_.find_by_domain(d->fqdn);
            if (md != nullptr) mail_.remove(md->id);
            domains_.remove(did);
        }
    }

    for (const auto& d : databases_.list()) {
        if (d.site_id == site_id) {
            databases_.remove(d.id);
        }
    }

    // Backups are preserved after site removal — archive files and
    // records remain in /srv/containercp/backups/ for later inspection.

    // Release the nginx port
    if (nginx_port > 0) {
        port_manager_.release(nginx_port);
    }

    proxy_provider_.reload();

    sites_.remove(site_id);

    return {true, ""};
}

} // namespace containercp::operations
