#include "WordPressPhpDefineScanner.h"

#include <cctype>
#include <string_view>

namespace containercp::wordpress {
namespace {

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

std::size_t skip_spaces(const std::string& content, std::size_t pos) {
    while (pos < content.size() && std::isspace(static_cast<unsigned char>(content[pos]))) {
        ++pos;
    }
    return pos;
}

bool is_control_keyword(std::string_view token) {
    static constexpr std::string_view controls[] = {
        "if", "elseif", "else", "switch", "foreach", "for", "while", "try", "catch", "function"
    };
    for (auto control : controls) {
        if (token == control) {
            return true;
        }
    }
    return false;
}

bool any_conditional_scope(const std::vector<bool>& block_stack, bool pending_control) {
    if (pending_control) {
        return true;
    }
    for (bool conditional : block_stack) {
        if (conditional) {
            return true;
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

} // namespace

std::vector<PhpDefineCall> find_php_define_calls(const std::string& content) {
    std::vector<PhpDefineCall> calls;
    bool in_single = false;
    bool in_double = false;
    bool in_line_comment = false;
    bool in_block_comment = false;
    bool pending_control = false;
    int pending_control_paren_depth = 0;
    std::vector<bool> block_stack;

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

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const std::size_t token_start = i;
            while (i < content.size() && is_identifier_char(content[i])) {
                ++i;
            }
            const std::string_view token(content.data() + token_start, i - token_start);
            if ((token_start == 0 || content[token_start - 1] != '$') && token == "define") {
                const std::size_t open = skip_spaces(content, i);
                if (open < content.size() && content[open] == '(') {
                    auto close = find_define_close(content, open);
                    if (close) {
                        calls.push_back({token_start,
                                         open + 1,
                                         *close,
                                         content.substr(open + 1, *close - open - 1),
                                         any_conditional_scope(block_stack, pending_control)});
                        i = *close;
                        continue;
                    }
                }
            }
            if ((token_start == 0 || content[token_start - 1] != '$') && is_control_keyword(token)) {
                pending_control = true;
            }
            --i;
            continue;
        }

        if (pending_control && c == '(') {
            ++pending_control_paren_depth;
        } else if (pending_control && c == ')' && pending_control_paren_depth > 0) {
            --pending_control_paren_depth;
        }

        if (c == '{') {
            block_stack.push_back(pending_control);
            pending_control = false;
            pending_control_paren_depth = 0;
        } else if (c == '}') {
            if (!block_stack.empty()) {
                block_stack.pop_back();
            }
            pending_control = false;
            pending_control_paren_depth = 0;
        } else if (c == ';' && pending_control_paren_depth == 0) {
            pending_control = false;
        }
    }
    return calls;
}

std::vector<PhpArgumentSpan> split_php_top_level_arguments(const std::string& body, std::size_t body_start) {
    std::vector<PhpArgumentSpan> args;
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

std::optional<std::string> parse_php_string_literal(const PhpArgumentSpan& arg) {
    return parse_php_string_literal(arg.text);
}

std::optional<PhpLiteralValueSpan> php_literal_value_span(const PhpArgumentSpan& arg) {
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
            return PhpLiteralValueSpan{arg.start + 1, arg.start + i, quote};
        }
    }
    return std::nullopt;
}

} // namespace containercp::wordpress
