#ifndef CONTAINERCP_SITE_SITE_MANAGER_H
#define CONTAINERCP_SITE_SITE_MANAGER_H

#include "site/Site.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::site {

class SiteManager {
public:
    uint64_t create(const std::string& domain, const std::string& owner, uint64_t node_id,
                    const std::string& web_server = "");
    bool remove(uint64_t id);
    Site* find(const std::string& domain);
    const std::vector<Site>& list() const;

    void set_sites(const std::vector<Site>& sites);

private:
    std::vector<Site> sites_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::site

#endif // CONTAINERCP_SITE_SITE_MANAGER_H
