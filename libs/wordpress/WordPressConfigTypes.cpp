#include "WordPressConfigTypes.h"

#include <array>
#include <utility>

namespace containercp::wordpress {
namespace {

template <typename Enum, std::size_t Size>
std::string enum_to_string(Enum value, const std::array<std::pair<Enum, const char*>, Size>& values) {
    for (const auto& [candidate, text] : values) {
        if (candidate == value) {
            return text;
        }
    }
    return "unknown";
}

template <typename Enum, std::size_t Size>
std::optional<Enum> enum_from_string(const std::string& value,
                                     const std::array<std::pair<Enum, const char*>, Size>& values) {
    for (const auto& [candidate, text] : values) {
        if (value == text) {
            return candidate;
        }
    }
    return std::nullopt;
}

constexpr std::array credential_sources = {
    std::pair{WordPressCredentialSource::Unknown, "unknown"},
    std::pair{WordPressCredentialSource::Missing, "missing"},
    std::pair{WordPressCredentialSource::DirectConstant, "direct_constant"},
    std::pair{WordPressCredentialSource::EnvironmentVariable, "environment_variable"},
    std::pair{WordPressCredentialSource::ServerVariable, "server_variable"},
    std::pair{WordPressCredentialSource::VariableReference, "variable_reference"},
    std::pair{WordPressCredentialSource::IncludedFile, "included_file"},
    std::pair{WordPressCredentialSource::Expression, "expression"},
    std::pair{WordPressCredentialSource::FunctionCall, "function_call"},
    std::pair{WordPressCredentialSource::Mixed, "mixed"},
    std::pair{WordPressCredentialSource::Unsupported, "unsupported"},
};

constexpr std::array credential_mutabilities = {
    std::pair{WordPressCredentialMutability::Unknown, "unknown"},
    std::pair{WordPressCredentialMutability::MutableDirectConstant, "mutable_direct_constant"},
    std::pair{WordPressCredentialMutability::ReadOnly, "read_only"},
    std::pair{WordPressCredentialMutability::Unsupported, "unsupported"},
    std::pair{WordPressCredentialMutability::Ambiguous, "ambiguous"},
};

constexpr std::array credential_statuses = {
    std::pair{WordPressCredentialStatus::Unknown, "unknown"},
    std::pair{WordPressCredentialStatus::Complete, "complete"},
    std::pair{WordPressCredentialStatus::NotWordPress, "not_wordpress"},
    std::pair{WordPressCredentialStatus::ConfigMissing, "config_missing"},
    std::pair{WordPressCredentialStatus::CredentialsMissing, "credentials_missing"},
    std::pair{WordPressCredentialStatus::Unsupported, "unsupported"},
    std::pair{WordPressCredentialStatus::Ambiguous, "ambiguous"},
    std::pair{WordPressCredentialStatus::UnsafePath, "unsafe_path"},
    std::pair{WordPressCredentialStatus::Error, "error"},
};

constexpr std::array credential_value_states = {
    std::pair{WordPressCredentialValueState::Unknown, "unknown"},
    std::pair{WordPressCredentialValueState::Missing, "missing"},
    std::pair{WordPressCredentialValueState::Present, "present"},
    std::pair{WordPressCredentialValueState::Redacted, "redacted"},
    std::pair{WordPressCredentialValueState::Unsupported, "unsupported"},
    std::pair{WordPressCredentialValueState::Ambiguous, "ambiguous"},
};

constexpr std::array config_issue_severities = {
    std::pair{WordPressConfigIssueSeverity::Info, "info"},
    std::pair{WordPressConfigIssueSeverity::Warning, "warning"},
    std::pair{WordPressConfigIssueSeverity::Error, "error"},
};

} // namespace

std::string credential_source_to_string(WordPressCredentialSource source) {
    return enum_to_string(source, credential_sources);
}

std::optional<WordPressCredentialSource> credential_source_from_string(const std::string& value) {
    return enum_from_string(value, credential_sources);
}

std::string credential_mutability_to_string(WordPressCredentialMutability mutability) {
    return enum_to_string(mutability, credential_mutabilities);
}

std::optional<WordPressCredentialMutability> credential_mutability_from_string(const std::string& value) {
    return enum_from_string(value, credential_mutabilities);
}

std::string credential_status_to_string(WordPressCredentialStatus status) {
    return enum_to_string(status, credential_statuses);
}

std::optional<WordPressCredentialStatus> credential_status_from_string(const std::string& value) {
    return enum_from_string(value, credential_statuses);
}

std::string credential_value_state_to_string(WordPressCredentialValueState state) {
    return enum_to_string(state, credential_value_states);
}

std::optional<WordPressCredentialValueState> credential_value_state_from_string(const std::string& value) {
    return enum_from_string(value, credential_value_states);
}

std::string config_issue_severity_to_string(WordPressConfigIssueSeverity severity) {
    return enum_to_string(severity, config_issue_severities);
}

std::optional<WordPressConfigIssueSeverity> config_issue_severity_from_string(const std::string& value) {
    return enum_from_string(value, config_issue_severities);
}

WordPressCredentialValue WordPressCredentialValue::public_value(std::string value) {
    WordPressCredentialValue result;
    result.state = WordPressCredentialValueState::Present;
    result.value = std::move(value);
    result.sensitive = false;
    return result;
}

WordPressCredentialValue WordPressCredentialValue::secret_present() {
    WordPressCredentialValue result;
    result.state = WordPressCredentialValueState::Redacted;
    result.sensitive = true;
    return result;
}

WordPressCredentialValue WordPressCredentialValue::missing() {
    WordPressCredentialValue result;
    result.state = WordPressCredentialValueState::Missing;
    return result;
}

bool WordPressCredentialValue::has_public_value() const {
    return !sensitive && !value.empty() && state == WordPressCredentialValueState::Present;
}

std::string WordPressCredentialValue::public_display_value() const {
    if (sensitive) {
        return state == WordPressCredentialValueState::Missing ? "" : "[redacted]";
    }
    return value;
}

WordPressCredentialValue WordPressCredentialValue::public_safe() const {
    WordPressCredentialValue result = *this;
    if (result.sensitive) {
        result.value.clear();
        if (result.state == WordPressCredentialValueState::Present) {
            result.state = WordPressCredentialValueState::Redacted;
        }
    }
    return result;
}

WordPressCredentialSet WordPressCredentialSet::public_safe() const {
    WordPressCredentialSet result;
    result.db_name = db_name.public_safe();
    result.db_user = db_user.public_safe();
    result.db_password = db_password.public_safe();
    result.db_host = db_host.public_safe();
    return result;
}

WordPressConfigInspection WordPressConfigInspection::public_safe() const {
    WordPressConfigInspection result = *this;
    result.credentials = credentials.public_safe();
    return result;
}

} // namespace containercp::wordpress
