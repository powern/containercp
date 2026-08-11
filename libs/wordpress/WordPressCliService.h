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

enum class WordPressCliMutation {
    PluginInstall,
    PluginActivate,
    PluginDeactivate,
    PluginUpdate,
    PluginDelete,
    ThemeInstall,
    ThemeActivate,
    ThemeUpdate,
    ThemeDelete,
    CoreUpdate,
    LanguageInstall,
    LanguageUpdate,
    CacheFlush
};

std::string wordPressCliOperationName(WordPressCliOperation operation);
bool parseWordPressCliOperation(const std::string& value,
                                WordPressCliOperation& operation);
std::string wordPressCliMutationName(WordPressCliMutation mutation);
bool parseWordPressCliMutation(const std::string& value,
                               WordPressCliMutation& mutation);
bool wordPressCliMutationRequiresPackage(WordPressCliMutation mutation);
bool validWordPressCliPackageIdentifier(const std::string& value);

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
                        std::string docker_executable = "/usr/bin/docker",
                        int runner_timeout_seconds = 60,
                        int cleanup_timeout_seconds = 15);

    WordPressCliResult run(uint64_t site_id, WordPressCliOperation operation) const;
    WordPressCliResult run_mutation(uint64_t site_id,
                                    WordPressCliMutation mutation,
                                    const std::string& package_id = {}) const;
    WordPressCliResult reconcile_stale_runners() const;

    WordPressCliArtifact validate_artifact() const;

    static std::vector<std::string> operation_arguments(WordPressCliOperation operation);
    static std::vector<std::string> mutation_arguments(WordPressCliMutation mutation,
                                                       const std::string& package_id);

private:
    enum class RunnerPresenceState {
        Absent,
        Present,
        Unknown,
    };

    struct RunnerPresence {
        RunnerPresenceState state = RunnerPresenceState::Unknown;
        std::string message;
    };

    WordPressCliResult failure(std::string code, std::string message) const;
    bool trusted_docker_executable() const;
    std::string runner_name(uint64_t site_id, const std::string& execution_id) const;
    WordPressCliResult cleanup_runner(const std::string& runner,
                                     uint64_t site_id,
                                     const std::string& operation_name,
                                     const std::string& execution_id) const;
    RunnerPresence verify_runner_absent(const std::string& identifier) const;
    WordPressCliResult reconcile_runner(const std::string& identifier) const;
    WordPressCliResult run_command(uint64_t site_id,
                                   const std::string& operation_name,
                                   const std::vector<std::string>& arguments,
                                   bool writable) const;
    runtime::CommandResult execute_docker(const std::vector<std::string>& args,
                                          int timeout_seconds,
                                          std::size_t max_output_bytes) const;

    runtime::CommandExecutor& executor_;
    WordPressRuntimeContextResolver& resolver_;
    config::Config& config_;
    logger::Logger& logger_;
    std::string docker_executable_;
    int runner_timeout_seconds_;
    int cleanup_timeout_seconds_;
};

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_CLI_SERVICE_H
