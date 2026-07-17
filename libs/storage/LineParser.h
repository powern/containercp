#ifndef CONTAINERCP_STORAGE_LINE_PARSER_H
#define CONTAINERCP_STORAGE_LINE_PARSER_H

#include <cerrno>
#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace containercp::storage::parser {

struct LineParser {
    std::string filename;
    int line_number = 0;
    std::ifstream file;
    std::string current_line;
    bool has_error = false;
    std::string error_msg;

    LineParser(const std::filesystem::path& path, const std::string& fname)
        : filename(fname), file(path) {}

    bool next() {
        if (has_error) return false;
        if (!std::getline(file, current_line)) return false;
        ++line_number; return true;
    }
    bool empty_line() const { return current_line.empty(); }

    int count_pipes() const {
        int n = 0; for (char c : current_line) if (c == '|') ++n; return n;
    }

    std::vector<std::string> split() const {
        std::vector<std::string> fields; size_t start = 0;
        while (true) {
            size_t pos = current_line.find('|', start);
            if (pos == std::string::npos) {
                fields.push_back(current_line.substr(start)); break;
            }
            fields.push_back(current_line.substr(start, pos - start));
            start = pos + 1;
        }
        return fields;
    }

    static bool parse_uint64(const std::string& s, uint64_t& out, std::string& err) {
        if (s.empty()) { err = "empty field"; return false; }
        if (s[0] == '-') { err = "negative value"; return false; }
        errno = 0; char* end = nullptr;
        unsigned long long val = std::strtoull(s.c_str(), &end, 10);
        if (end == s.c_str()) { err = "no digits"; return false; }
        if (*end != '\0') { err = "trailing characters"; return false; }
        if (errno == ERANGE) { err = "overflow"; return false; }
        out = static_cast<uint64_t>(val); return true;
    }

    static bool parse_int(const std::string& s, int& out, std::string& err) {
        if (s.empty()) { err = "empty field"; return false; }
        errno = 0; char* end = nullptr;
        long val = std::strtol(s.c_str(), &end, 10);
        if (end == s.c_str()) { err = "no digits"; return false; }
        if (*end != '\0') { err = "trailing characters"; return false; }
        if (errno == ERANGE || val < INT_MIN || val > INT_MAX) { err = "overflow"; return false; }
        out = static_cast<int>(val); return true;
    }

    static bool parse_bool(const std::string& s, bool& out, std::string& err) {
        if (s == "1") { out = true; return true; }
        if (s == "0") { out = false; return true; }
        err = "invalid boolean (expected 0 or 1)"; return false;
    }
};

} // namespace containercp::storage::detail

#endif // CONTAINERCP_STORAGE_LINE_PARSER_H
