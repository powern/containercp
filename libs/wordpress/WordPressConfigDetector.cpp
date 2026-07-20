#include "WordPressConfigDetector.h"

#include "utils/PathUtils.h"
#include "wordpress/WordPressPhpDefineScanner.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace containercp::wordpress {
namespace {

namespace fs = std::filesystem;

struct ParsedField {
    bool found = false;
    bool duplicate = false;
    bool conditional = false;
    WordPressCredentialSource source = WordPressCredentialSource::Missing;
    WordPressCredentialMutability mutability = WordPressCredentialMutability::Unknown;
    WordPressCredentialValueState state = WordPressCredentialValueState::Missing;
    std::string value;
};

std::string trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool starts_with_at(const std::string& content, std::size_t pos, std::string_view needle) {
    return pos + needle.size() <= content.size() &&
           std::string_view(content).substr(pos, needle.size()) == needle;
}

bool has_identifier_at(const std::string& content, std::size_t pos, std::string_view identifier) {
    if (!starts_with_at(content, pos, identifier)) {
        return false;
    }
    if (pos > 0 && is_identifier_char(content[pos - 1])) {
        return false;
    }
    const std::size_t end = pos + identifier.size();
    return end >= content.size() || !is_identifier_char(content[end]);
}

bool contains_identifier(const std::string& expr, std::string_view identifier) {
    for (std::size_t i = 0; i < expr.size(); ++i) {
        if (has_identifier_at(expr, i, identifier)) {
            return true;
        }
    }
    return false;
}

bool contains_any_operator(const std::string& expr) {
    static constexpr std::string_view operators = ".+-*/?:";
    return std::any_of(expr.begin(), expr.end(), [](char c) {
        return operators.find(c) != std::string_view::npos;
    });
}

WordPressCredentialSource classify_expression(const std::string& expr) {
    const std::string value = trim(expr);
    if (value.empty()) {
        return WordPressCredentialSource::Missing;
    }
    if (contains_identifier(value, "getenv") || value.find("$_ENV") != std::string::npos) {
        return WordPressCredentialSource::EnvironmentVariable;
    }
    if (value.find("$_SERVER") != std::string::npos) {
        return WordPressCredentialSource::ServerVariable;
    }
    if (contains_identifier(value, "include") || contains_identifier(value, "include_once") ||
        contains_identifier(value, "require") || contains_identifier(value, "require_once")) {
        return WordPressCredentialSource::IncludedFile;
    }
    if (value.find('$') != std::string::npos) {
        return WordPressCredentialSource::VariableReference;
    }
    if (contains_any_operator(value)) {
        return WordPressCredentialSource::Expression;
    }
    const auto open = value.find('(');
    if (open != std::string::npos && open > 0 && std::isalpha(static_cast<unsigned char>(value[0]))) {
        return WordPressCredentialSource::FunctionCall;
    }
    return WordPressCredentialSource::Unsupported;
}

ParsedField parse_field(const std::vector<PhpDefineCall>& calls, const std::string& field_name) {
    ParsedField result;
    for (const auto& call : calls) {
        auto args = split_php_top_level_arguments(call.body, call.body_start);
        if (args.size() < 2) {
            continue;
        }
        auto name = parse_php_string_literal(args[0]);
        if (!name || *name != field_name) {
            continue;
        }

        if (result.found) {
            result.duplicate = true;
        }
        result.found = true;
        result.conditional = result.conditional || call.conditional;

        auto literal = parse_php_string_literal(args[1]);
        if (literal) {
            result.source = WordPressCredentialSource::DirectConstant;
            result.mutability = WordPressCredentialMutability::MutableDirectConstant;
            result.state = WordPressCredentialValueState::Present;
            result.value = *literal;
        } else {
            result.source = classify_expression(args[1].text);
            result.mutability = WordPressCredentialMutability::Unsupported;
            result.state = WordPressCredentialValueState::Unsupported;
            result.value.clear();
        }
    }
    return result;
}

WordPressCredentialValue to_credential_value(const ParsedField& field, bool sensitive) {
    WordPressCredentialValue value;
    value.source = field.source;
    value.mutability = field.mutability;
    value.state = field.state;
    value.sensitive = sensitive;
    if (field.state == WordPressCredentialValueState::Present) {
        if (sensitive) {
            value.state = WordPressCredentialValueState::Redacted;
        } else {
            value.value = field.value;
        }
    }
    return value.public_safe();
}

void add_issue(std::vector<WordPressConfigIssue>& issues,
               WordPressConfigIssueSeverity severity,
               std::string code,
               std::string message) {
    issues.push_back({severity, std::move(code), std::move(message)});
}

WordPressConfigPathSafety unsafe_path(std::string code,
                                      std::string message,
                                      const fs::path& site_root,
                                      const fs::path& config_path,
                                      WordPressCredentialStatus status = WordPressCredentialStatus::UnsafePath) {
    WordPressConfigPathSafety result;
    result.safe = false;
    result.status = status;
    result.code = std::move(code);
    result.message = std::move(message);
    result.site_root = site_root;
    result.config_path = config_path;
    return result;
}

WordPressConfigPathSafety safe_path(const fs::path& site_root, const fs::path& config_path) {
    WordPressConfigPathSafety result;
    result.safe = true;
    result.status = WordPressCredentialStatus::Complete;
    result.code = "ok";
    result.message = "Active wp-config.php path is safe";
    result.site_root = site_root;
    result.config_path = config_path;
    return result;
}

} // namespace

bool WordPressConfigDetector::is_active_config_filename(const std::filesystem::path& path) {
    return path.filename() == "wp-config.php";
}

WordPressConfigPathSafety WordPressConfigDetector::inspect_config_path(const std::filesystem::path& site_root,
                                                                       const std::filesystem::path& candidate_path) const {
    std::error_code ec;
    if (site_root.empty()) {
        return unsafe_path("site_root_missing", "Site root is required", site_root, candidate_path);
    }
    if (candidate_path.empty()) {
        return unsafe_path("config_path_missing", "WordPress config path is required", site_root, candidate_path,
                           WordPressCredentialStatus::ConfigMissing);
    }

    const fs::path root_abs = fs::absolute(site_root, ec).lexically_normal();
    if (ec) {
        return unsafe_path("site_root_invalid", "Site root could not be resolved", site_root, candidate_path);
    }

    const fs::path candidate_abs = (candidate_path.is_absolute()
                                        ? fs::absolute(candidate_path, ec)
                                        : fs::absolute(root_abs / candidate_path, ec))
                                       .lexically_normal();
    if (ec) {
        return unsafe_path("config_path_invalid", "WordPress config path could not be resolved", root_abs, candidate_path);
    }

    if (!is_active_config_filename(candidate_abs)) {
        return unsafe_path("not_active_config", "Only active wp-config.php files are accepted", root_abs, candidate_abs);
    }

    if (!utils::path_has_prefix(candidate_abs, root_abs)) {
        return unsafe_path("path_outside_root", "WordPress config path escapes the site root", root_abs, candidate_abs);
    }

    const fs::file_status root_status = fs::symlink_status(root_abs, ec);
    if (ec || !fs::exists(root_status)) {
        return unsafe_path("site_root_missing", "Site root does not exist", root_abs, candidate_abs,
                           WordPressCredentialStatus::ConfigMissing);
    }
    if (fs::is_symlink(root_status)) {
        return unsafe_path("symlink_rejected", "Site root must not be a symlink", root_abs, candidate_abs);
    }
    if (!fs::is_directory(root_status)) {
        return unsafe_path("site_root_not_directory", "Site root must be a directory", root_abs, candidate_abs);
    }

    fs::path current = root_abs;
    const fs::path relative = candidate_abs.lexically_relative(root_abs);
    for (const auto& part : relative) {
        current /= part;
        const fs::file_status status = fs::symlink_status(current, ec);
        if (ec || !fs::exists(status)) {
            if (current == candidate_abs) {
                return unsafe_path("config_missing", "WordPress config file does not exist", root_abs, candidate_abs,
                                   WordPressCredentialStatus::ConfigMissing);
            }
            return unsafe_path("parent_missing", "WordPress config parent path does not exist", root_abs, candidate_abs,
                               WordPressCredentialStatus::ConfigMissing);
        }
        if (fs::is_symlink(status)) {
            return unsafe_path("symlink_rejected", "WordPress config path contains a symlink", root_abs, candidate_abs);
        }
        if (current != candidate_abs && !fs::is_directory(status)) {
            return unsafe_path("parent_not_directory", "WordPress config parent path is not a directory", root_abs, candidate_abs);
        }
        if (current == candidate_abs && !fs::is_regular_file(status)) {
            return unsafe_path("config_not_regular", "WordPress config path is not a regular file", root_abs, candidate_abs);
        }
    }

    return safe_path(root_abs, candidate_abs);
}

WordPressConfigInspection WordPressConfigDetector::inspect_content(const std::string& content) const {
    WordPressConfigInspection inspection;
    if (trim(content).empty()) {
        inspection.source = WordPressCredentialSource::Missing;
        inspection.mutability = WordPressCredentialMutability::ReadOnly;
        inspection.status = WordPressCredentialStatus::ConfigMissing;
        inspection.credentials.db_name = WordPressCredentialValue::missing();
        inspection.credentials.db_user = WordPressCredentialValue::missing();
        inspection.credentials.db_password = WordPressCredentialValue::missing();
        inspection.credentials.db_host = WordPressCredentialValue::missing();
        add_issue(inspection.issues, WordPressConfigIssueSeverity::Error, "config_missing", "WordPress config content is empty");
        return inspection;
    }

    const auto calls = find_php_define_calls(content);
    auto db_name = parse_field(calls, "DB_NAME");
    auto db_user = parse_field(calls, "DB_USER");
    auto db_password = parse_field(calls, "DB_PASSWORD");
    auto db_host = parse_field(calls, "DB_HOST");

    inspection.credentials.db_name = to_credential_value(db_name, false);
    inspection.credentials.db_user = to_credential_value(db_user, false);
    inspection.credentials.db_password = to_credential_value(db_password, true);
    inspection.credentials.db_host = to_credential_value(db_host, false);

    const std::array<std::pair<std::string_view, ParsedField>, 4> fields = {
        std::pair<std::string_view, ParsedField>{"DB_NAME", db_name},
        std::pair<std::string_view, ParsedField>{"DB_USER", db_user},
        std::pair<std::string_view, ParsedField>{"DB_PASSWORD", db_password},
        std::pair<std::string_view, ParsedField>{"DB_HOST", db_host},
    };

    bool found_any = false;
    bool duplicate = false;
    bool conditional = false;
    bool unsupported = false;
    bool missing_required = false;
    std::set<WordPressCredentialSource> sources;

    for (const auto& [name, field] : fields) {
        if (field.found) {
            found_any = true;
            sources.insert(field.source);
        }
        if (field.duplicate) {
            duplicate = true;
            add_issue(inspection.issues, WordPressConfigIssueSeverity::Error, "duplicate_constant",
                      std::string(name) + " is defined more than once");
        }
        if (field.conditional) {
            conditional = true;
            add_issue(inspection.issues, WordPressConfigIssueSeverity::Error, "conditional_constant",
                      std::string(name) + " is defined inside a conditional block");
        }
        if (field.found && field.source != WordPressCredentialSource::DirectConstant) {
            unsupported = true;
            add_issue(inspection.issues, WordPressConfigIssueSeverity::Error, "unsupported_credential_source",
                      std::string(name) + " does not use a direct string literal");
        }
        if ((name == "DB_NAME" || name == "DB_USER" || name == "DB_PASSWORD") && !field.found) {
            missing_required = true;
            add_issue(inspection.issues, WordPressConfigIssueSeverity::Error, "credential_missing",
                      std::string(name) + " is missing");
        }
    }

    if (!found_any) {
        inspection.source = WordPressCredentialSource::Missing;
        inspection.mutability = WordPressCredentialMutability::ReadOnly;
        inspection.status = WordPressCredentialStatus::CredentialsMissing;
        add_issue(inspection.issues, WordPressConfigIssueSeverity::Error, "credentials_missing", "No WordPress database constants were found");
    } else if (duplicate || conditional) {
        inspection.source = sources.size() == 1 ? *sources.begin() : WordPressCredentialSource::Mixed;
        inspection.mutability = WordPressCredentialMutability::Ambiguous;
        inspection.status = WordPressCredentialStatus::Ambiguous;
    } else if (unsupported) {
        inspection.source = sources.size() == 1 ? *sources.begin() : WordPressCredentialSource::Mixed;
        inspection.mutability = WordPressCredentialMutability::Unsupported;
        inspection.status = WordPressCredentialStatus::Unsupported;
    } else if (missing_required) {
        inspection.source = sources.size() == 1 ? *sources.begin() : WordPressCredentialSource::Mixed;
        inspection.mutability = WordPressCredentialMutability::ReadOnly;
        inspection.status = WordPressCredentialStatus::CredentialsMissing;
    } else {
        inspection.source = WordPressCredentialSource::DirectConstant;
        inspection.mutability = WordPressCredentialMutability::MutableDirectConstant;
        inspection.status = WordPressCredentialStatus::Complete;
    }

    return inspection.public_safe();
}

} // namespace containercp::wordpress
