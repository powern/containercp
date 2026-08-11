#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_CLI_SERVICE_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_CLI_SERVICE_H

#include "config/Config.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"
#include "wordpress/WordPressRuntimeContext.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace containercp::wordpress {

enum class WordPressCliOperation {
    CoreIsInstalled,
    CoreVersion,
    PluginList,
    ThemeList
};

std::string wordPressCliOperationName(WordPressCliOperation operation);

struct WordPressCliArtifact {
    bool ok = false;
    std::string failure_code;
    std::string message;
    std::filesystem::path phar_path;
    std::string version;
    std::string sha256;
};

struct WordPressCliResult {
    bool success = false;
    std::string failure_code;
    std::string message;
    std::string output;
    std::string diagnostic;
    bool cleanup_succeeded = false;
    int exit_code = -1;
};

class WordPressCliService {
public:
    WordPressCliService(runtime::CommandExecutor& executor,
                        WordPressRuntimeContextResolver& resolver,
                        config::Config& config,
                        logger::Logger& logger,
                        std::string docker_executable = "/usr/bin/docker");

    WordPressCliResult run(uint64_t site_id, WordPressCliOperation operation) const;
    WordPressCliResult reconcile_stale_runners() const;

    WordPressCliArtifact validate_artifact() const;

    static std::vector<std::string> operation_arguments(WordPressCliOperation operation);

private:
    WordPressCliResult failure(std::string code, std::string message) const;
    bool trusted_docker_executable() const;
    std::string runner_name(uint64_t site_id) const;
    WordPressCliResult cleanup_runner(const std::string& runner) const;
    bool verify_runner_absent(const std::string& identifier) const;
    WordPressCliResult reconcile_runner(const std::string& identifier) const;
    runtime::CommandResult execute_docker(const std::vector<std::string>& args,
                                          int timeout_seconds,
                                          std::size_t max_output_bytes) const;

    runtime::CommandExecutor& executor_;
    WordPressRuntimeContextResolver& resolver_;
    config::Config& config_;
    logger::Logger& logger_;
    std::string docker_executable_;
};

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_CLI_SERVICE_H
