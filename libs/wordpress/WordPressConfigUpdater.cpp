#include "WordPressConfigUpdater.h"

#include <cctype>
#include <optional>
#include <string_view>
#include <vector>

namespace containercp::wordpress {
namespace {

struct ArgumentSpan {
    std::size_t start = 0;
    std::size_t end = 0;
    std::string text;
};

struct ReplacementSpan {
    std::size_t value_start = 0;
    std::size_t value_end = 0;
    char quote = '\'';
    bool conditional = false;
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
    const std::string prefix = content.substr(start, define_offset - start);
    static constexpr std::string_view controls[] = {"if", "elseif", "else", "switch", "foreach", "for", "while"};
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        for (auto control : controls) {
            if (has_identifier_at(prefix, i, control)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<std::size_t> find_define_close(const std::string& content, std::size_t open) {
    bool in_single = false;
    bool in_double = false;
    bool in_line_comment = false;
    bool in_block_comment = false;
    int depth = 1;

    for (std::size_t pos = open + 1; pos < content.size(); ++pos) {
        char c = content[pos];
        char next = pos + 1 < content.size() ? content[pos + 1] : '\0';
        if (in_line_comment) {
            if (c == '\n' || c == '\r') {
                in_line_comment = false;
            }
            continue;
        }
        if (in_block_comment) {
            if (c == '*' && next == '/') {
                in_block_comment = false;
                ++pos;
            }
            continue;
        }
        if (in_single) {
            if (c == '\\') {
                ++pos;
            } else if (c == '\'') {
                in_single = false;
            }
            continue;
        }
        if (in_double) {
            if (c == '\\') {
                ++pos;
            } else if (c == '"') {
                in_double = false;
            }
            continue;
        }
        if (c == '/' && next == '/') {
            in_line_comment = true;
            ++pos;
            continue;
        }
        if (c == '#') {
            in_line_comment = true;
            continue;
        }
        if (c == '/' && next == '*') {
            in_block_comment = true;
            ++pos;
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
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
            if (depth == 0) {
                return pos;
            }
        }
    }
    return std::nullopt;
}

std::vector<ArgumentSpan> split_args_with_offsets(const std::string& body, std::size_t body_start) {
    std::vector<ArgumentSpan> args;
    bool in_single = false;
    bool in_double = false;
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    std::size_t start = 0;

    auto push_arg = [&](std::size_t end) {
        std::size_t local_start = start;
        while (local_start < end && std::isspace(static_cast<unsigned char>(body[local_start]))) {
            ++local_start;
        }
        std::size_t local_end = end;
        while (local_end > local_start && std::isspace(static_cast<unsigned char>(body[local_end - 1]))) {
            --local_end;
        }
        args.push_back({body_start + local_start, body_start + local_end, body.substr(local_start, local_end - local_start)});
    };

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
            push_arg(i);
            start = i + 1;
        }
    }
    push_arg(body.size());
    return args;
}

std::optional<std::string> parse_string_literal(const ArgumentSpan& arg) {
    if (arg.text.size() < 2 || (arg.text.front() != '\'' && arg.text.front() != '"')) {
        return std::nullopt;
    }
    const char quote = arg.text.front();
    std::string parsed;
    for (std::size_t i = 1; i < arg.text.size(); ++i) {
        char c = arg.text[i];
        if (c == '\\') {
            if (i + 1 >= arg.text.size()) {
                return std::nullopt;
            }
            parsed.push_back(arg.text[i + 1]);
            ++i;
            continue;
        }
        if (c == quote) {
            const std::string rest = trim(arg.text.substr(i + 1));
            if (rest.empty() || rest == ";") {
                return parsed;
            }
            return std::nullopt;
        }
        parsed.push_back(c);
    }
    return std::nullopt;
}

std::optional<ReplacementSpan> literal_value_span(const ArgumentSpan& arg, bool conditional) {
    if (arg.text.size() < 2 || (arg.text.front() != '\'' && arg.text.front() != '"')) {
        return std::nullopt;
    }
    const char quote = arg.text.front();
    for (std::size_t i = 1; i < arg.text.size(); ++i) {
        char c = arg.text[i];
        if (c == '\\') {
            ++i;
            continue;
        }
        if (c == quote) {
            const std::string rest = trim(arg.text.substr(i + 1));
            if (!rest.empty() && rest != ";") {
                return std::nullopt;
            }
            return ReplacementSpan{arg.start + 1, arg.start + i, quote, conditional};
        }
    }
    return std::nullopt;
}

std::vector<ReplacementSpan> find_target_spans(const std::string& content, const std::string& target_name) {
    std::vector<ReplacementSpan> spans;
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
        const std::size_t open = skip_spaces(content, i + 6);
        if (open >= content.size() || content[open] != '(') {
            continue;
        }
        auto close = find_define_close(content, open);
        if (!close) {
            continue;
        }
        const std::string body = content.substr(open + 1, *close - open - 1);
        auto args = split_args_with_offsets(body, open + 1);
        if (args.size() >= 2) {
            auto constant_name = parse_string_literal(args[0]);
            if (constant_name && *constant_name == target_name) {
                auto span = literal_value_span(args[1], looks_conditional(content, i));
                if (span) {
                    spans.push_back(*span);
                } else {
                    spans.push_back({0, 0, '\'', looks_conditional(content, i)});
                }
            }
        }
        i = *close;
    }
    return spans;
}

bool contains_unsupported_source(const std::string& content, const std::string& target_name) {
    const auto spans = find_target_spans(content, target_name);
    return spans.empty();
}

std::string escape_for_quote(const std::string& value, char quote) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        if (c == '\\' || c == quote) {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    return escaped;
}

WordPressConfigUpdateResult failure(std::string code, std::string message) {
    WordPressConfigUpdateResult result;
    result.success = false;
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

} // namespace

std::string wordpress_update_field_name(WordPressConfigUpdateField field) {
    switch (field) {
    case WordPressConfigUpdateField::DbName:
        return "DB_NAME";
    case WordPressConfigUpdateField::DbUser:
        return "DB_USER";
    case WordPressConfigUpdateField::DbPassword:
        return "DB_PASSWORD";
    case WordPressConfigUpdateField::DbHost:
        return "DB_HOST";
    }
    return "DB_PASSWORD";
}

WordPressConfigUpdateResult WordPressConfigUpdater::render_update(const std::string& content,
                                                                  WordPressConfigUpdateField field,
                                                                  const std::string& new_value) const {
    const std::string target_name = wordpress_update_field_name(field);
    const auto spans = find_target_spans(content, target_name);
    if (spans.empty() || contains_unsupported_source(content, target_name)) {
        return failure("unsupported_credential_source", target_name + " is missing or is not a direct string literal");
    }
    if (spans.size() != 1) {
        return failure("ambiguous_credential_source", target_name + " must be defined exactly once");
    }
    if (spans[0].conditional) {
        return failure("ambiguous_credential_source", target_name + " must not be defined inside a conditional block");
    }
    if (spans[0].value_start == 0 && spans[0].value_end == 0) {
        return failure("unsupported_credential_source", target_name + " is not a direct string literal");
    }

    WordPressConfigUpdateResult result;
    result.success = true;
    result.code = "ok";
    result.message = "WordPress credential rendered";
    result.content = content.substr(0, spans[0].value_start) + escape_for_quote(new_value, spans[0].quote) +
                     content.substr(spans[0].value_end);
    return result;
}

} // namespace containercp::wordpress
