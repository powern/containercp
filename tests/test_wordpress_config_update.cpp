#include "wordpress/WordPressConfigUpdater.h"

#include "doctest/doctest.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <sys/stat.h>

using namespace containercp::wordpress;

namespace {

namespace fs = std::filesystem;

struct PhpLiteral {
    char quote = '\'';
    std::string body;
};

std::optional<unsigned char> hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned char>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned char>(10 + c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<unsigned char>(10 + c - 'A');
    }
    return std::nullopt;
}

std::optional<PhpLiteral> extract_db_password_literal(const std::string& content) {
    const auto define_pos = content.find("DB_PASSWORD");
    if (define_pos == std::string::npos) {
        return std::nullopt;
    }
    const auto comma = content.find(',', define_pos);
    if (comma == std::string::npos) {
        return std::nullopt;
    }
    auto pos = comma + 1;
    while (pos < content.size() && std::isspace(static_cast<unsigned char>(content[pos]))) {
        ++pos;
    }
    if (pos >= content.size() || (content[pos] != '\'' && content[pos] != '"')) {
        return std::nullopt;
    }
    const char quote = content[pos++];
    std::string body;
    for (; pos < content.size(); ++pos) {
        const char c = content[pos];
        if (c == '\\') {
            if (pos + 1 >= content.size()) {
                return std::nullopt;
            }
            body.push_back(c);
            body.push_back(content[pos + 1]);
            ++pos;
            continue;
        }
        if (c == quote) {
            return PhpLiteral{quote, body};
        }
        body.push_back(c);
    }
    return std::nullopt;
}

std::optional<std::string> decode_php_literal(const PhpLiteral& literal) {
    std::string decoded;
    for (std::size_t i = 0; i < literal.body.size(); ++i) {
        const char c = literal.body[i];
        if (c != '\\') {
            decoded.push_back(c);
            continue;
        }
        if (i + 1 >= literal.body.size()) {
            return std::nullopt;
        }
        const char next = literal.body[++i];
        if (literal.quote == '\'') {
            if (next == '\\' || next == '\'') {
                decoded.push_back(next);
            } else {
                decoded.push_back('\\');
                decoded.push_back(next);
            }
            continue;
        }

        switch (next) {
        case 'n':
            decoded.push_back('\n');
            break;
        case 'r':
            decoded.push_back('\r');
            break;
        case 't':
            decoded.push_back('\t');
            break;
        case '0':
            decoded.push_back('\0');
            break;
        case '\\':
        case '"':
        case '$':
            decoded.push_back(next);
            break;
        case 'x': {
            if (i + 2 >= literal.body.size()) {
                return std::nullopt;
            }
            auto hi = hex_value(literal.body[i + 1]);
            auto lo = hex_value(literal.body[i + 2]);
            if (!hi || !lo) {
                return std::nullopt;
            }
            decoded.push_back(static_cast<char>((*hi << 4) | *lo));
            i += 2;
            break;
        }
        default:
            decoded.push_back(next);
            break;
        }
    }
    return decoded;
}

std::string render_password(const std::string& template_content, const std::string& password) {
    WordPressConfigUpdater updater;
    auto result = updater.render_update(template_content, WordPressConfigUpdateField::DbPassword, password);
    REQUIRE(result.success);
    return result.content;
}

void check_rendered_password_semantics(const std::string& template_content, const std::string& password) {
    const auto rendered = render_password(template_content, password);
    auto literal = extract_db_password_literal(rendered);
    REQUIRE(literal.has_value());
    auto decoded = decode_php_literal(*literal);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == password);
}

fs::path updater_test_root(const std::string& name) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("containercp_wp_update_" + name + "_" + std::to_string(unique));
}

void write_test_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string read_test_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool has_temp_update_files(const fs::path& dir) {
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().filename().string().find(".containercp-tmp-") != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("WordPress updater replaces direct DB_PASSWORD and preserves unrelated content") {
    WordPressConfigUpdater updater;
    const std::string input = R"PHP(<?php
// define('DB_PASSWORD', 'commented');
define('DB_NAME', 'wp_prod');
define('DB_PASSWORD', 'old-secret');
$table_prefix = 'wp_';
)PHP";

    const auto result = updater.render_update(input, WordPressConfigUpdateField::DbPassword, "new-secret");
    REQUIRE(result.success);
    CHECK(result.content.find("define('DB_PASSWORD', 'new-secret');") != std::string::npos);
    CHECK(result.content.find("old-secret") == std::string::npos);
    CHECK(result.content.find("commented") != std::string::npos);
    CHECK(result.content.find("$table_prefix = 'wp_';") != std::string::npos);
}

TEST_CASE("WordPress updater preserves double quote style and escapes replacement") {
    WordPressConfigUpdater updater;
    const std::string input = R"PHP(<?php
define("DB_PASSWORD", "old");
)PHP";

    const auto result = updater.render_update(input, WordPressConfigUpdateField::DbPassword, R"(pa"ss\word)");
    REQUIRE(result.success);
    CHECK(result.content.find(R"(define("DB_PASSWORD", "pa\"ss\\word");)") != std::string::npos);
    check_rendered_password_semantics(input, R"(pa"ss\word)");
}

TEST_CASE("WordPress updater preserves single quote style and escapes replacement") {
    WordPressConfigUpdater updater;
    const std::string input = "<?php\ndefine('DB_USER', 'old');\n";

    const auto result = updater.render_update(input, WordPressConfigUpdateField::DbUser, "new'user\\name");
    REQUIRE(result.success);
    CHECK(result.content == "<?php\ndefine('DB_USER', 'new\\'user\\\\name');\n");
}

TEST_CASE("WordPress updater escapes dollar interpolation in double quoted password semantics") {
    const std::string input = "<?php\ndefine(\"DB_PASSWORD\", \"old\");\n";

    check_rendered_password_semantics(input, "abc$word123");
    check_rendered_password_semantics(input, "abc${name}xyz");
    check_rendered_password_semantics(input, "$name");

    const auto rendered = render_password(input, "abc$word123-${name}-$name");
    CHECK(rendered.find("abc\\$word123-\\${name}-\\$name") != std::string::npos);
}

TEST_CASE("WordPress updater encodes double quoted control characters with PHP semantics") {
    const std::string input = "<?php\ndefine(\"DB_PASSWORD\", \"old\");\n";
    std::string password = "line\ncarriage\rtab\tzero";
    password.push_back('\0');
    password.push_back('\x01');
    password += "end$var${name}\\\"";

    const auto rendered = render_password(input, password);
    CHECK(rendered.find("\\n") != std::string::npos);
    CHECK(rendered.find("\\r") != std::string::npos);
    CHECK(rendered.find("\\t") != std::string::npos);
    CHECK(rendered.find("\\0") != std::string::npos);
    CHECK(rendered.find("\\x01") != std::string::npos);
    CHECK(rendered.find("\\$var\\${name}") != std::string::npos);

    auto literal = extract_db_password_literal(rendered);
    REQUIRE(literal.has_value());
    auto decoded = decode_php_literal(*literal);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == password);
}

TEST_CASE("WordPress updater preserves single quoted newline carriage return tab semantics") {
    const std::string input = "<?php\ndefine('DB_PASSWORD', 'old');\n";
    const std::string password = "line\ncarriage\rtab\tbackslash\\quote'";

    const auto rendered = render_password(input, password);
    CHECK(rendered.find("backslash\\\\quote\\'") != std::string::npos);
    auto literal = extract_db_password_literal(rendered);
    REQUIRE(literal.has_value());
    CHECK(literal->quote == '\'');
    auto decoded = decode_php_literal(*literal);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == password);
}

TEST_CASE("WordPress updater rejects unsupported single quoted NUL and control characters") {
    WordPressConfigUpdater updater;
    const std::string input = "<?php\ndefine('DB_PASSWORD', 'old');\n";

    std::string nul_password = "prefix";
    nul_password.push_back('\0');
    nul_password += "suffix";
    const auto nul_result = updater.render_update(input, WordPressConfigUpdateField::DbPassword, nul_password);
    CHECK_FALSE(nul_result.success);
    CHECK(nul_result.code == "unsupported_credential_value");
    CHECK(nul_result.message.find("prefix") == std::string::npos);

    std::string control_password = "prefix";
    control_password.push_back('\x01');
    control_password += "suffix";
    const auto control_result = updater.render_update(input, WordPressConfigUpdateField::DbPassword, control_password);
    CHECK_FALSE(control_result.success);
    CHECK(control_result.code == "unsupported_credential_value");
    CHECK(control_result.message.find("prefix") == std::string::npos);
}

TEST_CASE("WordPress updater preserves semantic value for combined special characters") {
    const std::string double_input = "<?php\ndefine(\"DB_PASSWORD\", \"old\");\n";
    const std::string single_input = "<?php\ndefine('DB_PASSWORD', 'old');\n";
    const std::string password = "a$b${name}$word\\single'\"\n\r\tend";

    check_rendered_password_semantics(double_input, password);
    check_rendered_password_semantics(single_input, password);
}

TEST_CASE("WordPress updater rejects duplicate target constants") {
    WordPressConfigUpdater updater;
    const std::string input = R"PHP(<?php
define('DB_PASSWORD', 'one');
define('DB_PASSWORD', 'two');
)PHP";

    const auto result = updater.render_update(input, WordPressConfigUpdateField::DbPassword, "new");
    CHECK_FALSE(result.success);
    CHECK(result.code == "ambiguous_credential_source");
    CHECK(result.message.find("new") == std::string::npos);
}

TEST_CASE("WordPress updater rejects dynamic and included target constants") {
    WordPressConfigUpdater updater;

    auto getenv_result = updater.render_update("<?php define('DB_PASSWORD', getenv('DB_PASSWORD'));", WordPressConfigUpdateField::DbPassword, "new");
    CHECK_FALSE(getenv_result.success);
    CHECK(getenv_result.code == "unsupported_credential_source");

    auto include_result = updater.render_update("<?php define('DB_PASSWORD', require '/run/secret.php');", WordPressConfigUpdateField::DbPassword, "new");
    CHECK_FALSE(include_result.success);
    CHECK(include_result.code == "unsupported_credential_source");
}

TEST_CASE("WordPress updater rejects conditional target constants") {
    WordPressConfigUpdater updater;
    const std::string input = R"PHP(<?php
if ($env === 'prod') {
    define('DB_PASSWORD', 'old');
}
)PHP";

    const auto result = updater.render_update(input, WordPressConfigUpdateField::DbPassword, "new");
    CHECK_FALSE(result.success);
    CHECK(result.code == "ambiguous_credential_source");
}

TEST_CASE("WordPress updater keeps unconditional password writable after previous conditional block") {
    WordPressConfigUpdater updater;
    const std::string input = R"PHP(<?php
if ($env === 'prod') {
    define('WP_DEBUG', true);
}
define('DB_PASSWORD', 'old');
)PHP";

    const auto result = updater.render_update(input, WordPressConfigUpdateField::DbPassword, "new");
    REQUIRE(result.success);
    CHECK(result.content.find("define('DB_PASSWORD', 'new');") != std::string::npos);
}

TEST_CASE("WordPress updater ignores if word in comments and strings for conditional detection") {
    WordPressConfigUpdater updater;
    const std::string input = R"PHP(<?php
// if this comment were parsed, the next define would be rejected
$message = "if inside a string must not affect DB_PASSWORD";
define('DB_PASSWORD', 'old');
)PHP";

    const auto result = updater.render_update(input, WordPressConfigUpdateField::DbPassword, "new");
    REQUIRE(result.success);
    CHECK(result.content.find("define('DB_PASSWORD', 'new');") != std::string::npos);
}

TEST_CASE("WordPress updater atomically updates file and preserves mode") {
    WordPressConfigUpdater updater;
    const auto root = updater_test_root("atomic_success");
    const auto config = root / "public" / "wp-config.php";
    const std::string original = "<?php\ndefine('DB_PASSWORD', 'old');\n$table_prefix = 'wp_';\n";
    write_test_file(config, original);
    fs::permissions(config, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);

    const auto result = updater.update_file_atomic(root, config, WordPressConfigUpdateField::DbPassword, "new");
    REQUIRE(result.success);
    CHECK(result.rollback.valid);
    CHECK(result.rollback.previous_content == original);
    CHECK(read_test_file(config).find("define('DB_PASSWORD', 'new');") != std::string::npos);
    CHECK(read_test_file(config).find("$table_prefix = 'wp_';") != std::string::npos);
    CHECK_FALSE(has_temp_update_files(config.parent_path()));

    struct stat st {};
    REQUIRE(::stat(config.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);

    fs::remove_all(root);
}

TEST_CASE("WordPress updater rollback restores previous file content") {
    WordPressConfigUpdater updater;
    const auto root = updater_test_root("rollback");
    const auto config = root / "public" / "wp-config.php";
    const std::string original = "<?php\ndefine(\"DB_PASSWORD\", \"old\");\n";
    write_test_file(config, original);

    const auto update = updater.update_file_atomic(root, config, WordPressConfigUpdateField::DbPassword, "new$secret");
    REQUIRE(update.success);
    CHECK(read_test_file(config).find("new\\$secret") != std::string::npos);

    const auto rollback = updater.rollback_file(update.rollback);
    REQUIRE(rollback.success);
    CHECK(read_test_file(config) == original);
    CHECK_FALSE(has_temp_update_files(config.parent_path()));

    fs::remove_all(root);
}

TEST_CASE("WordPress updater failed file render leaves original content and cleans temp files") {
    WordPressConfigUpdater updater;
    const auto root = updater_test_root("render_failure");
    const auto config = root / "public" / "wp-config.php";
    const std::string original = R"PHP(<?php
define('DB_PASSWORD', 'one');
define('DB_PASSWORD', 'two');
)PHP";
    write_test_file(config, original);

    const auto result = updater.update_file_atomic(root, config, WordPressConfigUpdateField::DbPassword, "new-secret");
    CHECK_FALSE(result.success);
    CHECK(result.code == "ambiguous_credential_source");
    CHECK(result.message.find("new-secret") == std::string::npos);
    CHECK(read_test_file(config) == original);
    CHECK_FALSE(has_temp_update_files(config.parent_path()));

    fs::remove_all(root);
}

TEST_CASE("WordPress updater rejects symlink config file without touching target") {
    WordPressConfigUpdater updater;
    const auto root = updater_test_root("symlink_reject");
    const auto target = root / "public" / "target.php";
    const auto link = root / "public" / "wp-config.php";
    write_test_file(target, "<?php\ndefine('DB_PASSWORD', 'target');\n");
    std::error_code ec;
    fs::create_symlink(target, link, ec);
    REQUIRE_FALSE(ec);

    const auto result = updater.update_file_atomic(root, link, WordPressConfigUpdateField::DbPassword, "new-secret");
    CHECK_FALSE(result.success);
    CHECK(result.code == "symlink_rejected");
    CHECK(read_test_file(target).find("target") != std::string::npos);
    CHECK_FALSE(has_temp_update_files(target.parent_path()));

    fs::remove_all(root);
}

TEST_CASE("WordPress updater rejects path outside site root") {
    WordPressConfigUpdater updater;
    const auto base = updater_test_root("path_escape");
    const auto root = base / "site";
    const auto outside = base / "outside" / "wp-config.php";
    fs::create_directories(root);
    write_test_file(outside, "<?php\ndefine('DB_PASSWORD', 'outside');\n");

    const auto result = updater.update_file_atomic(root, root / ".." / "outside" / "wp-config.php", WordPressConfigUpdateField::DbPassword, "new");
    CHECK_FALSE(result.success);
    CHECK(result.code == "path_outside_root");
    CHECK(read_test_file(outside).find("outside") != std::string::npos);
    CHECK_FALSE(has_temp_update_files(outside.parent_path()));

    fs::remove_all(base);
}

TEST_CASE("WordPress updater invalid rollback handle fails closed") {
    WordPressConfigUpdater updater;
    const WordPressConfigRollbackHandle rollback;

    const auto result = updater.rollback_file(rollback);
    CHECK_FALSE(result.success);
    CHECK(result.code == "rollback_invalid");
}
