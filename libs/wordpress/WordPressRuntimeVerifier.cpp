#include "WordPressRuntimeVerifier.h"

#include <cctype>
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

bool has_parent_reference(const fs::path& path) {
    for (const auto& part : path) {
        if (part == "..") {
            return true;
        }
    }
    return false;
}

bool is_safe_service_name(const std::string& value) {
    if (value.empty() || value.size() > 64) {
        return false;
    }
    for (unsigned char c : value) {
        const bool alnum = std::isalnum(c) != 0;
        if (!alnum && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

bool is_safe_container_root(const fs::path& path) {
    return !path.empty() && path.is_absolute() && !has_parent_reference(path);
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
    if (!request.compose_dir.is_absolute() || !request.document_root.is_absolute() || !request.config_path.is_absolute()) {
        return result(false, "unsafe_runtime_path", "WordPress runtime verification paths must be absolute");
    }
    if (!is_safe_service_name(request.php_service)) {
        return result(false, "invalid_php_service", "WordPress runtime verification PHP service is invalid");
    }
    const fs::path container_document_root = request.container_document_root.empty()
        ? fs::path("/var/www/html")
        : fs::path(request.container_document_root);
    if (!is_safe_container_root(container_document_root)) {
        return result(false, "unsafe_container_document_root", "WordPress runtime verification container document root is unsafe");
    }

    std::error_code ec;
    const fs::path compose_dir = fs::absolute(request.compose_dir, ec).lexically_normal();
    if (ec) {
        return result(false, "unsafe_runtime_path", "WordPress runtime verification compose path is unsafe");
    }
    const fs::path document_root = fs::absolute(request.document_root, ec).lexically_normal();
    if (ec || !path_has_prefix(document_root, compose_dir)) {
        return result(false, "unsafe_runtime_path", "WordPress runtime verification document root is outside the compose project");
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
