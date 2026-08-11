#include "wordpress/WordPressCliService.h"

#include "security/SecureRandom.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <sys/stat.h>

namespace containercp::wordpress {
namespace {

constexpr int kRunnerTimeoutSeconds = 60;
constexpr int kCleanupTimeoutSeconds = 15;
constexpr std::size_t kMaxOutputBytes = 65536;
constexpr const char* kExpectedWpCliVersion = "2.11.0";
constexpr const char* kRunnerLabel = "containercp.wpcli.managed=true";
constexpr const char* kRunnerPrefix = "containercp-wpcli-";

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    return value.substr(first);
}

bool valid_sha256(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

bool valid_package_identifier(const std::string& value) {
    if (value.empty() || value.size() > 128 || value.front() == '-' || value.front() == '/' ||
        value.back() == '/' || value.find("..") != std::string::npos || value.find("//") != std::string::npos) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.' || c == '/';
    });
}

std::string sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) return {};

    EVP_MD_CTX* raw_context = EVP_MD_CTX_new();
    if (raw_context == nullptr) return {};
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(raw_context, &EVP_MD_CTX_free);
    if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) return {};

    std::array<char, 8192> buffer{};
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(count)) != 1) {
            return {};
        }
    }
    if (!input.eof()) return {};

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1) return {};
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest_size * 2);
    for (unsigned int i = 0; i < digest_size; ++i) {
        result.push_back(hex[(digest[i] >> 4) & 0x0f]);
        result.push_back(hex[digest[i] & 0x0f]);
    }
    return result;
}

bool safe_read_only_root_file(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) return false;
    struct stat metadata{};
    if (::lstat(path.c_str(), &metadata) != 0) return false;
    return metadata.st_uid == 0 && (metadata.st_mode & 0222) == 0;
}

bool safe_root_directory(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) return false;
    struct stat metadata{};
    if (::lstat(path.c_str(), &metadata) != 0) return false;
    return metadata.st_uid == 0 && (metadata.st_mode & 0022) == 0;
}

std::vector<std::string> split_pipe(const std::string& value) {
    std::vector<std::string> fields;
    std::istringstream stream(value);
    std::string field;
    while (std::getline(stream, field, '|')) {
        fields.push_back(trim(field));
    }
    return fields;
}

} // namespace

std::string wordPressCliOperationName(WordPressCliOperation operation) {
    switch (operation) {
        case WordPressCliOperation::CoreIsInstalled: return "core-is-installed";
        case WordPressCliOperation::CoreVersion: return "core-version";
        case WordPressCliOperation::PluginList: return "plugin-list";
        case WordPressCliOperation::ThemeList: return "theme-list";
    }
    return "unknown";
}

bool parseWordPressCliOperation(const std::string& value,
                                WordPressCliOperation& operation) {
    if (value == "core-is-installed") {
        operation = WordPressCliOperation::CoreIsInstalled;
        return true;
    }
    if (value == "core-version") {
        operation = WordPressCliOperation::CoreVersion;
        return true;
    }
    if (value == "plugin-list") {
        operation = WordPressCliOperation::PluginList;
        return true;
    }
    if (value == "theme-list") {
        operation = WordPressCliOperation::ThemeList;
        return true;
    }
    return false;
}

std::string wordPressCliMutationName(WordPressCliMutation mutation) {
    switch (mutation) {
        case WordPressCliMutation::PluginInstall: return "plugin-install";
        case WordPressCliMutation::PluginActivate: return "plugin-activate";
        case WordPressCliMutation::PluginDeactivate: return "plugin-deactivate";
        case WordPressCliMutation::PluginUpdate: return "plugin-update";
        case WordPressCliMutation::PluginDelete: return "plugin-delete";
        case WordPressCliMutation::ThemeInstall: return "theme-install";
        case WordPressCliMutation::ThemeActivate: return "theme-activate";
        case WordPressCliMutation::ThemeUpdate: return "theme-update";
        case WordPressCliMutation::ThemeDelete: return "theme-delete";
        case WordPressCliMutation::CoreUpdate: return "core-update";
        case WordPressCliMutation::LanguageInstall: return "language-install";
        case WordPressCliMutation::LanguageUpdate: return "language-update";
        case WordPressCliMutation::CacheFlush: return "cache-flush";
    }
    return "unknown";
}

bool parseWordPressCliMutation(const std::string& value,
                               WordPressCliMutation& mutation) {
    const std::array<std::pair<const char*, WordPressCliMutation>, 13> values{{
        {"plugin-install", WordPressCliMutation::PluginInstall},
        {"plugin-activate", WordPressCliMutation::PluginActivate},
        {"plugin-deactivate", WordPressCliMutation::PluginDeactivate},
        {"plugin-update", WordPressCliMutation::PluginUpdate},
        {"plugin-delete", WordPressCliMutation::PluginDelete},
        {"theme-install", WordPressCliMutation::ThemeInstall},
        {"theme-activate", WordPressCliMutation::ThemeActivate},
        {"theme-update", WordPressCliMutation::ThemeUpdate},
        {"theme-delete", WordPressCliMutation::ThemeDelete},
        {"core-update", WordPressCliMutation::CoreUpdate},
        {"language-install", WordPressCliMutation::LanguageInstall},
        {"language-update", WordPressCliMutation::LanguageUpdate},
        {"cache-flush", WordPressCliMutation::CacheFlush},
    }};
    for (const auto& [name, value_mutation] : values) {
        if (value == name) {
            mutation = value_mutation;
            return true;
        }
    }
    return false;
}

bool wordPressCliMutationRequiresPackage(WordPressCliMutation mutation) {
    switch (mutation) {
        case WordPressCliMutation::PluginInstall:
        case WordPressCliMutation::PluginActivate:
        case WordPressCliMutation::PluginDeactivate:
        case WordPressCliMutation::PluginUpdate:
        case WordPressCliMutation::PluginDelete:
        case WordPressCliMutation::ThemeInstall:
        case WordPressCliMutation::ThemeActivate:
        case WordPressCliMutation::ThemeUpdate:
        case WordPressCliMutation::ThemeDelete:
        case WordPressCliMutation::LanguageInstall:
            return true;
        case WordPressCliMutation::CoreUpdate:
        case WordPressCliMutation::LanguageUpdate:
        case WordPressCliMutation::CacheFlush:
            return false;
    }
    return false;
}

bool validWordPressCliPackageIdentifier(const std::string& value) {
    return valid_package_identifier(value);
}

WordPressCliService::WordPressCliService(runtime::CommandExecutor& executor,
                                         WordPressRuntimeContextResolver& resolver,
                                         config::Config& config,
                                         logger::Logger& logger,
                                         std::string docker_executable)
    : executor_(executor)
    , resolver_(resolver)
    , config_(config)
    , logger_(logger)
    , docker_executable_(std::move(docker_executable)) {
}

WordPressCliResult WordPressCliService::failure(std::string code, std::string message) const {
    WordPressCliResult result;
    result.failure_code = std::move(code);
    result.message = std::move(message);
    return result;
}

std::vector<std::string> WordPressCliService::operation_arguments(WordPressCliOperation operation) {
    std::vector<std::string> arguments{
        "--no-color", "--skip-plugins", "--skip-themes"
    };
    switch (operation) {
        case WordPressCliOperation::CoreIsInstalled:
            arguments.insert(arguments.end(), {"core", "is-installed"});
            break;
        case WordPressCliOperation::CoreVersion:
            arguments.insert(arguments.end(), {"core", "version"});
            break;
        case WordPressCliOperation::PluginList:
            arguments.insert(arguments.end(), {"plugin", "list"});
            break;
        case WordPressCliOperation::ThemeList:
            arguments.insert(arguments.end(), {"theme", "list"});
            break;
    }
    return arguments;
}

std::vector<std::string> WordPressCliService::mutation_arguments(WordPressCliMutation mutation,
                                                                  const std::string& package_id) {
    switch (mutation) {
        case WordPressCliMutation::PluginInstall: return {"--no-color", "plugin", "install", package_id};
        case WordPressCliMutation::PluginActivate: return {"--no-color", "plugin", "activate", package_id};
        case WordPressCliMutation::PluginDeactivate: return {"--no-color", "plugin", "deactivate", package_id};
        case WordPressCliMutation::PluginUpdate: return {"--no-color", "plugin", "update", package_id};
        case WordPressCliMutation::PluginDelete: return {"--no-color", "plugin", "delete", package_id};
        case WordPressCliMutation::ThemeInstall: return {"--no-color", "theme", "install", package_id};
        case WordPressCliMutation::ThemeActivate: return {"--no-color", "theme", "activate", package_id};
        case WordPressCliMutation::ThemeUpdate: return {"--no-color", "theme", "update", package_id};
        case WordPressCliMutation::ThemeDelete: return {"--no-color", "theme", "delete", package_id};
        case WordPressCliMutation::CoreUpdate: return {"--no-color", "core", "update"};
        case WordPressCliMutation::LanguageInstall: return {"--no-color", "language", "core", "install", package_id};
        case WordPressCliMutation::LanguageUpdate: return {"--no-color", "language", "core", "update"};
        case WordPressCliMutation::CacheFlush: return {"--no-color", "cache", "flush"};
    }
    return {};
}

WordPressCliArtifact WordPressCliService::validate_artifact() const {
    WordPressCliArtifact artifact;
    const std::filesystem::path artifact_directory = config_.data_root() + "/wp-cli";
    const std::filesystem::path phar_path = artifact_directory / "wp-cli.phar";
    const std::filesystem::path version_path = artifact_directory / "version";
    const std::filesystem::path sha_path = artifact_directory / "sha256";

    std::error_code ec;
    const auto directory_status = std::filesystem::symlink_status(artifact_directory, ec);
    if (ec || !safe_root_directory(artifact_directory)) {
        artifact.failure_code = "wordpress_cli_artifact_missing";
        artifact.message = "Pinned WP-CLI artifact directory is unavailable";
        return artifact;
    }
    std::error_code file_ec;
    if (!std::filesystem::exists(phar_path, file_ec) ||
        !std::filesystem::exists(version_path, file_ec) ||
        !std::filesystem::exists(sha_path, file_ec)) {
        artifact.failure_code = "wordpress_cli_artifact_missing";
        artifact.message = "Pinned WP-CLI Phar or integrity metadata is missing";
        return artifact;
    }
    if (!safe_read_only_root_file(phar_path) || !safe_read_only_root_file(version_path) ||
        !safe_read_only_root_file(sha_path)) {
        artifact.failure_code = "wordpress_cli_artifact_untrusted";
        artifact.message = "Pinned WP-CLI artifact files must be root-owned, regular, and read-only";
        return artifact;
    }

    artifact.version = trim(std::ifstream(version_path).good() ? [&]() {
        std::ifstream input(version_path);
        std::ostringstream content;
        content << input.rdbuf();
        return content.str();
    }() : std::string());
    std::ifstream sha_input(sha_path);
    std::ostringstream sha_content;
    sha_content << sha_input.rdbuf();
    const std::string expected_sha = trim(sha_content.str());
    if (artifact.version != kExpectedWpCliVersion || !valid_sha256(expected_sha)) {
        artifact.failure_code = "wordpress_cli_artifact_metadata_invalid";
        artifact.message = "Pinned WP-CLI version or SHA-256 metadata does not match the reviewed artifact";
        return artifact;
    }

    const std::string actual_sha = sha256_file(phar_path);
    if (actual_sha.empty() || actual_sha != expected_sha) {
        artifact.failure_code = "wordpress_cli_artifact_integrity_failed";
        artifact.message = "Pinned WP-CLI Phar SHA-256 does not match its manifest";
        return artifact;
    }

    artifact.ok = true;
    artifact.phar_path = phar_path;
    artifact.sha256 = actual_sha;
    artifact.message = "Pinned WP-CLI artifact verified";
    return artifact;
}

bool WordPressCliService::trusted_docker_executable() const {
    if (docker_executable_.empty() || docker_executable_.front() != '/') return false;
    struct stat metadata{};
    if (::lstat(docker_executable_.c_str(), &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        S_ISLNK(metadata.st_mode) || metadata.st_uid != 0 || (metadata.st_mode & 0022) != 0) {
        logger_.error("WORDPRESS", "Trusted Docker executable metadata rejected");
        return false;
    }
    return (metadata.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}

std::string WordPressCliService::runner_name(uint64_t site_id) const {
    const auto suffix = security::SecureRandom::hex(8);
    if (!suffix.has_value()) return {};
    return std::string(kRunnerPrefix) + std::to_string(site_id) + "-" + *suffix;
}

runtime::CommandResult WordPressCliService::execute_docker(const std::vector<std::string>& args,
                                                           int timeout_seconds,
                                                           std::size_t max_output_bytes) const {
    std::vector<std::string> command;
    command.reserve(args.size() + 1);
    command.push_back(docker_executable_);
    command.insert(command.end(), args.begin(), args.end());
    return executor_.run_safe(command, {}, timeout_seconds, max_output_bytes);
}

bool WordPressCliService::verify_runner_absent(const std::string& identifier) const {
    const auto inspected = execute_docker({"inspect", identifier, "--format", "{{.Id}}"},
                                          kCleanupTimeoutSeconds, 4096);
    return inspected.exit_code != 0;
}

WordPressCliResult WordPressCliService::cleanup_runner(const std::string& runner) const {
    WordPressCliResult result;
    const auto removed = execute_docker({"rm", "-f", runner}, kCleanupTimeoutSeconds, 4096);
    if (!verify_runner_absent(runner)) {
        result.failure_code = "wordpress_cli_runner_cleanup_failed";
        result.message = "WP-CLI runner remained after cleanup";
        return result;
    }
    result.success = true;
    result.cleanup_succeeded = true;
    if (removed.exit_code != 0) {
        logger_.warning("WORDPRESS", "Managed WP-CLI runner was already absent during cleanup");
    }
    return result;
}

WordPressCliResult WordPressCliService::run(uint64_t site_id,
                                             WordPressCliOperation operation) const {
    return run_command(site_id, wordPressCliOperationName(operation), operation_arguments(operation), false);
}

WordPressCliResult WordPressCliService::run_mutation(uint64_t site_id,
                                                     WordPressCliMutation mutation,
                                                     const std::string& package_id) const {
    if (wordPressCliMutationRequiresPackage(mutation) && !valid_package_identifier(package_id)) {
        return failure("wordpress_cli_invalid_package", "Package identifier is invalid");
    }
    if (!wordPressCliMutationRequiresPackage(mutation) && !package_id.empty()) {
        return failure("wordpress_cli_unexpected_package", "This typed mutation does not accept a package identifier");
    }
    return run_command(site_id, wordPressCliMutationName(mutation), mutation_arguments(mutation, package_id), true);
}

WordPressCliResult WordPressCliService::run_command(uint64_t site_id,
                                                    const std::string& operation_name,
                                                    const std::vector<std::string>& arguments,
                                                    bool writable) const {
    const auto context = resolver_.resolve(site_id);
    if (!context.ok || (writable ? !context.mutation_capable : !context.read_only_capable)) {
        return failure(context.failure_code.empty() ? "wordpress_runtime_unavailable" : context.failure_code,
                       "Managed WordPress runtime is unavailable");
    }
    if (!trusted_docker_executable()) {
        return failure("wordpress_cli_docker_executable_untrusted",
                       "Trusted Docker executable is unavailable: " + docker_executable_);
    }

    const auto artifact = validate_artifact();
    if (!artifact.ok) return failure(artifact.failure_code, artifact.message);

    const auto name = runner_name(site_id);
    if (name.empty()) return failure("wordpress_cli_runner_name_failed", "Could not generate an internal runner name");

    std::vector<std::string> command{
        "run", "--name", name,
        "--label", kRunnerLabel,
        "--label", "containercp.wpcli.site.id=" + std::to_string(site_id),
        "--label", "containercp.wpcli.operation=" + operation_name,
        "--network", context.private_network,
        "--mount", "type=bind,src=" + context.document_root.string() + ",dst=" + context.container_document_root + (writable ? "" : ",readonly"),
        "--mount", "type=bind,src=" + artifact.phar_path.string() + ",dst=/opt/containercp/wp-cli/wp-cli.phar,readonly",
        "--read-only",
        "--cap-drop=ALL",
        "--security-opt=no-new-privileges",
        "--pids-limit", "64",
        "--memory", "256m",
        "--cpus", "1",
        "--tmpfs", "/tmp:rw,noexec,nosuid,size=64m",
        "--tmpfs", "/home/wpcli:rw,noexec,nosuid,size=16m",
        "--user", std::to_string(context.php_fpm_uid) + ":" + std::to_string(context.php_fpm_gid),
        "--workdir", context.container_document_root,
        "--env", "HOME=/home/wpcli",
        context.immutable_php_image_id,
        "php", "/opt/containercp/wp-cli/wp-cli.phar"
    };
    command.insert(command.end(), arguments.begin(), arguments.end());

    const auto execution = execute_docker(command, kRunnerTimeoutSeconds, kMaxOutputBytes);
    auto cleanup = cleanup_runner(name);
    WordPressCliResult result;
    result.output = execution.out;
    result.diagnostic = execution.err;
    result.exit_code = execution.exit_code;
    result.cleanup_succeeded = cleanup.cleanup_succeeded;
    if (!cleanup.success) {
        result.failure_code = cleanup.failure_code;
        result.message = cleanup.message;
        return result;
    }
    if (execution.exit_code != 0) {
        result.failure_code = execution.exit_code == -1
            ? "wordpress_cli_timeout" : "wordpress_cli_failed";
        result.message = "Typed WP-CLI operation failed";
        return result;
    }
    result.success = true;
    result.message = "Typed WP-CLI operation completed";
    return result;
}

WordPressCliResult WordPressCliService::reconcile_runner(const std::string& identifier) const {
    const auto inspected = execute_docker({"inspect", identifier, "--format",
        "{{.Name}}|{{index .Config.Labels \"containercp.wpcli.managed\"}}"},
        kCleanupTimeoutSeconds, 4096);
    const auto fields = split_pipe(inspected.out);
    if (inspected.exit_code != 0 || fields.size() != 2 || fields[1] != "true" ||
        fields[0].find("/" + std::string(kRunnerPrefix)) != 0) {
        return failure("wordpress_cli_reconciliation_rejected", "Unmanaged runner was not eligible for cleanup");
    }
    auto cleanup = cleanup_runner(identifier);
    if (!cleanup.success) return cleanup;
    return cleanup;
}

WordPressCliResult WordPressCliService::reconcile_stale_runners() const {
    if (!trusted_docker_executable()) {
        return failure("wordpress_cli_docker_executable_untrusted", "Trusted Docker executable is unavailable");
    }
    const auto listed = execute_docker({"ps", "-a", "--filter", "label=" + std::string(kRunnerLabel),
                                        "--format", "{{.ID}}"}, kCleanupTimeoutSeconds, 16384);
    if (listed.exit_code != 0) {
        return failure("wordpress_cli_reconciliation_failed", "Managed WP-CLI runners could not be enumerated");
    }
    std::istringstream lines(listed.out);
    std::string identifier;
    while (std::getline(lines, identifier)) {
        identifier = trim(identifier);
        if (identifier.empty()) continue;
        const auto cleanup = reconcile_runner(identifier);
        if (!cleanup.success) return cleanup;
    }
    WordPressCliResult result;
    result.success = true;
    result.cleanup_succeeded = true;
    result.message = "Managed stale WP-CLI runners reconciled";
    return result;
}

} // namespace containercp::wordpress
