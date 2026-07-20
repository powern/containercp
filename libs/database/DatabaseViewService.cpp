#include "DatabaseViewService.h"

#include "api/JsonFormatter.h"

#include <sstream>
#include <utility>

namespace containercp::database {
namespace {

namespace fs = std::filesystem;

DatabaseViewCredential credential_from_wordpress(const Database& database,
                                                 const wordpress::WordPressDatabaseCredentialSecret& secret) {
    DatabaseViewCredential credential;
    credential.source = "wordpress_config";
    credential.code = secret.code;
    if (!secret.available) {
        if (secret.code == "config_missing" || secret.code == "site_root_missing" ||
            secret.code == "credentials_missing" || secret.code == "site_not_found") {
            credential.state = "missing";
        } else if (secret.code == "unsafe_path" || secret.code == "ambiguous" ||
                   secret.code == "unsupported" || secret.code == "wordpress_not_detected") {
            credential.state = "invalid";
        } else {
            credential.state = "unknown";
        }
        return credential;
    }

    if (secret.db_host != "mariadb" || secret.db_name != database.db_name || secret.db_user != database.db_user) {
        credential.state = "invalid";
        credential.code = "wordpress_metadata_mismatch";
        credential.db_name = secret.db_name;
        credential.db_user = secret.db_user;
        credential.db_host = secret.db_host;
        return credential;
    }

    credential.available = true;
    credential.state = "available";
    credential.code = "available";
    credential.db_name = secret.db_name;
    credential.db_user = secret.db_user;
    credential.db_host = secret.db_host;
    credential.password = secret.db_password;
    return credential;
}

} // namespace

DatabaseViewService::DatabaseViewService(logger::Logger& logger,
                                         DatabaseManager& databases,
                                         site::SiteManager& sites,
                                         runtime::SiteRuntimeManager& site_runtime,
                                         wordpress::WordPressConfigService& wordpress_config,
                                         const MariaDBCredentialProvider& mariadb_provider,
                                         std::filesystem::path sites_root)
    : DatabaseViewService(
          logger,
          databases,
          sites,
          [&site_runtime](const site::Site& site_record) {
              return site_runtime.get_status(site_record.id, site_record.domain).db;
          },
          [&wordpress_config](const Database& database, const site::Site*) {
              if (!database.db_password.empty()) {
                  DatabaseViewCredential credential;
                  credential.available = true;
                  credential.state = "available";
                  credential.source = "metadata";
                  credential.code = "available";
                  credential.db_name = database.db_name;
                  credential.db_user = database.db_user;
                  credential.db_host = "mariadb";
                  credential.password = database.db_password;
                  return credential;
              }
              return credential_from_wordpress(database,
                                               wordpress_config.database_credentials_for_verification(database.site_id));
          },
          [&mariadb_provider, sites_root = std::move(sites_root)](const Database& database,
                                                                  const site::Site& site_record,
                                                                  const DatabaseViewCredential& credential) {
              DatabaseConnectionCheck check;
              check.attempted = true;
              const fs::path compose_file = sites_root / site_record.domain / "docker-compose.yml";
              const auto result = mariadb_provider.verify_password(
                  {compose_file.string(), "mariadb"},
                  {database.db_user, "%"},
                  credential.password);
              check.success = result.success;
              check.status = result.success ? "verified" : "connection_failed";
              check.code = result.code;
              return check;
          }) {
}

DatabaseViewService::DatabaseViewService(logger::Logger& logger,
                                         DatabaseManager& databases,
                                         site::SiteManager& sites,
                                         RuntimeStatusLookup runtime_lookup,
                                         CredentialLookup credential_lookup,
                                         ConnectionVerifier connection_verifier)
    : logger_(logger)
    , databases_(databases)
    , sites_(sites)
    , runtime_lookup_(std::move(runtime_lookup))
    , credential_lookup_(std::move(credential_lookup))
    , connection_verifier_(std::move(connection_verifier)) {
}

std::string DatabaseViewService::normalize_runtime_status(const std::string& status) {
    if (status == "Running") {
        return "Running";
    }
    if (status == "Stopped") {
        return "Stopped";
    }
    return "Unknown";
}

DatabaseView DatabaseViewService::build_view(const Database& database) const {
    DatabaseView view;
    view.database_id = database.id;
    view.site_id = database.site_id;
    view.database_name = database.db_name;
    view.database_user = database.db_user;
    view.engine = database.engine;
    view.engine_version = database.version;
    view.enabled = database.enabled;

    const auto* site_record = sites_.find_by_id(database.site_id);
    if (site_record == nullptr) {
        view.runtime_status = "Unknown";
        view.connection_status = "not_checked";
        view.credential_state = "unknown";
        view.ownership_state = "imported";
        view.imported_state = "site_missing";
        return view;
    }

    view.domain = site_record->domain;
    view.runtime_status = normalize_runtime_status(runtime_lookup_(*site_record).status);

    int site_database_count = 0;
    for (const auto& candidate : databases_.list()) {
        if (candidate.site_id == database.site_id && candidate.enabled) {
            ++site_database_count;
        }
    }

    const auto credential = credential_lookup_(database, site_record);
    view.credential_state = credential.state;
    view.ownership_state = (database.db_password.empty() || credential.source == "wordpress_config") ? "imported" : "managed";
    view.imported_state = view.ownership_state == "imported" ? "detected" : "none";
    if (site_database_count != 1) {
        view.imported_state = "metadata_conflict";
    } else if (!credential.available && view.ownership_state == "imported") {
        view.imported_state = "credential_unavailable";
    }

    if (view.runtime_status != "Running" || !credential.available) {
        view.connection_status = "not_checked";
        return view;
    }

    const auto connection = connection_verifier_(database, *site_record, credential);
    view.connection_status = connection.status.empty() ? (connection.success ? "verified" : "connection_failed")
                                                       : connection.status;
    return view;
}

std::string DatabaseViewService::view_to_json(const DatabaseView& view) {
    std::ostringstream json;
    json << "{\"database_id\":" << view.database_id
         << ",\"id\":" << view.database_id
         << ",\"site_id\":" << view.site_id
         << ",\"domain\":\"" << api::JsonFormatter::escape(view.domain)
         << "\",\"database_name\":\"" << api::JsonFormatter::escape(view.database_name)
         << "\",\"database_user\":\"" << api::JsonFormatter::escape(view.database_user)
         << "\",\"engine\":\"" << api::JsonFormatter::escape(view.engine)
         << "\",\"engine_version\":\"" << api::JsonFormatter::escape(view.engine_version)
         << "\",\"runtime_status\":\"" << api::JsonFormatter::escape(view.runtime_status)
         << "\",\"connection_status\":\"" << api::JsonFormatter::escape(view.connection_status)
         << "\",\"credential_state\":\"" << api::JsonFormatter::escape(view.credential_state)
         << "\",\"ownership_state\":\"" << api::JsonFormatter::escape(view.ownership_state)
         << "\",\"imported_state\":\"" << api::JsonFormatter::escape(view.imported_state)
         << "\",\"created_at\":\"" << api::JsonFormatter::escape(view.created_at)
         << "\",\"updated_at\":\"" << api::JsonFormatter::escape(view.updated_at)
         << "\",\"enabled\":" << (view.enabled ? "true" : "false")
         << "}";
    return json.str();
}

std::string DatabaseViewService::build_enriched_json() const {
    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& database : databases_.list()) {
        if (!first) {
            json << ",";
        }
        first = false;
        json << view_to_json(build_view(database));
    }
    json << "]";
    logger_.info("DATABASE_VIEW", "Built enriched database inventory");
    return json.str();
}

std::string DatabaseViewService::build_enriched_json(uint64_t database_id) const {
    const auto* database = databases_.find(database_id);
    if (database == nullptr) {
        return "null";
    }
    return view_to_json(build_view(*database));
}

} // namespace containercp::database
