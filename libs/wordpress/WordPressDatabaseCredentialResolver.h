#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_DATABASE_CREDENTIAL_RESOLVER_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_DATABASE_CREDENTIAL_RESOLVER_H

#include "database/DatabaseManager.h"
#include "wordpress/WordPressConfigService.h"

#include <cstdint>
#include <string>

namespace containercp::wordpress {

struct WordPressDatabaseCredentialTarget {
    bool available = false;
    uint64_t database_id = 0;
    std::string status = "unknown";
    std::string message;
    std::string db_name;
    std::string db_user;
    std::string db_host;
};

struct WordPressDatabaseCredentialStatus {
    WordPressConfigServiceResult inspection;
    WordPressConfigPublicView view;
    WordPressDatabaseCredentialTarget target;
};

class WordPressDatabaseCredentialResolver {
public:
    WordPressDatabaseCredentialResolver(WordPressConfigService& wordpress_config,
                                        const database::DatabaseManager& databases);

    WordPressDatabaseCredentialStatus resolve_site(uint64_t site_id) const;
    WordPressDatabaseCredentialTarget resolve_target(const WordPressConfigServiceResult& inspection) const;

private:
    WordPressConfigService& wordpress_config_;
    const database::DatabaseManager& databases_;
};

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_DATABASE_CREDENTIAL_RESOLVER_H
