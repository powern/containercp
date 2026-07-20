#include "wordpress/WordPressConfigDetector.h"

#include "doctest/doctest.h"

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace containercp::wordpress;

namespace {

namespace fs = std::filesystem;

fs::path test_root(const std::string& name) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("containercp_wp_detector_" + name + "_" + std::to_string(unique));
}

void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

} // namespace

TEST_CASE("WordPress detector reads direct constants without exposing password") {
    WordPressConfigDetector detector;
    const auto inspection = detector.inspect_content(R"PHP(
<?php
define('DB_NAME', 'wp_prod');
define("DB_USER", "wp_user")
define( 'DB_PASSWORD' , 'super-secret' );
define('DB_HOST', 'mariadb:3306');
)PHP");

    CHECK(inspection.status == WordPressCredentialStatus::Complete);
    CHECK(inspection.source == WordPressCredentialSource::DirectConstant);
    CHECK(inspection.mutability == WordPressCredentialMutability::MutableDirectConstant);
    CHECK(inspection.credentials.db_name.value == "wp_prod");
    CHECK(inspection.credentials.db_user.value == "wp_user");
    CHECK(inspection.credentials.db_host.value == "mariadb:3306");
    CHECK(inspection.credentials.db_password.sensitive);
    CHECK(inspection.credentials.db_password.value.empty());
    CHECK(inspection.credentials.db_password.public_display_value() == "[redacted]");
}

TEST_CASE("WordPress detector ignores commented constants and handles escaped literals") {
    WordPressConfigDetector detector;
    const auto inspection = detector.inspect_content(R"PHP(
<?php
// define('DB_NAME', 'bad');
/* define('DB_USER', 'bad'); */
# define('DB_PASSWORD', 'bad');
define('DB_NAME', 'wp\'prod');
define("DB_USER", "wp\"user");
define('DB_PASSWORD', 'p@ss, with ) and ; chars');
)PHP");

    CHECK(inspection.status == WordPressCredentialStatus::Complete);
    CHECK(inspection.credentials.db_name.value == "wp'prod");
    CHECK(inspection.credentials.db_user.value == "wp\"user");
    CHECK(inspection.credentials.db_password.value.empty());
    CHECK(inspection.issues.empty());
}

TEST_CASE("WordPress detector reports duplicates as ambiguous") {
    WordPressConfigDetector detector;
    const auto inspection = detector.inspect_content(R"PHP(
<?php
define('DB_NAME', 'wp_one');
define('DB_NAME', 'wp_two');
define('DB_USER', 'wp_user');
define('DB_PASSWORD', 'secret');
)PHP");

    CHECK(inspection.status == WordPressCredentialStatus::Ambiguous);
    CHECK(inspection.mutability == WordPressCredentialMutability::Ambiguous);
    CHECK(inspection.credentials.db_name.value == "wp_two");
    REQUIRE_FALSE(inspection.issues.empty());
    CHECK(inspection.issues[0].code == "duplicate_constant");
}

TEST_CASE("WordPress detector reports conditional constants as ambiguous") {
    WordPressConfigDetector detector;
    const auto inspection = detector.inspect_content(R"PHP(
<?php
if (getenv('WP_ENV') === 'prod') {
    define('DB_NAME', 'wp_prod');
}
define('DB_USER', 'wp_user');
define('DB_PASSWORD', 'secret');
)PHP");

    CHECK(inspection.status == WordPressCredentialStatus::Ambiguous);
    CHECK(inspection.mutability == WordPressCredentialMutability::Ambiguous);
    CHECK(inspection.issues[0].code == "conditional_constant");
}

TEST_CASE("WordPress detector classifies dynamic credential sources") {
    WordPressConfigDetector detector;

    auto getenv_result = detector.inspect_content(R"PHP(
<?php
define('DB_NAME', getenv('WORDPRESS_DB_NAME'));
define('DB_USER', 'wp_user');
define('DB_PASSWORD', 'secret');
)PHP");
    CHECK(getenv_result.status == WordPressCredentialStatus::Unsupported);
    CHECK(getenv_result.source == WordPressCredentialSource::Mixed);
    CHECK(getenv_result.credentials.db_name.source == WordPressCredentialSource::EnvironmentVariable);

    auto env_result = detector.inspect_content("<?php define('DB_NAME', $_ENV['DB_NAME']); define('DB_USER', 'u'); define('DB_PASSWORD', 'p');");
    CHECK(env_result.credentials.db_name.source == WordPressCredentialSource::EnvironmentVariable);

    auto server_result = detector.inspect_content("<?php define('DB_NAME', $_SERVER['DB_NAME']); define('DB_USER', 'u'); define('DB_PASSWORD', 'p');");
    CHECK(server_result.credentials.db_name.source == WordPressCredentialSource::ServerVariable);

    auto variable_result = detector.inspect_content("<?php define('DB_NAME', $db_name); define('DB_USER', 'u'); define('DB_PASSWORD', 'p');");
    CHECK(variable_result.credentials.db_name.source == WordPressCredentialSource::VariableReference);

    auto include_result = detector.inspect_content("<?php define('DB_NAME', require '/run/secrets/db.php'); define('DB_USER', 'u'); define('DB_PASSWORD', 'p');");
    CHECK(include_result.credentials.db_name.source == WordPressCredentialSource::IncludedFile);

    auto concat_result = detector.inspect_content("<?php define('DB_NAME', 'wp_' . 'prod'); define('DB_USER', 'u'); define('DB_PASSWORD', 'p');");
    CHECK(concat_result.credentials.db_name.source == WordPressCredentialSource::Expression);

    auto helper_result = detector.inspect_content("<?php define('DB_NAME', database_name()); define('DB_USER', 'u'); define('DB_PASSWORD', 'p');");
    CHECK(helper_result.credentials.db_name.source == WordPressCredentialSource::FunctionCall);
}

TEST_CASE("WordPress detector reports missing content and missing credentials") {
    WordPressConfigDetector detector;

    auto empty = detector.inspect_content("\n  \t");
    CHECK(empty.status == WordPressCredentialStatus::ConfigMissing);
    CHECK(empty.source == WordPressCredentialSource::Missing);

    auto missing = detector.inspect_content("<?php define('AUTH_KEY', 'not db');");
    CHECK(missing.status == WordPressCredentialStatus::CredentialsMissing);
    CHECK(missing.credentials.db_password.value.empty());
    REQUIRE_FALSE(missing.issues.empty());
    CHECK(missing.issues.back().code == "credentials_missing");
}

TEST_CASE("WordPress detector accepts only regular active wp-config path inside site root") {
    WordPressConfigDetector detector;
    const auto root = test_root("valid_path");
    const auto config = root / "public" / "wp-config.php";
    write_file(config, "<?php define('DB_NAME', 'wp');");

    const auto result = detector.inspect_config_path(root, config);
    CHECK(result.safe);
    CHECK(result.status == WordPressCredentialStatus::Complete);
    CHECK(result.code == "ok");
    CHECK(result.config_path.filename() == "wp-config.php");

    fs::remove_all(root);
}

TEST_CASE("WordPress detector rejects backup temp and missing wp-config paths") {
    WordPressConfigDetector detector;
    const auto root = test_root("reject_names");
    fs::create_directories(root / "public");
    write_file(root / "public" / "wp-config.php.bak", "backup");

    auto backup = detector.inspect_config_path(root, root / "public" / "wp-config.php.bak");
    CHECK_FALSE(backup.safe);
    CHECK(backup.status == WordPressCredentialStatus::UnsafePath);
    CHECK(backup.code == "not_active_config");

    auto temp = detector.inspect_config_path(root, root / "public" / ".wp-config.php.swp");
    CHECK_FALSE(temp.safe);
    CHECK(temp.code == "not_active_config");

    auto missing = detector.inspect_config_path(root, root / "public" / "wp-config.php");
    CHECK_FALSE(missing.safe);
    CHECK(missing.status == WordPressCredentialStatus::ConfigMissing);
    CHECK(missing.code == "config_missing");

    fs::remove_all(root);
}

TEST_CASE("WordPress detector rejects traversal outside site root") {
    WordPressConfigDetector detector;
    const auto root = test_root("path_escape") / "site";
    const auto outside = root.parent_path() / "outside" / "wp-config.php";
    fs::create_directories(root);
    write_file(outside, "<?php define('DB_NAME', 'outside');");

    const auto result = detector.inspect_config_path(root, root / ".." / "outside" / "wp-config.php");
    CHECK_FALSE(result.safe);
    CHECK(result.status == WordPressCredentialStatus::UnsafePath);
    CHECK(result.code == "path_outside_root");

    fs::remove_all(root.parent_path());
}

TEST_CASE("WordPress detector treats trailing site root separator as equivalent") {
    WordPressConfigDetector detector;
    const auto root = test_root("trailing_root") / "site";
    const auto config = root / "public" / "wp-config.php";
    write_file(config, "<?php define('DB_NAME', 'wp');");

    const auto no_trailing = detector.inspect_config_path(root, config);
    const auto trailing = detector.inspect_config_path(fs::path(root.string() + "/"), config);

    CHECK(no_trailing.safe);
    CHECK(trailing.safe);
    CHECK(no_trailing.config_path == trailing.config_path);

    fs::remove_all(root.parent_path());
}

TEST_CASE("WordPress detector rejects symlinked config paths") {
    WordPressConfigDetector detector;
    const auto root = test_root("symlink");
    const auto target = root / "target.php";
    const auto link = root / "wp-config.php";
    write_file(target, "<?php define('DB_NAME', 'wp');");

    std::error_code ec;
    fs::create_symlink(target, link, ec);
    REQUIRE_FALSE(ec);

    const auto result = detector.inspect_config_path(root, link);
    CHECK_FALSE(result.safe);
    CHECK(result.status == WordPressCredentialStatus::UnsafePath);
    CHECK(result.code == "symlink_rejected");

    fs::remove_all(root);
}

TEST_CASE("WordPress detector rejects empty site root utility input") {
    WordPressConfigDetector detector;
    const auto result = detector.inspect_config_path({}, "wp-config.php");
    CHECK_FALSE(result.safe);
    CHECK(result.status == WordPressCredentialStatus::UnsafePath);
    CHECK(result.code == "site_root_missing");
}
