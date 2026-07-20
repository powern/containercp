#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_PHP_DEFINE_SCANNER_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_PHP_DEFINE_SCANNER_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace containercp::wordpress {

struct PhpDefineCall {
    std::size_t offset = 0;
    std::size_t body_start = 0;
    std::size_t close_paren = 0;
    std::string body;
    bool conditional = false;
};

struct PhpArgumentSpan {
    std::size_t start = 0;
    std::size_t end = 0;
    std::string text;
};

struct PhpLiteralValueSpan {
    std::size_t value_start = 0;
    std::size_t value_end = 0;
    char quote = '\'';
};

std::vector<PhpDefineCall> find_php_define_calls(const std::string& content);
std::vector<PhpArgumentSpan> split_php_top_level_arguments(const std::string& body, std::size_t body_start);
std::optional<std::string> parse_php_string_literal(const std::string& expr);
std::optional<std::string> parse_php_string_literal(const PhpArgumentSpan& arg);
std::optional<PhpLiteralValueSpan> php_literal_value_span(const PhpArgumentSpan& arg);

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_PHP_DEFINE_SCANNER_H
