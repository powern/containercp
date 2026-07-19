#include "wordpress/WordPressConfigTypes.h"

#include "doctest/doctest.h"

using namespace containercp::wordpress;

TEST_CASE("WordPress credential type string conversions round trip") {
    CHECK(credential_source_to_string(WordPressCredentialSource::DirectConstant) == "direct_constant");
    CHECK(credential_source_from_string("direct_constant") == WordPressCredentialSource::DirectConstant);
    CHECK_FALSE(credential_source_from_string("direct-constant").has_value());

    CHECK(credential_mutability_to_string(WordPressCredentialMutability::MutableDirectConstant) == "mutable_direct_constant");
    CHECK(credential_mutability_from_string("mutable_direct_constant") == WordPressCredentialMutability::MutableDirectConstant);
    CHECK_FALSE(credential_mutability_from_string("mutable").has_value());

    CHECK(credential_status_to_string(WordPressCredentialStatus::CredentialsMissing) == "credentials_missing");
    CHECK(credential_status_from_string("credentials_missing") == WordPressCredentialStatus::CredentialsMissing);
    CHECK_FALSE(credential_status_from_string("credentials missing").has_value());

    CHECK(credential_value_state_to_string(WordPressCredentialValueState::Redacted) == "redacted");
    CHECK(credential_value_state_from_string("redacted") == WordPressCredentialValueState::Redacted);
    CHECK_FALSE(credential_value_state_from_string("secret").has_value());

    CHECK(config_issue_severity_to_string(WordPressConfigIssueSeverity::Warning) == "warning");
    CHECK(config_issue_severity_from_string("warning") == WordPressConfigIssueSeverity::Warning);
    CHECK_FALSE(config_issue_severity_from_string("warn").has_value());
}

TEST_CASE("WordPress credential value exposes only public values") {
    auto db_name = WordPressCredentialValue::public_value("wordpress_db");
    CHECK(db_name.has_public_value());
    CHECK(db_name.public_display_value() == "wordpress_db");
    CHECK(db_name.public_safe().value == "wordpress_db");

    auto password = WordPressCredentialValue::secret_present();
    CHECK_FALSE(password.has_public_value());
    CHECK(password.public_display_value() == "[redacted]");
    CHECK(password.value.empty());
    CHECK(password.public_safe().value.empty());
    CHECK(password.public_safe().state == WordPressCredentialValueState::Redacted);
}

TEST_CASE("WordPress inspection public view redacts sensitive values") {
    WordPressConfigInspection inspection;
    inspection.source = WordPressCredentialSource::DirectConstant;
    inspection.mutability = WordPressCredentialMutability::MutableDirectConstant;
    inspection.status = WordPressCredentialStatus::Complete;
    inspection.credentials.db_name = WordPressCredentialValue::public_value("wp_prod");
    inspection.credentials.db_user = WordPressCredentialValue::public_value("wp_user");
    inspection.credentials.db_host = WordPressCredentialValue::public_value("mariadb");
    inspection.credentials.db_password = WordPressCredentialValue::secret_present();
    inspection.issues.push_back({WordPressConfigIssueSeverity::Warning, "file_mode", "Config file is group-readable"});

    auto public_view = inspection.public_safe();
    CHECK(public_view.source == WordPressCredentialSource::DirectConstant);
    CHECK(public_view.mutability == WordPressCredentialMutability::MutableDirectConstant);
    CHECK(public_view.status == WordPressCredentialStatus::Complete);
    CHECK(public_view.credentials.db_name.value == "wp_prod");
    CHECK(public_view.credentials.db_user.value == "wp_user");
    CHECK(public_view.credentials.db_host.value == "mariadb");
    CHECK(public_view.credentials.db_password.sensitive);
    CHECK(public_view.credentials.db_password.value.empty());
    CHECK(public_view.credentials.db_password.public_display_value() == "[redacted]");
    CHECK(public_view.issues.size() == 1);
}

TEST_CASE("WordPress public-safe conversion clears accidentally populated sensitive value") {
    WordPressCredentialValue password;
    password.state = WordPressCredentialValueState::Present;
    password.value = "do-not-show";
    password.sensitive = true;

    auto safe_password = password.public_safe();
    CHECK(safe_password.sensitive);
    CHECK(safe_password.state == WordPressCredentialValueState::Redacted);
    CHECK(safe_password.value.empty());
    CHECK(safe_password.public_display_value() == "[redacted]");
}
