#ifndef CONTAINERCP_DATABASE_DATABASE_LIFECYCLE_SERVICE_H
#define CONTAINERCP_DATABASE_DATABASE_LIFECYCLE_SERVICE_H

#include "database/DatabaseManager.h"
#include "database/DatabaseProvider.h"
#include "jobs/Job.h"
#include "runtime/SiteRuntimeManager.h"
#include "site/SiteManager.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace containercp::database {

struct DatabaseLifecycleResult {
    bool success = false;
    std::string code;
    std::string message;
    std::vector<jobs::JobStep> steps;
    jobs::JobFailureDiagnostics failure;
    bool manual_recovery_required = false;
};

struct DatabaseCreateManagedRequest {
    uint64_t site_id = 0;
    uint64_t database_id = 0;
    uint64_t job_id = 0;
};

struct DatabaseVerifyManagedRequest {
    uint64_t site_id = 0;
    uint64_t database_id = 0;
    uint64_t job_id = 0;
};

struct DatabaseDropManagedRequest {
    uint64_t site_id = 0;
    uint64_t database_id = 0;
    uint64_t job_id = 0;
    std::string confirmation;
    std::string expected_domain;
    std::string expected_database_name;
};

class DatabaseLifecycleService {
public:
    using PersistCallback = std::function<bool()>;
    using RuntimeDbStatusLookup = std::function<std::string(const site::Site&)>;

    DatabaseLifecycleService(site::SiteManager& sites,
                             DatabaseManager& databases,
                             runtime::SiteRuntimeManager& site_runtime,
                             const DatabaseProvider& provider,
                             std::filesystem::path sites_root,
                             PersistCallback persist);

    DatabaseLifecycleService(site::SiteManager& sites,
                             DatabaseManager& databases,
                             RuntimeDbStatusLookup runtime_lookup,
                             const DatabaseProvider& provider,
                             std::filesystem::path sites_root,
                             PersistCallback persist);

    DatabaseLifecycleResult createManagedDatabase(const DatabaseCreateManagedRequest& request);
    DatabaseLifecycleResult verifyManagedDatabase(const DatabaseVerifyManagedRequest& request);
    DatabaseLifecycleResult dropManagedDatabase(const DatabaseDropManagedRequest& request);

    bool can_drop(const Database& database) const;
    std::string drop_block_reason(const Database& database) const;

private:
    struct ResolvedTarget {
        site::Site* site_record = nullptr;
        Database* database = nullptr;
        MariaDBConnectionTarget target;
        DatabaseProviderCredential service_account;
    };

    DatabaseLifecycleResult resolve_target(uint64_t site_id,
                                           uint64_t database_id,
                                           bool require_managed,
                                           ResolvedTarget& resolved,
                                           const std::string& operation,
                                           uint64_t job_id) const;
    bool site_has_exactly_one_database(uint64_t site_id) const;
    DatabaseProviderCredential load_service_account(const site::Site& site_record) const;
    MariaDBConnectionTarget target_for_site(const site::Site& site_record) const;

    site::SiteManager& sites_;
    DatabaseManager& databases_;
    runtime::SiteRuntimeManager* site_runtime_ = nullptr;
    RuntimeDbStatusLookup runtime_lookup_;
    const DatabaseProvider& provider_;
    std::filesystem::path sites_root_;
    PersistCallback persist_;
};

bool database_drop_confirmation_valid(const std::string& confirmation,
                                      const std::string& database_name,
                                      const std::string& domain);

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_LIFECYCLE_SERVICE_H
