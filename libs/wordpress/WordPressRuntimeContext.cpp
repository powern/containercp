#include "wordpress/WordPressRuntimeContext.h"

#include "utils/PathUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace containercp::wordpress {
namespace {

std::vector<std::string> split_pipe(const std::string& value) {
    std::vector<std::string> fields;
    std::istringstream stream(value);
    std::string field;
    while (std::getline(stream, field, '|')) {
        while (!field.empty() && (field.back() == '\r' || field.back() == '\n')) field.pop_back();
        fields.push_back(field);
    }
    return fields;
}

std::string trim_line(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

bool is_sha256_image_id(const std::string& value) {
    if (value.size() <= 7 || value.compare(0, 7, "sha256:") != 0) return false;
    return std::all_of(value.begin() + 7, value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

bool is_forbidden_network(const std::string& name) {
    return name == "containercp-public" || name == "bridge" || name == "host" ||
           name == "none" || name.rfind("containercp-mail", 0) == 0;
}

bool canonical_equals(const std::filesystem::path& left,
                      const std::filesystem::path& right) {
    std::error_code left_ec;
    std::error_code right_ec;
    const auto left_canonical = std::filesystem::weakly_canonical(left, left_ec);
    const auto right_canonical = std::filesystem::weakly_canonical(right, right_ec);
    return !left_ec && !right_ec && left_canonical == right_canonical;
}

bool parse_non_root_id(const std::string& token, int64_t& value) {
    if (token.empty() || !std::all_of(token.begin(), token.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        })) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoll(token, &consumed);
        if (consumed != token.size() || parsed <= 0) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

WordPressRuntimeContextResolver::WordPressRuntimeContextResolver(
    runtime::CommandExecutor& executor,
    site::SiteManager& sites,
    WordPressConfigService& config_service,
    config::Config& config,
    logger::Logger& logger)
    : executor_(executor)
    , sites_(sites)
    , config_service_(config_service)
    , config_(config)
    , logger_(logger) {
}

WordPressRuntimeContext WordPressRuntimeContextResolver::failure(
    uint64_t site_id,
    std::string domain,
    std::string code,
    std::string message) const {
    WordPressRuntimeContext context;
    context.site_id = site_id;
    context.domain = std::move(domain);
    context.failure_code = std::move(code);
    context.message = std::move(message);
    return context;
}

WordPressRuntimeContext WordPressRuntimeContextResolver::resolve(uint64_t site_id) const {
    const auto* site = sites_.find_by_id(site_id);
    if (site == nullptr) {
        return failure(site_id, {}, "site_not_found", "Managed site was not found");
    }
    if (site->id == 0 || site->node_id == 0 || site->owner == "system") {
        return failure(site_id, site->domain, "system_site_rejected", "System sites cannot run WordPress operations");
    }

    const auto config_result = config_service_.inspect_site(site_id);
    if (config_result.site_root.empty() || config_result.document_root.empty() ||
        config_result.config_path.empty()) {
        const std::string code = config_result.code.empty()
            ? "wordpress_paths_unavailable" : config_result.code;
        return failure(site_id, site->domain, code, "Canonical WordPress paths could not be resolved");
    }

    WordPressRuntimeContext context;
    context.site_id = site_id;
    context.domain = site->domain;
    context.site_root = std::filesystem::weakly_canonical(config_result.site_root);
    context.document_root = std::filesystem::weakly_canonical(config_result.document_root);
    context.config_path = std::filesystem::weakly_canonical(config_result.config_path);
    context.compose_file = context.site_root / "docker-compose.yml";
    context.php_service = "php";

    std::error_code ec;
    const auto sites_root = std::filesystem::weakly_canonical(config_.sites_dir(), ec);
    if (ec || !utils::path_has_prefix(context.site_root, sites_root) ||
        !utils::path_has_prefix(context.document_root, context.site_root) ||
        !utils::path_has_prefix(context.config_path, context.site_root)) {
        return failure(site_id, site->domain, "wordpress_path_escape", "WordPress paths are outside the managed site root");
    }

    const auto site_status = std::filesystem::symlink_status(context.site_root, ec);
    const auto document_status = std::filesystem::symlink_status(context.document_root, ec);
    const auto compose_status = std::filesystem::symlink_status(context.compose_file, ec);
    if (ec || std::filesystem::is_symlink(site_status) || std::filesystem::is_symlink(document_status) ||
        std::filesystem::is_symlink(compose_status) || !std::filesystem::is_directory(site_status) ||
        !std::filesystem::is_directory(document_status) || !std::filesystem::is_regular_file(compose_status)) {
        return failure(site_id, site->domain, "wordpress_path_unsafe", "Managed WordPress paths are unsafe");
    }

    if (!resolve_php_container(context, context) ||
        !verify_image_identity(context) ||
        !resolve_private_network(context) ||
        !resolve_document_root_mount(context) ||
        !resolve_php_fpm_identity(context)) {
        logger_.error("WORDPRESS", "Runtime context resolution failed: " + context.failure_code);
        return context;
    }

    context.ok = true;
    context.runtime_capable = true;
    context.read_only_capable = true;
    context.mutation_capable = context.document_root_mount_read_write &&
                               context.php_fpm_uid > 0 && context.php_fpm_gid > 0;
    context.failure_code = "ok";
    context.message = "Managed WordPress runtime context resolved";
    return context;
}

bool WordPressRuntimeContextResolver::resolve_php_container(
    const WordPressRuntimeContext& base,
    WordPressRuntimeContext& context) const {
    const auto listed = executor_.run({"docker", "compose", "-f", base.compose_file.string(),
                                       "ps", "--all", "--format", "{{.Name}}", "php"});
    if (listed.exit_code != 0) {
        context.failure_code = "wordpress_php_service_unavailable";
        context.message = "Managed PHP service could not be enumerated";
        return false;
    }

    std::vector<std::string> candidates;
    std::istringstream lines(listed.out);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim_line(line);
        if (!line.empty() && std::find(candidates.begin(), candidates.end(), line) == candidates.end()) {
            candidates.push_back(line);
        }
    }
    if (candidates.size() != 1) {
        context.failure_code = "wordpress_php_service_ambiguous";
        context.message = "Exactly one managed running PHP service is required";
        return false;
    }

    const std::string inspect_format =
        "{{index .Config.Labels \"containercp.site.id\"}}|"
        "{{index .Config.Labels \"com.docker.compose.service\"}}|"
        "{{index .Config.Labels \"com.docker.compose.project\"}}|"
        "{{index .Config.Labels \"com.docker.compose.project.working_dir\"}}|"
        "{{.State.Status}}|{{.Config.Image}}|{{.Image}}";
    const auto inspected = executor_.run({"docker", "inspect", candidates.front(),
                                          "--format", inspect_format});
    const auto fields = split_pipe(inspected.out);
    if (inspected.exit_code != 0 || fields.size() != 7 ||
        fields[0] != std::to_string(base.site_id) || fields[1] != "php" ||
        fields[3].empty() || !canonical_equals(fields[3], base.site_root) ||
        fields[4] != "running") {
        context.failure_code = "wordpress_php_identity_unproven";
        context.message = "Managed PHP container identity could not be proven";
        return false;
    }
    if (fields[2].empty() || fields[5].empty() || !is_sha256_image_id(fields[6])) {
        context.failure_code = "wordpress_php_image_unproven";
        context.message = "Configured and immutable PHP image identity is required";
        return false;
    }

    context.php_container = candidates.front();
    context.compose_project = fields[2];
    context.configured_php_image = fields[5];
    context.immutable_php_image_id = fields[6];
    return true;
}

bool WordPressRuntimeContextResolver::verify_image_identity(WordPressRuntimeContext& context) const {
    const auto image = executor_.run({"docker", "image", "inspect", context.immutable_php_image_id,
                                      "--format", "{{.Id}}"});
    if (image.exit_code != 0 || trim_line(image.out) != context.immutable_php_image_id) {
        context.failure_code = "wordpress_php_image_mismatch";
        context.message = "Running PHP image ID did not match the inspected immutable image";
        return false;
    }
    return true;
}

bool WordPressRuntimeContextResolver::resolve_private_network(WordPressRuntimeContext& context) const {
    const auto networks = executor_.run({"docker", "inspect", context.php_container,
        "--format", "{{range $name, $network := .NetworkSettings.Networks}}{{$name}}|{{$network.NetworkID}}\n{{end}}"});
    if (networks.exit_code != 0) {
        context.failure_code = "wordpress_network_unavailable";
        context.message = "Managed PHP networks could not be inspected";
        return false;
    }

    std::size_t selected = 0;
    std::istringstream lines(networks.out);
    std::string line;
    while (std::getline(lines, line)) {
        const auto fields = split_pipe(line);
        if (fields.size() != 2 || fields[0].empty() || fields[1].empty()) continue;
        const std::string& name = fields[0];
        if (name == "containercp-public" || name == "bridge" || name == "host" || name == "none") {
            context.failure_code = "wordpress_public_network_rejected";
            context.message = "Managed PHP must not join a public or default network";
            return false;
        }
        if (name.rfind("containercp-mail", 0) == 0) continue;

        const auto inspected = executor_.run({"docker", "network", "inspect", name,
            "--format", "{{.Name}}|{{index .Labels \"com.docker.compose.project\"}}|{{index .Labels \"com.docker.compose.network\"}}"});
        const auto network_fields = split_pipe(inspected.out);
        if (inspected.exit_code != 0 || network_fields.size() != 3 ||
            network_fields[1] != context.compose_project ||
            network_fields[2].empty() || is_forbidden_network(network_fields[2])) {
            context.failure_code = "wordpress_unrelated_network_rejected";
            context.message = "Managed PHP joined an unrelated network";
            return false;
        }
        context.private_network = name;
        context.private_network_id = fields[1];
        ++selected;
    }

    if (selected != 1) {
        context.failure_code = "wordpress_private_network_unproven";
        context.message = "Exactly one selected-site private network is required";
        return false;
    }
    return true;
}

bool WordPressRuntimeContextResolver::resolve_document_root_mount(WordPressRuntimeContext& context) const {
    const auto mounts = executor_.run({"docker", "inspect", context.php_container,
        "--format", "{{range .Mounts}}{{.Source}}|{{.Destination}}|{{.RW}}\n{{end}}"});
    if (mounts.exit_code != 0) {
        context.failure_code = "wordpress_document_root_mount_unavailable";
        context.message = "Managed PHP mounts could not be inspected";
        return false;
    }

    std::error_code ec;
    const auto sites_root = std::filesystem::weakly_canonical(config_.sites_dir(), ec);
    if (ec) {
        context.failure_code = "wordpress_sites_root_unavailable";
        context.message = "Managed sites root could not be resolved";
        return false;
    }

    std::size_t matching = 0;
    std::istringstream lines(mounts.out);
    std::string line;
    while (std::getline(lines, line)) {
        const auto fields = split_pipe(line);
        if (fields.size() != 3 || fields[0].empty()) continue;
        std::error_code source_ec;
        const auto source_status = std::filesystem::symlink_status(fields[0], source_ec);
        const auto source = std::filesystem::weakly_canonical(fields[0], source_ec);
        if (source_ec || std::filesystem::is_symlink(source_status)) {
            context.failure_code = "wordpress_mount_path_unsafe";
            context.message = "Managed PHP mount source is unsafe";
            return false;
        }
        if (utils::path_has_prefix(source, sites_root) &&
            !utils::path_has_prefix(source, context.site_root)) {
            context.failure_code = "wordpress_cross_site_mount_rejected";
            context.message = "Managed PHP mount references another site";
            return false;
        }
        if (source != context.document_root) continue;
        if (fields[1].empty() || fields[1].front() != '/' ||
            (fields[2] != "true" && fields[2] != "false")) {
            context.failure_code = "wordpress_document_root_mount_invalid";
            context.message = "Managed document-root mount is invalid";
            return false;
        }
        context.container_document_root = fields[1];
        context.document_root_mount_read_write = fields[2] == "true";
        ++matching;
    }
    if (matching != 1) {
        context.failure_code = "wordpress_document_root_mount_unproven";
        context.message = "Exactly one managed document-root mount is required";
        return false;
    }
    return true;
}

bool WordPressRuntimeContextResolver::resolve_php_fpm_identity(WordPressRuntimeContext& context) const {
    const auto process_list = executor_.run({"docker", "top", context.php_container,
                                             "-eo", "pid,uid,gid,args"});
    if (process_list.exit_code != 0) {
        context.failure_code = "wordpress_filesystem_identity_unproven";
        context.message = "PHP-FPM worker identity could not be inspected";
        return false;
    }

    std::set<std::pair<int64_t, int64_t>> identities;
    std::istringstream lines(process_list.out);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find("php-fpm") == std::string::npos || line.find("pool") == std::string::npos) continue;
        std::istringstream fields(line);
        std::string pid_token;
        std::string uid_token;
        std::string gid_token;
        if (!(fields >> pid_token >> uid_token >> gid_token)) continue;
        int64_t pid = -1;
        int64_t uid = -1;
        int64_t gid = -1;
        if (parse_non_root_id(pid_token, pid) &&
            parse_non_root_id(uid_token, uid) && parse_non_root_id(gid_token, gid)) {
            identities.emplace(uid, gid);
        }
    }
    if (identities.size() != 1) {
        context.failure_code = "wordpress_filesystem_identity_unproven";
        context.message = "Exactly one non-root PHP-FPM worker identity is required";
        return false;
    }
    context.php_fpm_uid = identities.begin()->first;
    context.php_fpm_gid = identities.begin()->second;
    return true;
}

} // namespace containercp::wordpress
