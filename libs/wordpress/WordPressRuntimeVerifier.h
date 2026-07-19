#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_RUNTIME_VERIFIER_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_RUNTIME_VERIFIER_H

#include "runtime/CommandExecutor.h"

#include <filesystem>
#include <string>
#include <vector>

namespace containercp::wordpress {

struct WordPressRuntimeVerificationRequest {
    std::filesystem::path compose_dir;
    std::filesystem::path document_root;
    std::filesystem::path config_path;
    std::string php_service = "php";
    std::string container_document_root = "/var/www/html";
};

struct WordPressRuntimeVerificationResult {
    bool success = false;
    std::string code;
    std::string message;
};

class WordPressRuntimeCommandRunner {
public:
    virtual ~WordPressRuntimeCommandRunner() = default;
    virtual runtime::CommandResult run(const std::vector<std::string>& args,
                                       const std::string& workdir = "") const = 0;
};

class WordPressRuntimeCommandExecutorRunner : public WordPressRuntimeCommandRunner {
public:
    explicit WordPressRuntimeCommandExecutorRunner(const runtime::CommandExecutor& executor);

    runtime::CommandResult run(const std::vector<std::string>& args,
                               const std::string& workdir = "") const override;

private:
    const runtime::CommandExecutor& executor_;
};

class WordPressRuntimeVerifier {
public:
    explicit WordPressRuntimeVerifier(const WordPressRuntimeCommandRunner& runner);

    WordPressRuntimeVerificationResult verify_database_access(const WordPressRuntimeVerificationRequest& request) const;

private:
    const WordPressRuntimeCommandRunner& runner_;
};

std::string wordpress_runtime_verification_script(const std::string& container_config_path);

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_RUNTIME_VERIFIER_H
