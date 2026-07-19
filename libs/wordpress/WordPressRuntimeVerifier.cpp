#include "WordPressRuntimeVerifier.h"

#include <system_error>
#include <utility>

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

WordPressRuntimeVerificationResult result(bool success, std::string code, std::string message) {
    return {success, std::move(code), std::move(message)};
}

std::string php_single_quoted_string(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('\'');
    for (const char c : value) {
        if (c == '\\' || c == '\'') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    escaped.push_back('\'');
    return escaped;
}

std::string to_container_config_path(const WordPressRuntimeVerificationRequest& request, bool& ok) {
    ok = false;
    std::error_code ec;
    const fs::path document_root = fs::absolute(request.document_root, ec).lexically_normal();
    if (ec) {
        return {};
    }
    const fs::path config_path = fs::absolute(request.config_path, ec).lexically_normal();
    if (ec || !path_has_prefix(config_path, document_root) || config_path.filename() != "wp-config.php") {
        return {};
    }

    fs::path relative = fs::relative(config_path, document_root, ec);
    if (ec || relative.empty() || relative.native().find("..") != std::string::npos) {
        return {};
    }

    fs::path container_path = request.container_document_root.empty()
        ? fs::path("/var/www/html")
        : fs::path(request.container_document_root);
    container_path /= relative;
    ok = true;
    return container_path.lexically_normal().string();
}

} // namespace

WordPressRuntimeCommandExecutorRunner::WordPressRuntimeCommandExecutorRunner(const runtime::CommandExecutor& executor)
    : executor_(executor) {
}

runtime::CommandResult WordPressRuntimeCommandExecutorRunner::run(const std::vector<std::string>& args,
                                                                  const std::string& workdir) const {
    return executor_.run(args, workdir);
}

WordPressRuntimeVerifier::WordPressRuntimeVerifier(const WordPressRuntimeCommandRunner& runner)
    : runner_(runner) {
}

WordPressRuntimeVerificationResult WordPressRuntimeVerifier::verify_database_access(const WordPressRuntimeVerificationRequest& request) const {
    if (request.compose_dir.empty() || request.document_root.empty() || request.config_path.empty() || request.php_service.empty()) {
        return result(false, "invalid_verification_request", "WordPress runtime verification request is incomplete");
    }

    bool path_ok = false;
    const std::string container_config_path = to_container_config_path(request, path_ok);
    if (!path_ok) {
        return result(false, "unsafe_config_path", "WordPress config path is outside the document root");
    }

    const std::vector<std::string> args = {
        "docker",
        "compose",
        "--project-directory",
        request.compose_dir.string(),
        "exec",
        "-T",
        request.php_service,
        "php",
        "-d",
        "display_errors=0",
        "-r",
        wordpress_runtime_verification_script(container_config_path),
    };

    const auto command = runner_.run(args);
    if (command.exit_code != 0) {
        return result(false, "wordpress_php_verification_failed", "WordPress runtime database verification failed");
    }
    return result(true, "wordpress_php_verification_ok", "WordPress runtime database verification passed");
}

std::string wordpress_runtime_verification_script(const std::string& container_config_path) {
    return "$cfg=" + php_single_quoted_string(container_config_path) + ";"
           "if(!is_file($cfg)){exit(2);}"
           "if(!defined('SHORTINIT')){define('SHORTINIT',true);}"
           "require $cfg;"
           "foreach(['DB_NAME','DB_USER','DB_PASSWORD','DB_HOST'] as $c){if(!defined($c)){exit(3);}}"
           "$db=@new mysqli(DB_HOST,DB_USER,DB_PASSWORD,DB_NAME);"
           "if($db->connect_errno){exit(4);}"
           "$ok=$db->query('SELECT 1');"
           "if(!$ok){exit(5);}"
           "$db->close();";
}

} // namespace containercp::wordpress
