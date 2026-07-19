#include "WordPressConfigService.h"

#include "config/Config.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

namespace containercp::wordpress {
namespace {

namespace fs = std::filesystem;

bool path_has_prefix(const fs::path& path, const fs::path& root) {
    auto path_it = path.begin();
    auto root_it = root.begin();
    for (; root_it != root.end(); ++root_it, ++path_it) {
        if (path_it == path.end() || *path_it != *root_it) {
            return false;
        }
    }
    return true;
}

std::string read_text_file(const fs::path& path, bool& ok) {
    std::ifstream in(path);
    if (!in.is_open()) {
        ok = false;
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    ok = true;
    return buffer.str();
}

std::vector<fs::path> candidate_document_roots(const fs::path& site_root) {
    return {
        site_root / "public",
        site_root / "public_html",
        site_root / "htdocs",
        site_root / "www",
        site_root / "root",
        site_root,
    };
}

bool has_unsafe_permissions(const fs::path& path) {
    std::error_code ec;
    const auto permissions = fs::symlink_status(path, ec).permissions();
    if (ec) {
        return false;
    }
    using fs::perms;
    return (permissions & (perms::group_read | perms::group_write | perms::group_exec |
                           perms::others_read | perms::others_write | perms::others_exec)) != perms::none;
}

void add_permission_warning(WordPressConfigInspection& inspection) {
    inspection.issues.push_back({WordPressConfigIssueSeverity::Warning,
                                 "unsafe_permissions",
                                 "WordPress config file is accessible by group or other users"});
}

std::string container_document_root_for_site(const site::Site& site_record) {
    if (site_record.web_server == "apache") {
        return "/usr/local/apache2/htdocs";
    }
    return "/var/www/html";
}

} // namespace

WordPressConfigService::WordPressConfigService(site::SiteManager& sites)
    : WordPressConfigService(sites, config::Config::instance().sites_dir()) {
}

WordPressConfigService::WordPressConfigService(site::SiteManager& sites, std::filesystem::path sites_root)
    : sites_(sites)
    , sites_root_(std::move(sites_root)) {
}

WordPressConfigServiceResult WordPressConfigService::failure(uint64_t site_id,
                                                             std::string domain,
                                                             WordPressCredentialStatus status,
                                                             std::string code,
                                                             std::string message) const {
    WordPressConfigServiceResult result;
    result.ok = false;
    result.status = status;
    result.code = std::move(code);
    result.message = std::move(message);
    result.site_id = site_id;
    result.domain = std::move(domain);
    return result;
}

WordPressConfigServiceResult WordPressConfigService::inspect_site(uint64_t site_id) const {
    if (site_id == 0) {
        return failure(0, {}, WordPressCredentialStatus::Unsupported, "system_site_unsupported",
                       "The system admin-panel site does not have WordPress credentials");
    }
    const auto* site_record = sites_.find_by_id(site_id);
    if (site_record == nullptr) {
        return failure(site_id, {}, WordPressCredentialStatus::Error, "site_not_found", "Site was not found");
    }
    return inspect(*site_record);
}

WordPressConfigServiceResult WordPressConfigService::inspect_domain(const std::string& domain) const {
    const auto* site_record = sites_.find(domain);
    if (site_record == nullptr) {
        return failure(0, domain, WordPressCredentialStatus::Error, "site_not_found", "Site was not found");
    }
    if (site_record->id == 0) {
        return failure(0, site_record->domain, WordPressCredentialStatus::Unsupported, "system_site_unsupported",
                       "The system admin-panel site does not have WordPress credentials");
    }
    return inspect(*site_record);
}

WordPressConfigServiceResult WordPressConfigService::inspect(const site::Site& site_record) const {
    std::error_code ec;
    const fs::path sites_root_abs = fs::absolute(sites_root_, ec).lexically_normal();
    if (ec || sites_root_.empty()) {
        return failure(site_record.id, site_record.domain, WordPressCredentialStatus::UnsafePath, "sites_root_invalid",
                       "Sites root could not be resolved");
    }

    const fs::path site_root = fs::absolute(sites_root_abs / site_record.domain, ec).lexically_normal();
    if (ec || !path_has_prefix(site_root, sites_root_abs)) {
        return failure(site_record.id, site_record.domain, WordPressCredentialStatus::UnsafePath, "site_root_escape",
                       "Resolved site root escapes the configured sites directory");
    }

    const fs::file_status root_status = fs::symlink_status(site_root, ec);
    if (ec || !fs::exists(root_status)) {
        auto result = failure(site_record.id, site_record.domain, WordPressCredentialStatus::ConfigMissing,
                              "site_root_missing", "Site root does not exist");
        result.site_root = site_root;
        return result;
    }
    if (fs::is_symlink(root_status) || !fs::is_directory(root_status)) {
        auto result = failure(site_record.id, site_record.domain, WordPressCredentialStatus::UnsafePath,
                              "site_root_unsafe", "Site root is not a safe directory");
        result.site_root = site_root;
        return result;
    }

    std::set<fs::path> seen;
    WordPressConfigPathSafety last_missing;
    for (const auto& document_root : candidate_document_roots(site_root)) {
        const fs::path config_path = document_root / "wp-config.php";
        const fs::path normalized = fs::absolute(config_path, ec).lexically_normal();
        if (ec || seen.count(normalized) != 0) {
            continue;
        }
        seen.insert(normalized);

        auto safety = detector_.inspect_config_path(site_root, normalized);
        if (!safety.safe) {
            if (safety.status == WordPressCredentialStatus::ConfigMissing) {
                last_missing = safety;
                continue;
            }
            auto result = failure(site_record.id, site_record.domain, safety.status, safety.code, safety.message);
            result.site_root = safety.site_root;
            result.document_root = document_root;
            result.config_path = safety.config_path;
            return result;
        }

        bool read_ok = false;
        const std::string content = read_text_file(safety.config_path, read_ok);
        if (!read_ok) {
            auto result = failure(site_record.id, site_record.domain, WordPressCredentialStatus::Error,
                                  "config_read_failed", "WordPress config file could not be read");
            result.site_root = safety.site_root;
            result.document_root = document_root;
            result.config_path = safety.config_path;
            return result;
        }

        WordPressConfigServiceResult result;
        result.ok = true;
        result.status = WordPressCredentialStatus::Complete;
        result.code = "ok";
        result.message = "WordPress config inspected";
        result.site_id = site_record.id;
        result.domain = site_record.domain;
        result.site_root = safety.site_root;
        result.document_root = document_root;
        result.config_path = safety.config_path;
        result.container_document_root = container_document_root_for_site(site_record);
        result.inspection = detector_.inspect_content(content);
        if (has_unsafe_permissions(safety.config_path)) {
            add_permission_warning(result.inspection);
        }
        result.status = result.inspection.status;
        result.ok = result.inspection.status == WordPressCredentialStatus::Complete;
        if (!result.ok) {
            result.code = credential_status_to_string(result.inspection.status);
            result.message = "WordPress config credentials are not fully supported";
        }
        return result;
    }

    auto result = failure(site_record.id, site_record.domain, WordPressCredentialStatus::ConfigMissing,
                          "config_missing", "No active wp-config.php file was found");
    result.site_root = site_root;
    if (!last_missing.config_path.empty()) {
        result.config_path = last_missing.config_path;
    }
    return result;
}

WordPressConfigPublicView WordPressConfigService::public_view(const WordPressConfigServiceResult& result) const {
    WordPressConfigPublicView view;
    view.available = result.ok;
    view.site_id = result.site_id;
    view.domain = result.domain;
    view.status = credential_status_to_string(result.status);
    view.source = credential_source_to_string(result.inspection.source);
    view.mutability = credential_mutability_to_string(result.inspection.mutability);

    const auto safe_inspection = result.inspection.public_safe();
    view.db_name = safe_inspection.credentials.db_name.public_display_value();
    view.db_user = safe_inspection.credentials.db_user.public_display_value();
    view.db_host = safe_inspection.credentials.db_host.public_display_value();
    view.db_password_present = safe_inspection.credentials.db_password.state == WordPressCredentialValueState::Redacted;
    view.issues = safe_inspection.issues;

    if (!result.ok && view.issues.empty() && !result.code.empty()) {
        view.issues.push_back({WordPressConfigIssueSeverity::Error, result.code, result.message});
    }
    return view;
}

WordPressRuntimeVerificationRequest WordPressConfigService::runtime_verification_request(const WordPressConfigServiceResult& result) const {
    WordPressRuntimeVerificationRequest request;
    request.compose_dir = result.site_root;
    request.document_root = result.document_root;
    request.config_path = result.config_path;
    if (!result.container_document_root.empty()) {
        request.container_document_root = result.container_document_root;
    }
    return request;
}

} // namespace containercp::wordpress
