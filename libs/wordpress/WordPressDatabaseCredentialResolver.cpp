#include "wordpress/WordPressDatabaseCredentialResolver.h"

#include <utility>
#include <vector>

namespace containercp::wordpress {
namespace {

WordPressDatabaseCredentialTarget target_unavailable(std::string status, std::string message) {
    WordPressDatabaseCredentialTarget target;
    target.available = false;
    target.status = std::move(status);
    target.message = std::move(message);
    return target;
}

} // namespace

WordPressDatabaseCredentialResolver::WordPressDatabaseCredentialResolver(WordPressConfigService& wordpress_config,
                                                                        const database::DatabaseManager& databases)
    : wordpress_config_(wordpress_config)
    , databases_(databases) {
}

WordPressDatabaseCredentialStatus WordPressDatabaseCredentialResolver::resolve_site(uint64_t site_id) const {
    WordPressDatabaseCredentialStatus status;
    status.inspection = wordpress_config_.inspect_site(site_id);
    status.view = wordpress_config_.public_view(status.inspection);
    status.target = resolve_target(status.inspection);
    return status;
}

WordPressDatabaseCredentialTarget WordPressDatabaseCredentialResolver::resolve_target(const WordPressConfigServiceResult& inspection) const {
    if (!inspection.ok) {
        return target_unavailable(inspection.code.empty() ? "wordpress_unavailable" : inspection.code,
                                  "WordPress credential target is unavailable");
    }

    const auto& credentials = inspection.inspection.credentials;
    WordPressDatabaseCredentialTarget target;
    target.db_name = credentials.db_name.value;
    target.db_user = credentials.db_user.value;
    target.db_host = credentials.db_host.value;

    if (target.db_name.empty() || target.db_user.empty() || target.db_host.empty()) {
        return target_unavailable("wordpress_metadata_incomplete", "WordPress database metadata is incomplete");
    }
    if (target.db_host != "mariadb") {
        target.status = "database_host_unsupported";
        target.message = "WordPress database host is not managed by the site MariaDB service";
        return target;
    }

    std::vector<uint64_t> matches;
    for (const auto& db : databases_.list()) {
        if (db.enabled && db.site_id == inspection.site_id &&
            db.db_name == target.db_name && db.db_user == target.db_user) {
            matches.push_back(db.id);
        }
    }

    if (matches.empty()) {
        target.status = "database_target_missing";
        target.message = "No database record matches the WordPress credential target";
        return target;
    }
    if (matches.size() > 1) {
        target.status = "database_target_ambiguous";
        target.message = "Multiple database records match the WordPress credential target";
        return target;
    }

    target.available = true;
    target.database_id = matches.front();
    target.status = "resolved";
    target.message = "WordPress database credential target resolved";
    return target;
}

} // namespace containercp::wordpress
