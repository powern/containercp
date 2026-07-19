#include "WordPressConfigDetector.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace containercp::wordpress {
namespace {

struct DefineCall {
    std::size_t offset = 0;
    std::string body;
    bool conditional = false;
};

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

std::size_t skip_spaces(const std::string& content, std::size_t pos) {
    while (pos < content.size() && std::isspace(static_cast<unsigned char>(content[pos]))) {
        ++pos;
    }
    return pos;
}

bool looks_conditional(const std::string& content, std::size_t define_offset) {
    const std::size_t start = define_offset > 180 ? define_offset - 180 : 0;
    std::string prefix = content.substr(start, define_offset - start);
    const std::array<std::string_view, 7> controls = {"if", "elseif", "else", "switch", "foreach", "for", "while"};
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        for (auto control : controls) {
            if (has_identifier_at(prefix, i, control)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<DefineCall> find_define_calls(const std::string& content) {
    std::vector<DefineCall> calls;
    bool in_single = false;
    bool in_double = false;
    bool in_line_comment = false;
    bool in_block_comment = false;

    for (std::size_t i = 0; i < content.size(); ++i) {
        char c = content[i];
        char next = i + 1 < content.size() ? content[i + 1] : '\0';

        if (in_line_comment) {
            if (c == '\n' || c == '\r') {
                in_line_comment = false;
            }
            continue;
        }
        if (in_block_comment) {
            if (c == '*' && next == '/') {
                in_block_comment = false;
                ++i;
            }
            continue;
        }
        if (in_single) {
            if (c == '\\') {
                ++i;
            } else if (c == '\'') {
                in_single = false;
            }
            continue;
        }
        if (in_double) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                in_double = false;
            }
            continue;
        }

        if (c == '/' && next == '/') {
            in_line_comment = true;
            ++i;
            continue;
        }
        if (c == '#') {
            in_line_comment = true;
            continue;
        }
        if (c == '/' && next == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }
        if (c == '\'') {
            in_single = true;
            continue;
        }
        if (c == '"') {
            in_double = true;
            continue;
        }

        if (!has_identifier_at(content, i, "define")) {
            continue;
        }

        std::size_t open = skip_spaces(content, i + 6);
        if (open >= content.size() || content[open] != '(') {
            continue;
        }

        bool arg_single = false;
        bool arg_double = false;
        bool arg_line_comment = false;
        bool arg_block_comment = false;
        int depth = 1;
        std::size_t pos = open + 1;
        for (; pos < content.size(); ++pos) {
            char ac = content[pos];
            char an = pos + 1 < content.size() ? content[pos + 1] : '\0';
            if (arg_line_comment) {
                if (ac == '\n' || ac == '\r') {
                    arg_line_comment = false;
                }
                continue;
            }
            if (arg_block_comment) {
                if (ac == '*' && an == '/') {
                    arg_block_comment = false;
                    ++pos;
                }
                continue;
            }
            if (arg_single) {
                if (ac == '\\') {
                    ++pos;
                } else if (ac == '\'') {
                    arg_single = false;
                }
                continue;
            }
            if (arg_double) {
                if (ac == '\\') {
                    ++pos;
                } else if (ac == '"') {
                    arg_double = false;
                }
                continue;
            }
            if (ac == '/' && an == '/') {
                arg_line_comment = true;
                ++pos;
                continue;
            }
            if (ac == '#') {
                arg_line_comment = true;
                continue;
            }
            if (ac == '/' && an == '*') {
                arg_block_comment = true;
                ++pos;
                continue;
            }
            if (ac == '\'') {
                arg_single = true;
                continue;
            }
            if (ac == '"') {
                arg_double = true;
                continue;
            }
            if (ac == '(') {
                ++depth;
            } else if (ac == ')') {
                --depth;
                if (depth == 0) {
                    calls.push_back({i, content.substr(open + 1, pos - open - 1), looks_conditional(content, i)});
                    i = pos;
                    break;
                }
            }
        }
    }
    return calls;
}

std::vector<std::string> split_top_level_args(const std::string& body) {
    std::vector<std::string> args;
    bool in_single = false;
    bool in_double = false;
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    std::size_t start = 0;

    for (std::size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (in_single) {
            if (c == '\\') {
                ++i;
            } else if (c == '\'') {
                in_single = false;
            }
            continue;
        }
        if (in_double) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                in_double = false;
            }
            continue;
        }
        if (c == '\'') {
            in_single = true;
        } else if (c == '"') {
            in_double = true;
        } else if (c == '(') {
            ++paren_depth;
        } else if (c == ')') {
            --paren_depth;
        } else if (c == '[') {
            ++bracket_depth;
        } else if (c == ']') {
            --bracket_depth;
        } else if (c == '{') {
            ++brace_depth;
        } else if (c == '}') {
            --brace_depth;
        } else if (c == ',' && paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
            args.push_back(trim(body.substr(start, i - start)));
            start = i + 1;
        }
    }
    args.push_back(trim(body.substr(start)));
    return args;
}

std::optional<std::string> parse_php_string_literal(const std::string& expr) {
    const std::string value = trim(expr);
    if (value.size() < 2 || (value.front() != '\'' && value.front() != '"')) {
        return std::nullopt;
    }
    const char quote = value.front();
    std::string parsed;
    for (std::size_t i = 1; i < value.size(); ++i) {
        char c = value[i];
        if (c == '\\') {
            if (i + 1 >= value.size()) {
                return std::nullopt;
            }
            parsed.push_back(value[i + 1]);
            ++i;
            continue;
        }
        if (c == quote) {
            const std::string rest = trim(value.substr(i + 1));
            if (rest.empty() || rest == ";") {
                return parsed;
            }
            return std::nullopt;
        }
        parsed.push_back(c);
    }
    return std::nullopt;
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

ParsedField parse_field(const std::vector<DefineCall>& calls, const std::string& field_name) {
    ParsedField result;
    for (const auto& call : calls) {
        auto args = split_top_level_args(call.body);
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
            result.source = classify_expression(args[1]);
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

} // namespace

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

    const auto calls = find_define_calls(content);
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
