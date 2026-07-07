#include "SiteRemoveOperation.h"

#include <vector>

namespace containercp::operations {

SiteRemoveOperation::SiteRemoveOperation(site::SiteManager& sites, domain::DomainManager& domains,
                                         database::DatabaseManager& databases, backup::BackupManager& backups,
                                         ssl::SslCertificateManager& ssl, mail::MailDomainManager& mail,
                                         proxy::ReverseProxyManager& proxies,
                                         filesystem::Filesystem& fs, config::Config& cfg, runtime::Runtime& rt)
    : sites_(sites)
    , domains_(domains)
    , databases_(databases)
    , backups_(backups)
    , ssl_(ssl)
    , mail_(mail)
    , proxies_(proxies)
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

    rt_.remove_site(domain);

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

    for (const auto& b : backups_.list()) {
        if (b.site_id == site_id) {
            backups_.remove(b.id);
        }
    }

    sites_.remove(site_id);

    return {true, ""};
}

} // namespace containercp::operations
