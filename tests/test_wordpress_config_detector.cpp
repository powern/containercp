#include "wordpress/WordPressConfigDetector.h"

#include "doctest/doctest.h"

using namespace containercp::wordpress;

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
