#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_SERVICE_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_SERVICE_H

#include "site/SiteManager.h"
#include "wordpress/WordPressConfigDetector.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace containercp::wordpress {

struct WordPressConfigServiceResult {
    bool ok = false;
    WordPressCredentialStatus status = WordPressCredentialStatus::Unknown;
    std::string code;
    std::string message;
    uint64_t site_id = 0;
    std::string domain;
    std::filesystem::path site_root;
    std::filesystem::path document_root;
    std::filesystem::path config_path;
    WordPressConfigInspection inspection;
};

class WordPressConfigService {
public:
    explicit WordPressConfigService(site::SiteManager& sites);
    WordPressConfigService(site::SiteManager& sites, std::filesystem::path sites_root);

    WordPressConfigServiceResult inspect_site(uint64_t site_id) const;
    WordPressConfigServiceResult inspect_domain(const std::string& domain) const;

private:
    WordPressConfigServiceResult inspect(const site::Site& site_record) const;
    WordPressConfigServiceResult failure(uint64_t site_id,
                                         std::string domain,
                                         WordPressCredentialStatus status,
                                         std::string code,
                                         std::string message) const;

    site::SiteManager& sites_;
    std::filesystem::path sites_root_;
    WordPressConfigDetector detector_;
};

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_SERVICE_H
