#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_TYPES_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_TYPES_H

#include <optional>
#include <string>
#include <vector>

namespace containercp::wordpress {

enum class WordPressCredentialSource {
    Unknown,
    Missing,
    DirectConstant,
    EnvironmentVariable,
    ServerVariable,
    VariableReference,
    IncludedFile,
    Expression,
    FunctionCall,
    Mixed,
    Unsupported
};

enum class WordPressCredentialMutability {
    Unknown,
    MutableDirectConstant,
    ReadOnly,
    Unsupported,
    Ambiguous
};

enum class WordPressCredentialStatus {
    Unknown,
    Complete,
    NotWordPress,
    ConfigMissing,
    CredentialsMissing,
    Unsupported,
    Ambiguous,
    UnsafePath,
    Error
};

enum class WordPressCredentialValueState {
    Unknown,
    Missing,
    Present,
    Redacted,
    Unsupported,
    Ambiguous
};

enum class WordPressConfigIssueSeverity {
    Info,
    Warning,
    Error
};

std::string credential_source_to_string(WordPressCredentialSource source);
std::optional<WordPressCredentialSource> credential_source_from_string(const std::string& value);

std::string credential_mutability_to_string(WordPressCredentialMutability mutability);
std::optional<WordPressCredentialMutability> credential_mutability_from_string(const std::string& value);

std::string credential_status_to_string(WordPressCredentialStatus status);
std::optional<WordPressCredentialStatus> credential_status_from_string(const std::string& value);

std::string credential_value_state_to_string(WordPressCredentialValueState state);
std::optional<WordPressCredentialValueState> credential_value_state_from_string(const std::string& value);

std::string config_issue_severity_to_string(WordPressConfigIssueSeverity severity);
std::optional<WordPressConfigIssueSeverity> config_issue_severity_from_string(const std::string& value);

struct WordPressCredentialValue {
    WordPressCredentialValueState state = WordPressCredentialValueState::Unknown;
    std::string value;
    bool sensitive = false;

    static WordPressCredentialValue public_value(std::string value);
    static WordPressCredentialValue secret_present();
    static WordPressCredentialValue missing();

    bool has_public_value() const;
    std::string public_display_value() const;
    WordPressCredentialValue public_safe() const;
};

struct WordPressCredentialSet {
    WordPressCredentialValue db_name;
    WordPressCredentialValue db_user;
    WordPressCredentialValue db_password;
    WordPressCredentialValue db_host;

    WordPressCredentialSet public_safe() const;
};

struct WordPressConfigIssue {
    WordPressConfigIssueSeverity severity = WordPressConfigIssueSeverity::Info;
    std::string code;
    std::string message;
};

struct WordPressConfigInspection {
    WordPressCredentialSource source = WordPressCredentialSource::Unknown;
    WordPressCredentialMutability mutability = WordPressCredentialMutability::Unknown;
    WordPressCredentialStatus status = WordPressCredentialStatus::Unknown;
    WordPressCredentialSet credentials;
    std::vector<WordPressConfigIssue> issues;

    WordPressConfigInspection public_safe() const;
};

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_CONFIG_TYPES_H
