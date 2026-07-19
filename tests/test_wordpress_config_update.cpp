#include "wordpress/WordPressConfigUpdater.h"

#include "doctest/doctest.h"

using namespace containercp::wordpress;

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
}

TEST_CASE("WordPress updater preserves single quote style and escapes replacement") {
    WordPressConfigUpdater updater;
    const std::string input = "<?php\ndefine('DB_USER', 'old');\n";

    const auto result = updater.render_update(input, WordPressConfigUpdateField::DbUser, "new'user\\name");
    REQUIRE(result.success);
    CHECK(result.content == "<?php\ndefine('DB_USER', 'new\\'user\\\\name');\n");
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
