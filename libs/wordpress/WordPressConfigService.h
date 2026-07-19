#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_SERVICE_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_SERVICE_H

#include "site/SiteManager.h"
#include "wordpress/WordPressConfigDetector.h"
#include "wordpress/WordPressRuntimeVerifier.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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
    std::string container_document_root;
    WordPressConfigInspection inspection;
};

struct WordPressConfigPublicView {
    bool available = false;
    uint64_t site_id = 0;
    std::string domain;
    std::string status;
    std::string source;
    std::string mutability;
    std::string db_name;
    std::string db_user;
    std::string db_host;
    bool db_password_present = false;
    std::vector<WordPressConfigIssue> issues;
};

class WordPressConfigService {
public:
    explicit WordPressConfigService(site::SiteManager& sites);
    WordPressConfigService(site::SiteManager& sites, std::filesystem::path sites_root);

    WordPressConfigServiceResult inspect_site(uint64_t site_id) const;
    WordPressConfigServiceResult inspect_domain(const std::string& domain) const;

    WordPressConfigPublicView public_view(const WordPressConfigServiceResult& result) const;
    WordPressRuntimeVerificationRequest runtime_verification_request(const WordPressConfigServiceResult& result) const;

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
