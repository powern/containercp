#ifndef CONTAINERCP_DATABASE_DATABASE_VIEW_SERVICE_H
#define CONTAINERCP_DATABASE_DATABASE_VIEW_SERVICE_H

#include "database/DatabaseManager.h"
#include "database/MariaDBCredentialProvider.h"
#include "logger/Logger.h"
#include "runtime/SiteRuntimeManager.h"
#include "site/SiteManager.h"
#include "wordpress/WordPressConfigService.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace containercp::database {

struct DatabaseView {
    uint64_t database_id = 0;
    uint64_t site_id = 0;
    std::string domain;
    std::string database_name;
    std::string database_user;
    std::string engine;
    std::string engine_version;
    std::string runtime_status = "Unknown";
    std::string connection_status = "not_checked";
    std::string credential_state = "unknown";
    std::string ownership_state = "imported";
    std::string imported_state = "unknown";
    std::string created_at;
    std::string updated_at;
    bool enabled = false;
};

struct DatabaseViewCredential {
    bool available = false;
    std::string state = "unknown";
    std::string source = "unknown";
    std::string code;
    std::string db_name;
    std::string db_user;
    std::string db_host;
    std::string password;
};

struct DatabaseConnectionCheck {
    bool attempted = false;
    bool success = false;
    std::string status = "not_checked";
    std::string code;
};

class DatabaseViewService {
public:
    using RuntimeStatusLookup = std::function<runtime::ContainerStatus(const site::Site&)>;
    using CredentialLookup = std::function<DatabaseViewCredential(const Database&, const site::Site*)>;
    using ConnectionVerifier = std::function<DatabaseConnectionCheck(const Database&, const site::Site&, const DatabaseViewCredential&)>;

    DatabaseViewService(logger::Logger& logger,
                        DatabaseManager& databases,
                        site::SiteManager& sites,
                        runtime::SiteRuntimeManager& site_runtime,
                        wordpress::WordPressConfigService& wordpress_config,
                        const MariaDBCredentialProvider& mariadb_provider,
                        std::filesystem::path sites_root);

    DatabaseViewService(logger::Logger& logger,
                        DatabaseManager& databases,
                        site::SiteManager& sites,
                        RuntimeStatusLookup runtime_lookup,
                        CredentialLookup credential_lookup,
                        ConnectionVerifier connection_verifier);

    DatabaseView build_view(const Database& database) const;
    std::string build_enriched_json() const;
    std::string build_enriched_json(uint64_t database_id) const;

private:
    static std::string normalize_runtime_status(const std::string& status);
    static std::string view_to_json(const DatabaseView& view);

    logger::Logger& logger_;
    DatabaseManager& databases_;
    site::SiteManager& sites_;
    RuntimeStatusLookup runtime_lookup_;
    CredentialLookup credential_lookup_;
    ConnectionVerifier connection_verifier_;
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_VIEW_SERVICE_H
