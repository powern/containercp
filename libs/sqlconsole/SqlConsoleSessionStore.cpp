#include "sqlconsole/SqlConsoleSessionStore.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <sys/stat.h>

namespace containercp::sqlconsole {
namespace {

constexpr const char* kHeader = "# containercp-sql-console-sessions-v1";

int64_t to_epoch(std::chrono::system_clock::time_point value) {
    return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count();
}

std::chrono::system_clock::time_point from_epoch(const std::string& value) {
    try {
        return std::chrono::system_clock::time_point(std::chrono::seconds(std::stoll(value)));
    } catch (...) {
        return {};
    }
}

std::optional<std::chrono::system_clock::time_point> optional_from_epoch(const std::string& value) {
    if (value == "-") return std::nullopt;
    return from_epoch(value);
}

std::string optional_to_epoch(const std::optional<std::chrono::system_clock::time_point>& value) {
    if (!value) return "-";
    return std::to_string(to_epoch(*value));
}

std::vector<std::string> split_row(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    for (char c : line) {
        if (c == '|') {
            fields.push_back(field);
            field.clear();
        } else {
            field.push_back(c);
        }
    }
    fields.push_back(field);
    return fields;
}

} // namespace

SqlConsoleSessionStore::SqlConsoleSessionStore(std::filesystem::path path)
    : path_(std::move(path)) {
}

const std::filesystem::path& SqlConsoleSessionStore::path() const {
    return path_;
}

std::string sql_console_session_store_encode(const std::string& value) {
    std::ostringstream out;
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : value) {
        if (c == '%' || c == '|' || c == '\n' || c == '\r') {
            out << '%' << hex[c >> 4] << hex[c & 0x0f];
        } else {
            out << static_cast<char>(c);
        }
    }
    return out.str();
}

std::string sql_console_session_store_decode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    auto hex_value = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        return -1;
    };
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = hex_value(value[i + 1]);
            const int lo = hex_value(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

SqlConsoleSessionStoreResult SqlConsoleSessionStore::save(const std::vector<SqlConsoleSession>& sessions) const {
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) return {false, "directory_create_failed", "SQL Console session metadata directory could not be created"};

    const auto tmp = path_.parent_path() / (path_.filename().string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) return {false, "store_open_failed", "SQL Console session metadata file could not be opened"};
        out << kHeader << "\n";
        for (const auto& session : sessions) {
            out << sql_console_session_store_encode(session.launch_id) << '|'
                << session.database_id << '|'
                << session.site_id << '|'
                << sql_console_session_store_encode(session.admin_username) << '|'
                << sql_console_session_store_encode(session.admin_role) << '|'
                << sql_console_session_store_encode(session.provider) << '|'
                << sql_console_session_status_to_string(session.status) << '|'
                << sql_console_session_store_encode(session.database_name) << '|'
                << sql_console_session_store_encode(session.temporary_user_name) << '|'
                << to_epoch(session.created_at) << '|'
                << to_epoch(session.expires_at) << '|'
                << to_epoch(session.idle_expires_at) << '|'
                << to_epoch(session.last_seen_at) << '|'
                << optional_to_epoch(session.redeemed_at) << '|'
                << optional_to_epoch(session.revoked_at) << "\n";
        }
    }
    (void)::chmod(tmp.c_str(), S_IRUSR | S_IWUSR);
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        (void)std::filesystem::remove(tmp);
        return {false, "store_replace_failed", "SQL Console session metadata file could not be replaced"};
    }
    return {true, "stored", "SQL Console session metadata stored"};
}

std::vector<SqlConsoleSession> SqlConsoleSessionStore::load() const {
    std::vector<SqlConsoleSession> sessions;
    std::ifstream in(path_);
    if (!in.is_open()) return sessions;

    std::string line;
    if (!std::getline(in, line) || line != kHeader) return sessions;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto fields = split_row(line);
        if (fields.size() != 15) continue;
        SqlConsoleSession session;
        session.launch_id = sql_console_session_store_decode(fields[0]);
        try {
            session.database_id = static_cast<uint64_t>(std::stoull(fields[1]));
            session.site_id = static_cast<uint64_t>(std::stoull(fields[2]));
        } catch (...) {
            continue;
        }
        session.admin_username = sql_console_session_store_decode(fields[3]);
        session.admin_role = sql_console_session_store_decode(fields[4]);
        session.provider = sql_console_session_store_decode(fields[5]);
        session.status = sql_console_session_status_from_string(fields[6]);
        session.database_name = sql_console_session_store_decode(fields[7]);
        session.temporary_user_name = sql_console_session_store_decode(fields[8]);
        session.created_at = from_epoch(fields[9]);
        session.expires_at = from_epoch(fields[10]);
        session.idle_expires_at = from_epoch(fields[11]);
        session.last_seen_at = from_epoch(fields[12]);
        session.redeemed_at = optional_from_epoch(fields[13]);
        session.revoked_at = optional_from_epoch(fields[14]);
        sessions.push_back(std::move(session));
    }
    return sessions;
}

} // namespace containercp::sqlconsole
