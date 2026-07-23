#include "Storage.h"
#include "LegacyArchive.h"
#include "logger/Logger.h"

#include <climits>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace containercp::storage {

namespace {
constexpr int kExpectedSchemaVersion = 2;

struct ActivationState {
    int64_t state_version = 0;
    int64_t schema_version = 0;
    std::string active_backend;
    std::string migration_id;
    std::string database_path;
    std::string archive_path;
    std::string source_version;
    std::string target_version;
    std::string activation_timestamp;
    std::string verification_result;
};

std::string parent_directory(const std::string& path) {
    auto slash = path.rfind('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

void verify_secure_owner_and_permissions(const std::string& path,
                                         const struct stat& st,
                                         const std::string& label) {
    if (st.st_uid != geteuid()) {
        throw std::runtime_error(
            label + " owner mismatch for SQLite startup path: " + path);
    }
    if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        throw std::runtime_error(
            label + " has unsafe write permissions for SQLite startup path: " + path);
    }
}

void verify_secure_directory(const std::string& path, const std::string& label) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        throw std::runtime_error(label + " not found for SQLite startup path: " + path);
    }
    if (S_ISLNK(st.st_mode)) {
        throw std::runtime_error(label + " is a symlink and is not allowed: " + path);
    }
    if (!S_ISDIR(st.st_mode)) {
        throw std::runtime_error(label + " is not a directory: " + path);
    }
    verify_secure_owner_and_permissions(path, st, label);
}

class ActivationStateParser {
public:
    explicit ActivationStateParser(const std::string& json) : json_(json) {}

    bool parse(ActivationState& state) {
        skip_ws();
        if (next() != '{') return false;

        std::set<std::string> seen;
        bool first = true;
        while (true) {
            skip_ws();
            if (peek() == '}') {
                next();
                break;
            }
            if (!first) {
                if (next() != ',') return false;
                skip_ws();
                if (peek() == '}') return false;
            }
            first = false;

            std::string key;
            if (peek() != '"' || !parse_string(key)) return false;
            if (seen.count(key) != 0) return false;
            seen.insert(key);

            skip_ws();
            if (next() != ':') return false;
            skip_ws();

            if (key == "state_version") {
                if (peek() == '"') return false;
                if (!parse_int(state.state_version) || state.state_version != 1) return false;
            } else if (key == "schema_version") {
                if (peek() == '"') return false;
                if (!parse_int(state.schema_version) || state.schema_version < 1) return false;
            } else if (key == "active_backend") {
                if (!parse_required_string(state.active_backend)) return false;
                if (state.active_backend != "sqlite") return false;
            } else if (key == "migration_id") {
                if (!parse_required_string(state.migration_id)) return false;
            } else if (key == "database_path") {
                if (!parse_required_string(state.database_path)) return false;
            } else if (key == "archive_path") {
                if (!parse_required_string(state.archive_path)) return false;
            } else if (key == "source_version") {
                if (!parse_required_string(state.source_version)) return false;
                if (!LegacyArchive::safe_version(state.source_version)) return false;
            } else if (key == "target_version") {
                if (!parse_required_string(state.target_version)) return false;
                if (!LegacyArchive::safe_version(state.target_version)) return false;
            } else if (key == "activation_timestamp") {
                if (!parse_required_string(state.activation_timestamp)) return false;
                if (!LegacyArchive::valid_timestamp(state.activation_timestamp)) return false;
            } else if (key == "verification_result") {
                if (!parse_required_string(state.verification_result)) return false;
                if (state.verification_result != "success") return false;
            } else {
                return false;
            }
        }

        skip_ws();
        if (peek() != '\0') return false;

        static const std::set<std::string> kRequired = {
            "state_version",
            "active_backend",
            "migration_id",
            "database_path",
            "archive_path",
            "source_version",
            "target_version",
            "activation_timestamp",
            "schema_version",
            "verification_result",
        };
        if (seen.size() != kRequired.size()) return false;
        for (const auto& required : kRequired) {
            if (seen.count(required) == 0) return false;
        }
        return true;
    }

private:
    const std::string& json_;
    size_t pos_ = 0;

    char peek() const { return pos_ < json_.size() ? json_[pos_] : '\0'; }
    char next() { return pos_ < json_.size() ? json_[pos_++] : '\0'; }

    void skip_ws() {
        while (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r') {
            next();
        }
    }

    bool parse_required_string(std::string& out) {
        if (peek() != '"' || !parse_string(out)) return false;
        if (out.empty()) return false;
        for (unsigned char c : out) {
            if (c < 0x20) return false;
        }
        return true;
    }

    bool parse_string(std::string& out) {
        if (next() != '"') return false;
        out.clear();
        while (peek() != '"' && peek() != '\0') {
            if (peek() == '\\') {
                next();
                switch (next()) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        char c = next();
                        if (c >= '0' && c <= '9') cp = cp * 16 + static_cast<uint32_t>(c - '0');
                        else if (c >= 'a' && c <= 'f') cp = cp * 16 + static_cast<uint32_t>(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') cp = cp * 16 + static_cast<uint32_t>(c - 'A' + 10);
                        else return false;
                    }
                    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
                    if (cp <= 0x7F) out += static_cast<char>(cp);
                    else if (cp <= 0x7FF) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: return false;
                }
            } else if (static_cast<unsigned char>(peek()) < 0x20) {
                return false;
            } else {
                out += next();
            }
        }
        return next() == '"';
    }

    bool parse_int(int64_t& out) {
        if (peek() == '\0') return false;
        bool neg = false;
        if (peek() == '-') {
            neg = true;
            next();
        }
        if (peek() < '0' || peek() > '9') return false;
        uint64_t val = 0;
        const uint64_t max = neg ? static_cast<uint64_t>(INT64_MAX) + 1U : static_cast<uint64_t>(INT64_MAX);
        if (peek() == '0') {
            next();
        } else {
            while (peek() >= '0' && peek() <= '9') {
                uint64_t digit = static_cast<uint64_t>(next() - '0');
                if (val > max / 10U) return false;
                uint64_t next_val = val * 10U + digit;
                if (next_val < val || next_val > max) return false;
                val = next_val;
            }
        }
        if (peek() >= '0' && peek() <= '9') return false;
        if (neg) {
            if (val == static_cast<uint64_t>(INT64_MAX) + 1U) out = INT64_MIN;
            else out = -static_cast<int64_t>(val);
        } else {
            out = static_cast<int64_t>(val);
        }
        return true;
    }
};

bool parse_activation_state(const std::string& content, ActivationState& state) {
    ActivationStateParser parser(content);
    return parser.parse(state);
}

void validate_activation_state_consistency(const ActivationState& state,
                                           const std::string& sqlite_path) {
    if (state.database_path != sqlite_path) {
        throw std::runtime_error(
            "Activation state database_path='" + state.database_path
            + "' does not match expected '" + sqlite_path + "'");
    }

    if (!LegacyArchive::valid_migration_id(state.migration_id)) {
        throw std::runtime_error(
            "Activation state migration_id is invalid: " + state.migration_id);
    }

    if (state.schema_version != kExpectedSchemaVersion) {
        throw std::runtime_error(
            "Activation state schema_version='" + std::to_string(state.schema_version)
            + "' does not match expected '" + std::to_string(kExpectedSchemaVersion) + "'");
    }

    std::string archive_path = LegacyArchive::normalize_archive_identity_path(state.archive_path);
    if (archive_path.empty()) {
        throw std::runtime_error(
            "Activation state archive_path is invalid: " + state.archive_path);
    }
    struct stat archive_st;
    if (lstat(archive_path.c_str(), &archive_st) != 0) {
        throw std::runtime_error(
            "Activation state archive is missing, invalid, or inconsistent: " + state.archive_path);
    }
    verify_secure_directory(archive_path, "Activation state archive path");

    LegacyArchive archive("", "");
    ArchiveManifest manifest;
    if (!archive.verify_archive(archive_path, &manifest)) {
        throw std::runtime_error(
            "Activation state archive is missing, invalid, or inconsistent: " + state.archive_path);
    }

    if (manifest.archive_directory != archive_path) {
        throw std::runtime_error(
            "Activation state archive_path='" + state.archive_path
            + "' does not match archive manifest path '" + manifest.archive_directory + "'");
    }
    if (manifest.migration_id != state.migration_id) {
        throw std::runtime_error(
            "Activation state migration_id='" + state.migration_id
            + "' does not match archive migration_id '" + manifest.migration_id + "'");
    }
    if (manifest.source_version != state.source_version) {
        throw std::runtime_error(
            "Activation state source_version='" + state.source_version
            + "' does not match archive source_version '" + manifest.source_version + "'");
    }
    if (manifest.target_version != state.target_version) {
        throw std::runtime_error(
            "Activation state target_version='" + state.target_version
            + "' does not match archive target_version '" + manifest.target_version + "'");
    }
    if (manifest.verification_result != state.verification_result || state.verification_result != "success") {
        throw std::runtime_error(
            "Activation state verification_result does not represent a completed migration");
    }
}
}

Storage::Storage(const std::string& db_path, StorageOptions options)
    : db_path_(db_path)
    , options_(options)
    , pool_()
    , sqlite_(pool_)
{
    ::mkdir(db_path_.c_str(), 0755);

    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        std::string sqlite_path = sqlite_db_path();
        auto& log = logger::Logger::instance();
        log.info("STORAGE", "SQLite backend selected: " + sqlite_path
            + (options_.skip_startup_validation ? " (startup validation skipped)" : ""));

        try {
            if (!options_.skip_startup_validation) {
                validate_activation_state(sqlite_path);
                verify_sqlite_file(sqlite_path);
            }

            sqlite_ready_ = pool_.initialize(sqlite_path);
            if (!sqlite_ready_) {
                pool_.shutdown();
                if (options_.skip_startup_validation) {
                    log.warning("STORAGE", "SQLite backend not ready while startup validation is skipped: " + sqlite_path);
                    return;
                }
                throw std::runtime_error(
                    "Failed to initialize SQLite database: " + sqlite_path);
            }
            try {
                verify_sqlite_schema_version();
            } catch (const std::exception&) {
                if (options_.skip_startup_validation) {
                    pool_.shutdown();
                    sqlite_ready_ = false;
                    log.warning("STORAGE", "SQLite backend schema validation failed while startup validation is skipped: " + sqlite_path);
                    return;
                }
                throw;
            }
            if (!options_.skip_startup_validation) {
                verify_sqlite_startup();
                log.info("STORAGE", "SQLite startup validation passed: " + sqlite_path);
            }
            sqlite_ready_ = true;
            log.info("STORAGE", "SQLite backend ready: " + sqlite_path);
        } catch (const std::exception& e) {
            pool_.shutdown();
            sqlite_ready_ = false;
            log.error("STORAGE", "SQLite backend startup failed: " + std::string(e.what()));
            throw;
        }
    }
}

void Storage::validate_activation_state(const std::string& sqlite_path) {
    // Construct state path: parent_dir/storage-state.json
    std::string state_path;
    auto slash = sqlite_path.rfind('/');
    if (slash != std::string::npos) {
        state_path = sqlite_path.substr(0, slash) + "/storage-state.json";
    } else {
        state_path = "storage-state.json";
    }
    verify_secure_directory(parent_directory(sqlite_path), "SQLite storage directory");

    struct stat state_st;
    if (lstat(state_path.c_str(), &state_st) != 0) {
        throw std::runtime_error(
            "SQLite backend selected but activation state file not found: " + state_path
            + ". Run 'containercp storage migrate-to-sqlite' first.");
    }
    if (S_ISLNK(state_st.st_mode)) {
        throw std::runtime_error(
            "Activation state path is a symlink and is not allowed: " + state_path);
    }
    if (!S_ISREG(state_st.st_mode)) {
        throw std::runtime_error(
            "Activation state path is not a regular file: " + state_path);
    }
    verify_secure_owner_and_permissions(state_path, state_st, "Activation state path");

    std::ifstream f(state_path);
    if (!f.is_open()) {
        throw std::runtime_error(
            "SQLite backend selected but activation state file not found: " + state_path
            + ". Run 'containercp storage migrate-to-sqlite' first.");
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    f.close();

    ActivationState state;
    if (!parse_activation_state(content, state)) {
        throw std::runtime_error(
            "Activation state file is malformed or invalid: " + state_path);
    }

    validate_activation_state_consistency(state, sqlite_path);
}

void Storage::verify_sqlite_file(const std::string& sqlite_path) {
    verify_secure_directory(parent_directory(sqlite_path), "SQLite storage directory");

    struct stat st;
    if (lstat(sqlite_path.c_str(), &st) != 0) {
        throw std::runtime_error(
            "SQLite database file not found: " + sqlite_path + ": " + std::strerror(errno));
    }
    if (S_ISLNK(st.st_mode)) {
        throw std::runtime_error(
            "SQLite database path is a symlink and is not allowed: " + sqlite_path);
    }
    if (!S_ISREG(st.st_mode)) {
        throw std::runtime_error(
            "SQLite database path is not a regular file: " + sqlite_path);
    }
    verify_secure_owner_and_permissions(sqlite_path, st, "SQLite database path");
}

void Storage::verify_sqlite_schema_version() {
    ReadLease rl(pool_);
    if (!rl.is_valid()) {
        throw std::runtime_error("Startup validation: failed to acquire read lease for schema validation");
    }

    if (!rl->prepare("SELECT value FROM storage_meta WHERE key = 'schema_version'")) {
        throw std::runtime_error(
            "Startup validation: failed to query storage schema_version: " + rl->error_message());
    }
    if (!rl->step()) {
        throw std::runtime_error("Startup validation: storage schema_version is missing");
    }
    std::string schema_version = rl->column_text(0);
    if (schema_version != std::to_string(kExpectedSchemaVersion)) {
        throw std::runtime_error(
            "Startup validation: unsupported storage schema_version '" + schema_version
            + "' (expected '" + std::to_string(kExpectedSchemaVersion) + "')");
    }

    if (!rl->prepare("SELECT status FROM schema_migrations WHERE version = ?")) {
        throw std::runtime_error(
            "Startup validation: failed to query schema_migrations: " + rl->error_message());
    }
    if (!rl->bind_int(1, kExpectedSchemaVersion)) {
        throw std::runtime_error(
            "Startup validation: failed to bind schema version: " + rl->error_message());
    }
    if (!rl->step()) {
        throw std::runtime_error(
            "Startup validation: schema migration " + std::to_string(kExpectedSchemaVersion)
            + " is not recorded as completed");
    }
    std::string status = rl->column_text(0);
    if (status != "completed") {
        throw std::runtime_error(
            "Startup validation: schema migration " + std::to_string(kExpectedSchemaVersion)
            + " has status '" + status + "' (expected 'completed')");
    }

    if (!rl->prepare("SELECT COUNT(*) FROM schema_migrations WHERE status = 'completed' AND version > ?")) {
        throw std::runtime_error(
            "Startup validation: failed to query newer schema migrations: " + rl->error_message());
    }
    if (!rl->bind_int(1, kExpectedSchemaVersion)) {
        throw std::runtime_error(
            "Startup validation: failed to bind expected schema version: " + rl->error_message());
    }
    if (!rl->step()) {
        throw std::runtime_error("Startup validation: failed to read newer schema migration count");
    }
    if (rl->column_int(0) != 0) {
        throw std::runtime_error("Startup validation: database schema is newer than this runtime supports");
    }
}

void Storage::verify_sqlite_startup() {
    ReadLease rl(pool_);
    if (!rl.is_valid()) {
        throw std::runtime_error("Failed to acquire read lease for startup validation");
    }

    // Verify foreign_keys = ON
    if (!rl->prepare("PRAGMA foreign_keys")) {
        throw std::runtime_error(
            "Startup validation: failed to query foreign_keys: " + rl->error_message());
    }
    if (!rl->step()) {
        throw std::runtime_error(
            "Startup validation: no result from PRAGMA foreign_keys");
    }
    if (rl->column_int(0) != 1) {
        throw std::runtime_error(
            "Startup validation: PRAGMA foreign_keys is OFF (expected ON)");
    }

    // Verify journal_mode = WAL
    if (!rl->prepare("PRAGMA journal_mode")) {
        throw std::runtime_error(
            "Startup validation: failed to query journal_mode: " + rl->error_message());
    }
    if (!rl->step()) {
        throw std::runtime_error(
            "Startup validation: no result from PRAGMA journal_mode");
    }
    std::string jm = rl->column_text(0);
    if (jm != "wal") {
        throw std::runtime_error(
            "Startup validation: journal_mode is '" + jm + "' (expected 'wal')");
    }

    // Verify synchronous = FULL
    if (!rl->prepare("PRAGMA synchronous")) {
        throw std::runtime_error(
            "Startup validation: failed to query synchronous: " + rl->error_message());
    }
    if (!rl->step()) {
        throw std::runtime_error(
            "Startup validation: no result from PRAGMA synchronous");
    }
    if (rl->column_int(0) != 2) {
        int sync_val = static_cast<int>(rl->column_int(0));
        throw std::runtime_error(
            "Startup validation: synchronous is " + std::to_string(sync_val)
            + " (expected 2=FULL)");
    }

    // Run integrity_check
    if (!rl->prepare("PRAGMA integrity_check")) {
        throw std::runtime_error(
            "Startup validation: failed to run integrity_check: " + rl->error_message());
    }
    if (!rl->step()) {
        throw std::runtime_error(
            "Startup validation: no result from integrity_check");
    }
    std::string integrity = rl->column_text(0);
    if (integrity != "ok") {
        throw std::runtime_error(
            "Startup validation: integrity_check failed: " + integrity);
    }

    // Run foreign_key_check
    if (!rl->prepare("PRAGMA foreign_key_check")) {
        throw std::runtime_error(
            "Startup validation: failed to run foreign_key_check: " + rl->error_message());
    }
    std::vector<std::string> fk_violations;
    while (true) {
        if (!rl->step()) {
            if (rl->error_code() != 0) {
                throw std::runtime_error(
                    "Startup validation: foreign_key_check step failed: " + rl->error_message());
            }
            break;
        }
        fk_violations.push_back(
            std::string("table=") + rl->column_text(0)
            + " rowid=" + std::to_string(rl->column_int(1)));
    }
    if (!fk_violations.empty()) {
        std::string msg = "Startup validation: foreign_key_check found violations:";
        for (const auto& v : fk_violations) {
            msg += "\n  " + v;
        }
        throw std::runtime_error(msg);
    }
}

bool Storage::use_sqlite() const {
    return options_.core_backend == CoreStorageBackend::SqlitePhase5 && sqlite_ready_;
}

bool Storage::sqlite_ready() const {
    return sqlite_ready_;
}

std::string Storage::nodes_file() const {
    return db_path_ + "nodes.db";
}

std::string Storage::sites_file() const {
    return db_path_ + "sites.db";
}

std::string Storage::users_file() const {
    return db_path_ + "users.db";
}

std::string Storage::domains_file() const {
    return db_path_ + "domains.db";
}

std::string Storage::php_versions_file() const {
    return db_path_ + "php_versions.db";
}

std::string Storage::databases_file() const {
    return db_path_ + "databases.db";
}

std::string Storage::backups_file() const {
    return db_path_ + "backups.db";
}

std::string Storage::ssl_certificates_file() const {
    return db_path_ + "ssl_certificates.db";
}

std::string Storage::mail_domains_file() const {
    return db_path_ + "mail_domains.db";
}

std::string Storage::mailboxes_file() const {
    return db_path_ + "mail_mailboxes.db";
}

std::string Storage::mail_aliases_file() const {
    return db_path_ + "mail_aliases.db";
}

std::string Storage::mail_state_file() const {
    return db_path_ + "mail_state.db";
}

std::string Storage::mail_smarthost_file() const {
    return db_path_ + "mail_smarthost.db";
}

std::string Storage::access_users_file() const {
    return db_path_ + "access_users.db";
}

std::string Storage::auth_users_file() const {
    return db_path_ + "auth_users.db";
}

std::string Storage::access_grants_file() const {
    return db_path_ + "access_grants.db";
}

std::string Storage::reverse_proxies_file() const {
    return db_path_ + "reverse_proxies.db";
}

void Storage::save_nodes(const std::vector<node::Node>& nodes) {
    if (use_sqlite()) {
        sqlite_.save_nodes(nodes);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;  // explicit mode but SQLite unavailable — no-op, no TXT fallback
    }
    std::ofstream file(nodes_file());
    for (const auto& n : nodes) {
        file << n.id << "|" << n.name << "|" << n.type << "\n";
    }
}

std::vector<node::Node> Storage::load_nodes() {
    if (use_sqlite()) {
        return sqlite_.load_nodes();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};  // explicit mode but SQLite unavailable — return empty
    }
    std::vector<node::Node> nodes;
    std::ifstream file(nodes_file());
    if (!file.is_open()) return nodes;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        node::Node n;
        if (std::getline(ss, token, '|')) n.id = std::stoull(token);
        if (std::getline(ss, token, '|')) n.name = token;
        if (std::getline(ss, token, '|')) n.type = token;
        nodes.push_back(std::move(n));
    }
    return nodes;
}

void Storage::save_sites(const std::vector<site::Site>& sites) {
    if (use_sqlite()) {
        sqlite_.save_sites(sites);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(sites_file());
    for (const auto& s : sites) {
        file << s.id << "|" << s.domain << "|" << s.owner << "|"
             << s.node_id << "|" << s.web_server << "|"
             << (s.php_mail_enabled ? "1" : "0") << "|"
             << s.web_template_profile << "\n";
    }
}

std::vector<site::Site> Storage::load_sites() {
    if (use_sqlite()) {
        return sqlite_.load_sites();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<site::Site> sites;
    std::ifstream file(sites_file());
    if (!file.is_open()) {
        return sites;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        int pipes = 0;
        for (char c : line) if (c == '|') pipes++;

        std::istringstream ss(line);
        std::string token;
        site::Site s;
        if (std::getline(ss, token, '|')) s.id = std::stoull(token);
        if (std::getline(ss, token, '|')) s.domain = token;
        if (std::getline(ss, token, '|')) s.owner = token;
        if (std::getline(ss, token, '|')) s.node_id = std::stoull(token);
        if (std::getline(ss, token, '|')) s.web_server = token.empty() ? "apache" : token;
        if (std::getline(ss, token, '|')) s.php_mail_enabled = (token == "1");
        if (std::getline(ss, token, '|')) s.web_template_profile = token;

        s.php_mail_enabled_present = (pipes >= 5);
        s.name = s.domain;
        sites.push_back(std::move(s));
    }
    return sites;
}

void Storage::save_users(const std::vector<user::User>& users) {
    if (use_sqlite()) {
        sqlite_.save_users(users);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(users_file());
    for (const auto& u : users) {
        file << u.id << "|" << u.username << "|" << u.uid << "|"
             << u.home_directory << "|" << u.shell << "|" << (u.enabled ? "1" : "0") << "\n";
    }
}

std::vector<user::User> Storage::load_users() {
    if (use_sqlite()) {
        return sqlite_.load_users();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<user::User> users;
    std::ifstream file(users_file());
    if (!file.is_open()) {
        return users;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        user::User u;
        if (std::getline(ss, token, '|')) u.id = std::stoull(token);
        if (std::getline(ss, token, '|')) u.username = token;
        if (std::getline(ss, token, '|')) u.uid = std::stoull(token);
        if (std::getline(ss, token, '|')) u.home_directory = token;
        if (std::getline(ss, token, '|')) u.shell = token;
        if (std::getline(ss, token, '|')) u.enabled = (token == "1");
        u.name = u.username;
        users.push_back(std::move(u));
    }
    return users;
}

void Storage::save_domains(const std::vector<domain::Domain>& domains) {
    if (use_sqlite()) {
        sqlite_.save_domains(domains);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(domains_file());
    for (const auto& d : domains) {
        file << d.id << "|" << d.fqdn << "|" << d.owner_id << "|"
             << d.site_id << "|" << d.php_version << "|"
             << (d.ssl_enabled ? "1" : "0") << "|" << (d.enabled ? "1" : "0") << "\n";
    }
}

std::vector<domain::Domain> Storage::load_domains() {
    if (use_sqlite()) {
        return sqlite_.load_domains();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<domain::Domain> domains;
    std::ifstream file(domains_file());
    if (!file.is_open()) {
        return domains;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        domain::Domain d;
        if (std::getline(ss, token, '|')) d.id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.fqdn = token;
        if (std::getline(ss, token, '|')) d.owner_id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.php_version = token;
        if (std::getline(ss, token, '|')) d.ssl_enabled = (token == "1");
        if (std::getline(ss, token, '|')) d.enabled = (token == "1");
        d.name = d.fqdn;
        domains.push_back(std::move(d));
    }
    return domains;
}

void Storage::save_php_versions(const std::vector<php::PhpVersion>& versions) {
    if (use_sqlite()) {
        sqlite_.save_php_versions(versions);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(php_versions_file());
    for (const auto& pv : versions) {
        file << pv.id << "|" << pv.version << "|" << pv.image << "|"
             << (pv.enabled ? "1" : "0") << "|" << (pv.default_version ? "1" : "0") << "\n";
    }
}

std::vector<php::PhpVersion> Storage::load_php_versions() {
    if (use_sqlite()) {
        return sqlite_.load_php_versions();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<php::PhpVersion> versions;
    std::ifstream file(php_versions_file());
    if (!file.is_open()) return versions;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        php::PhpVersion pv;
        if (std::getline(ss, token, '|')) pv.id = std::stoull(token);
        if (std::getline(ss, token, '|')) pv.version = token;
        if (std::getline(ss, token, '|')) pv.image = token;
        if (std::getline(ss, token, '|')) pv.enabled = (token == "1");
        if (std::getline(ss, token, '|')) pv.default_version = (token == "1");
        pv.name = pv.version;
        versions.push_back(std::move(pv));
    }
    return versions;
}

void Storage::save_databases(const std::vector<database::Database>& databases) {
    if (use_sqlite()) {
        sqlite_.save_databases(databases);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(databases_file());
    for (const auto& d : databases) {
        file << d.id << "|" << d.db_name << "|" << d.db_user << "|" << d.db_password << "|"
             << d.engine << "|" << d.version << "|" << d.owner_id << "|" << d.site_id << "|"
             << (d.enabled ? "1" : "0") << "\n";
    }
}

std::vector<database::Database> Storage::load_databases() {
    if (use_sqlite()) {
        return sqlite_.load_databases();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<database::Database> databases;
    std::ifstream file(databases_file());
    if (!file.is_open()) {
        return databases;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        database::Database d;
        if (std::getline(ss, token, '|')) d.id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.db_name = token;
        if (std::getline(ss, token, '|')) d.db_user = token;
        if (std::getline(ss, token, '|')) d.db_password = token;
        if (std::getline(ss, token, '|')) d.engine = token;
        if (std::getline(ss, token, '|')) d.version = token;
        if (std::getline(ss, token, '|')) d.owner_id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.enabled = (token == "1");
        d.name = d.db_name;
        databases.push_back(std::move(d));
    }
    return databases;
}

void Storage::save_backups(const std::vector<backup::Backup>& backups) {
    if (use_sqlite()) {
        sqlite_.save_backups(backups);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(backups_file());
    for (const auto& b : backups) {
        file << b.id << "|" << b.site_id << "|" << b.owner_id << "|"
             << b.filename << "|" << b.type << "|" << b.size << "|"
             << b.created_at << "|" << b.status << "|"
             << b.file_path << "|" << b.compression << "\n";
    }
}

std::vector<backup::Backup> Storage::load_backups() {
    if (use_sqlite()) {
        return sqlite_.load_backups();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<backup::Backup> backups;
    std::ifstream file(backups_file());
    if (!file.is_open()) {
        return backups;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        backup::Backup b;
        if (std::getline(ss, token, '|')) b.id = std::stoull(token);
        if (std::getline(ss, token, '|')) b.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) b.owner_id = std::stoull(token);
        if (std::getline(ss, token, '|')) b.filename = token;
        if (std::getline(ss, token, '|')) b.type = token;
        if (std::getline(ss, token, '|')) b.size = std::stoull(token);
        if (std::getline(ss, token, '|')) b.created_at = token;
        if (std::getline(ss, token, '|')) b.status = token;
        if (std::getline(ss, token, '|')) b.file_path = token;
        if (std::getline(ss, token, '|')) b.compression = token;
        b.name = b.filename;
        backups.push_back(std::move(b));
    }
    return backups;
}

void Storage::save_ssl_certificates(const std::vector<ssl::SslCertificate>& certs) {
    if (use_sqlite()) { sqlite_.save_ssl_certificates(certs); return; }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return;
    std::ofstream file(ssl_certificates_file());
    for (const auto& c : certs) {
        file << c.id << "|" << c.domain_id << "|" << c.domain << "|"
             << c.provider << "|" << c.certificate_path << "|" << c.key_path << "|"
             << c.chain_path << "|" << c.issued_at << "|" << c.expires_at << "|"
             << c.renew_after << "|" << c.status << "|" << (c.auto_renew ? "1" : "0") << "|"
             << (c.https_enabled ? "1" : "0") << "|" << (c.redirect_enabled ? "1" : "0") << "|"
             << c.domains << "|" << c.challenge_type << "|" << c.last_error << "|"
             << c.last_validation << "|" << c.renew_attempts << "|" << c.version << "\n";
    }
}

std::vector<ssl::SslCertificate> Storage::load_ssl_certificates() {
    if (use_sqlite()) return sqlite_.load_ssl_certificates();
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return {};
    std::vector<ssl::SslCertificate> certs;
    std::ifstream file(ssl_certificates_file());
    if (!file.is_open()) {
        return certs;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        ssl::SslCertificate c;

        // Common fields for all formats
        if (std::getline(ss, token, '|')) c.id = std::stoull(token);
        if (std::getline(ss, token, '|')) c.domain_id = std::stoull(token);
        if (std::getline(ss, token, '|')) c.domain = token;
        if (std::getline(ss, token, '|')) c.provider = token;
        if (std::getline(ss, token, '|')) c.certificate_path = token;
        if (std::getline(ss, token, '|')) c.key_path = token;

        // Detect format version by counting remaining fields
        // Old format (v0.5-rc2): expires_at|status|enabled|auto_renew (4 more fields)
        // New format (v0.5 SSL): chain_path|issued_at|expires_at|renew_after|status|... (14 more fields)
        std::string rest;
        std::getline(ss, rest);

        std::vector<std::string> fields;
        std::istringstream rest_ss(rest);
        std::string f;
        while (std::getline(rest_ss, f, '|')) {
            fields.push_back(f);
        }

        if (fields.size() >= 10) {
            // New format
            size_t i = 0;
            if (i < fields.size()) c.chain_path = fields[i++];
            if (i < fields.size()) c.issued_at = fields[i++];
            if (i < fields.size()) c.expires_at = fields[i++];
            if (i < fields.size()) c.renew_after = fields[i++];
            if (i < fields.size()) c.status = fields[i++];
            if (i < fields.size()) c.auto_renew = (fields[i++] == "1");
            if (i < fields.size()) c.https_enabled = (fields[i++] == "1");
            if (i < fields.size()) c.redirect_enabled = (fields[i++] == "1");
            if (i < fields.size()) c.domains = fields[i++];
            if (i < fields.size()) c.challenge_type = fields[i++];
            if (i < fields.size()) c.last_error = fields[i++];
            if (i < fields.size()) c.last_validation = fields[i++];
            if (i < fields.size()) c.renew_attempts = std::stoi(fields[i++]);
            if (i < fields.size()) c.version = std::stoi(fields[i++]);
        } else {
            // Old format: expires_at|status|enabled|auto_renew
            size_t i = 0;
            if (i < fields.size()) c.expires_at = fields[i++];
            if (i < fields.size()) c.status = fields[i++];
            if (i < fields.size()) c.https_enabled = (fields[i++] == "1");
            if (i < fields.size()) c.auto_renew = (fields[i++] == "1");
            c.version = 0; // indicates old format
        }

        c.name = c.domain;
        certs.push_back(std::move(c));
    }
    return certs;
}

void Storage::save_mail_domains(const std::vector<mail::MailDomain>& domains) {
    if (use_sqlite()) { sqlite_.save_mail_domains(domains); return; }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return;
    std::ofstream file(mail_domains_file());
    for (const auto& m : domains) {
        file << m.id << "|"
             << mail::mail_domain_mode_to_string(m.mode) << "|"
             << m.domain_name << "|"
             << m.domain_id << "|"
             << m.site_id << "|"
             << (m.enabled ? "1" : "0") << "|"
             << m.catch_all << "|"
             << m.dkim_selector << "|"
             << m.dkim_public_key_dns << "|"
             << m.relay_host << "|"
             << m.max_mailboxes << "|"
             << m.max_aliases << "\n";
    }
}

std::vector<mail::MailDomain> Storage::load_mail_domains() {
    if (use_sqlite()) return sqlite_.load_mail_domains();
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return {};
    std::vector<mail::MailDomain> domains;
    std::ifstream file(mail_domains_file());
    if (!file.is_open()) {
        return domains;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        mail::MailDomain m;

        // Count pipes to detect old format (pre-site_id/pre-dkim_public_key_dns)
        int pipes = 0;
        for (char c : line) { if (c == '|') ++pipes; }

        if (std::getline(ss, token, '|')) m.id = std::stoull(token);
        if (std::getline(ss, token, '|')) m.mode = mail::mail_domain_mode_from_string(token);
        if (std::getline(ss, token, '|')) m.domain_name = token;

        if (pipes <= 9) {
            // Old format (10 fields): id|mode|domain_name|owner_id|enabled|catch_all|dkim_selector|relay_host|max_mailboxes|max_aliases
            // Map owner_id → domain_id, shift remaining fields
            if (std::getline(ss, token, '|')) m.domain_id = token.empty() ? 0 : std::stoull(token);
            if (std::getline(ss, token, '|')) m.enabled = (token == "1");
            if (std::getline(ss, token, '|')) m.catch_all = token;
            if (std::getline(ss, token, '|')) m.dkim_selector = token;
            if (std::getline(ss, token, '|')) m.relay_host = token;
            if (std::getline(ss, token, '|')) m.max_mailboxes = token.empty() ? 0 : std::stoull(token);
            if (std::getline(ss, token, '|')) m.max_aliases = token.empty() ? 0 : std::stoull(token);
        } else {
            // Current format
            if (std::getline(ss, token, '|')) m.domain_id = token.empty() ? 0 : std::stoull(token);
            if (std::getline(ss, token, '|')) m.site_id = token.empty() ? 0 : std::stoull(token);
            if (std::getline(ss, token, '|')) m.enabled = (token == "1");
            if (std::getline(ss, token, '|')) m.catch_all = token;
            if (std::getline(ss, token, '|')) m.dkim_selector = token;
            if (std::getline(ss, token, '|')) m.dkim_public_key_dns = token;
            if (std::getline(ss, token, '|')) m.relay_host = token;
            if (std::getline(ss, token, '|')) m.max_mailboxes = token.empty() ? 0 : std::stoull(token);
            if (std::getline(ss, token, '|')) m.max_aliases = token.empty() ? 0 : std::stoull(token);
        }
        m.name = m.domain_name;
        domains.push_back(std::move(m));
    }
    return domains;
}

void Storage::save_mailboxes(const std::vector<mail::Mailbox>& mailboxes) {
    if (use_sqlite()) { sqlite_.save_mailboxes(mailboxes); return; }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return;
    std::ofstream file(mailboxes_file());
    for (const auto& mb : mailboxes) {
        file << mb.id << "|"
             << mb.domain_id << "|"
             << mb.local_part << "|"
             << mb.password_hash << "|"
             << mb.quota_bytes << "|"
             << mb.quota_messages << "|"
             << (mb.enabled ? "1" : "0") << "|"
             << mb.display_name << "|"
             << mb.forward_to << "|"
             << (mb.spam_enabled ? "1" : "0") << "|"
             << mb.last_login << "|"
             << mb.created_at << "|"
             << mb.updated_at << "\n";
    }
}

std::vector<mail::Mailbox> Storage::load_mailboxes() {
    if (use_sqlite()) return sqlite_.load_mailboxes();
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return {};
    std::vector<mail::Mailbox> mailboxes;
    std::ifstream file(mailboxes_file());
    if (!file.is_open()) {
        return mailboxes;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        mail::Mailbox mb;
        if (std::getline(ss, token, '|')) mb.id = std::stoull(token);
        if (std::getline(ss, token, '|')) mb.domain_id = std::stoull(token);
        if (std::getline(ss, token, '|')) mb.local_part = token;
        if (std::getline(ss, token, '|')) mb.password_hash = token;
        if (std::getline(ss, token, '|')) mb.quota_bytes = token.empty() ? 0 : std::stoull(token);
        if (std::getline(ss, token, '|')) mb.quota_messages = token.empty() ? 0 : std::stoull(token);
        if (std::getline(ss, token, '|')) mb.enabled = (token == "1");
        if (std::getline(ss, token, '|')) mb.display_name = token;
        if (std::getline(ss, token, '|')) mb.forward_to = token;
        if (std::getline(ss, token, '|')) mb.spam_enabled = (token == "1");
        if (std::getline(ss, token, '|')) mb.last_login = token;
        if (std::getline(ss, token, '|')) mb.created_at = token;
        if (std::getline(ss, token, '|')) mb.updated_at = token;
        mb.name = mb.local_part;
        mailboxes.push_back(std::move(mb));
    }
    return mailboxes;
}

void Storage::save_mail_aliases(const std::vector<mail::MailAlias>& aliases) {
    if (use_sqlite()) { sqlite_.save_mail_aliases(aliases); return; }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return;
    std::ofstream file(mail_aliases_file());
    for (const auto& a : aliases) {
        file << a.id << "|"
             << a.domain_id << "|"
             << a.source_local_part << "|"
             << a.destination << "|"
             << (a.enabled ? "1" : "0") << "|"
             << a.created_at << "|"
             << a.updated_at << "\n";
    }
}

std::vector<mail::MailAlias> Storage::load_mail_aliases() {
    if (use_sqlite()) return sqlite_.load_mail_aliases();
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return {};
    std::vector<mail::MailAlias> aliases;
    std::ifstream file(mail_aliases_file());
    if (!file.is_open()) {
        return aliases;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        mail::MailAlias a;
        if (std::getline(ss, token, '|')) a.id = std::stoull(token);
        if (std::getline(ss, token, '|')) a.domain_id = std::stoull(token);
        if (std::getline(ss, token, '|')) a.source_local_part = token;
        if (std::getline(ss, token, '|')) a.destination = token;
        if (std::getline(ss, token, '|')) a.enabled = (token == "1");
        if (std::getline(ss, token, '|')) a.created_at = token;
        if (std::getline(ss, token, '|')) a.updated_at = token;
        a.name = a.source_local_part;
        aliases.push_back(std::move(a));
    }
    return aliases;
}

void Storage::save_mail_module_state(const std::string& state) {
    if (use_sqlite()) { sqlite_.save_mail_module_state(state); return; }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return;
    std::ofstream file(mail_state_file());
    file << state << "\n";
}

std::string Storage::load_mail_module_state() {
    if (use_sqlite()) return sqlite_.load_mail_module_state();
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return {};
    std::ifstream file(mail_state_file());
    std::string state;
    if (file.is_open()) {
        std::getline(file, state);
    }
    return state;
}

void Storage::save_mail_smarthost(const std::string& config) {
    if (use_sqlite()) { sqlite_.save_mail_smarthost(config); return; }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return;
    std::ofstream file(mail_smarthost_file());
    file << config << "\n";
}

std::string Storage::load_mail_smarthost() {
    if (use_sqlite()) return sqlite_.load_mail_smarthost();
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) return {};
    std::ifstream file(mail_smarthost_file());
    std::string config;
    if (file.is_open()) {
        std::getline(file, config);
    }
    return config;
}

void Storage::save_access_users(const std::vector<access::AccessUser>& users) {
    if (use_sqlite()) {
        sqlite_.save_access_users(users);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(access_users_file());
    for (const auto& u : users) {
        file << u.id << "|" << u.username << "|" << u.auth_type << "|"
             << u.password_hash << "|" << (u.enabled ? "1" : "0") << "\n";
    }
}

std::vector<access::AccessUser> Storage::load_access_users() {
    if (use_sqlite()) {
        return sqlite_.load_access_users();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<access::AccessUser> users;
    std::ifstream file(access_users_file());
    if (!file.is_open()) {
        return users;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        access::AccessUser u;
        if (std::getline(ss, token, '|')) u.id = std::stoull(token);
        if (std::getline(ss, token, '|')) u.username = token;
        if (std::getline(ss, token, '|')) u.auth_type = token;
        if (std::getline(ss, token, '|')) u.password_hash = token;
        if (std::getline(ss, token, '|')) u.enabled = (token == "1");
        u.name = u.username;
        users.push_back(std::move(u));
    }
    return users;
}

void Storage::save_auth_users(const std::vector<auth::AuthUser>& users) {
    if (use_sqlite()) {
        sqlite_.save_auth_users(users);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(auth_users_file());
    for (const auto& u : users) {
        file << u.id << "|" << u.username << "|" << u.password_hash << "|"
             << (u.must_change_password ? "1" : "0") << "|"
             << (u.enabled ? "1" : "0") << "|" << u.role << "\n";
    }
}

std::vector<auth::AuthUser> Storage::load_auth_users() {
    if (use_sqlite()) {
        return sqlite_.load_auth_users();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<auth::AuthUser> users;
    std::ifstream file(auth_users_file());
    if (!file.is_open()) {
        return users;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        auth::AuthUser u;
        if (std::getline(ss, token, '|')) u.id = std::stoull(token);
        if (std::getline(ss, token, '|')) u.username = token;
        if (std::getline(ss, token, '|')) u.password_hash = token;
        if (std::getline(ss, token, '|')) u.must_change_password = (token == "1");
        if (std::getline(ss, token, '|')) u.enabled = (token == "1");
        if (std::getline(ss, token, '|')) u.role = token;
        u.name = u.username;
        users.push_back(std::move(u));
    }
    return users;
}

void Storage::save_access_grants(const std::vector<access::AccessGrant>& grants) {
    if (use_sqlite()) {
        sqlite_.save_access_grants(grants);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(access_grants_file());
    for (const auto& g : grants) {
        file << g.id << "|" << g.access_user_id << "|" << g.site_id << "|"
             << access::permission_to_string(g.permission) << "\n";
    }
}

std::vector<access::AccessGrant> Storage::load_access_grants() {
    if (use_sqlite()) {
        return sqlite_.load_access_grants();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<access::AccessGrant> grants;
    std::ifstream file(access_grants_file());
    if (!file.is_open()) {
        return grants;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        access::AccessGrant g;
        if (std::getline(ss, token, '|')) g.id = std::stoull(token);
        if (std::getline(ss, token, '|')) g.access_user_id = std::stoull(token);
        if (std::getline(ss, token, '|')) g.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) g.permission = access::permission_from_string(token);
        g.name = std::to_string(g.access_user_id) + "-" + std::to_string(g.site_id);
        grants.push_back(std::move(g));
    }
    return grants;
}

void Storage::save_access_keys(const std::vector<access::AccessKey>& keys) {
    if (use_sqlite()) {
        sqlite_.save_access_keys(keys);
        return;
    }
}

std::vector<access::AccessKey> Storage::load_access_keys() {
    if (use_sqlite()) {
        return sqlite_.load_access_keys();
    }
    return {};
}

void Storage::save_reverse_proxies(const std::vector<proxy::ReverseProxy>& proxies) {
    if (use_sqlite()) {
        sqlite_.save_reverse_proxies(proxies);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(reverse_proxies_file());
    for (const auto& p : proxies) {
        file << p.id << "|" << p.domain << "|" << p.site_id << "|"
             << p.provider << "|" << p.config_path << "|" << p.upstream << "|"
             << (p.enabled ? "1" : "0") << "|" << p.status << "\n";
    }
}

std::vector<proxy::ReverseProxy> Storage::load_reverse_proxies() {
    if (use_sqlite()) {
        return sqlite_.load_reverse_proxies();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<proxy::ReverseProxy> proxies;
    std::ifstream file(reverse_proxies_file());
    if (!file.is_open()) {
        return proxies;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        proxy::ReverseProxy p;
        if (std::getline(ss, token, '|')) p.id = std::stoull(token);
        if (std::getline(ss, token, '|')) p.domain = token;
        if (std::getline(ss, token, '|')) p.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) p.provider = token;
        if (std::getline(ss, token, '|')) p.config_path = token;
        if (std::getline(ss, token, '|')) p.upstream = token;
        if (std::getline(ss, token, '|')) p.enabled = (token == "1");
        if (std::getline(ss, token, '|')) p.status = token;
        p.name = p.domain;
        proxies.push_back(std::move(p));
    }
    return proxies;
}

std::string Storage::profiles_file() const {
    return db_path_ + "profiles.db";
}

std::string Storage::template_profiles_file() const {
    return db_path_ + "template_profiles.db";
}

void Storage::save_profiles(const std::vector<profile::Profile>& profiles) {
    if (use_sqlite()) {
        sqlite_.save_profiles(profiles);
        return;
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return;
    }
    std::ofstream file(profiles_file());
    for (const auto& p : profiles) {
        file << p.id << "|" << p.profile_name << "|"
             << profile::profile_type_to_string(p.type) << "|"
             << p.web_server << "|" << p.runtime << "|"
             << p.template_path << "|" << p.description << "|"
             << (p.enabled ? "1" : "0") << "|" << (p.default_profile ? "1" : "0") << "\n";
    }
}

std::vector<profile::Profile> Storage::load_profiles() {
    if (use_sqlite()) {
        return sqlite_.load_profiles();
    }
    if (options_.core_backend == CoreStorageBackend::SqlitePhase5) {
        return {};
    }
    std::vector<profile::Profile> profiles;
    std::ifstream file(profiles_file());
    if (!file.is_open()) return profiles;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        profile::Profile p;
        if (std::getline(ss, token, '|')) p.id = std::stoull(token);
        if (std::getline(ss, token, '|')) p.profile_name = token;
        if (std::getline(ss, token, '|')) p.type = profile::profile_type_from_string(token);
        if (std::getline(ss, token, '|')) p.web_server = token;
        if (std::getline(ss, token, '|')) p.runtime = token;
        if (std::getline(ss, token, '|')) p.template_path = token;
        if (std::getline(ss, token, '|')) p.description = token;
        if (std::getline(ss, token, '|')) p.enabled = (token == "1");
        if (std::getline(ss, token, '|')) p.default_profile = (token == "1");
        p.name = p.profile_name;
        profiles.push_back(std::move(p));
    }
    return profiles;
}

std::vector<profile::Profile> Storage::migrate_template_profiles() {
    std::vector<profile::Profile> profiles;
    std::ifstream file(template_profiles_file());
    if (!file.is_open()) return profiles;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        profile::Profile p;
        p.type = profile::ProfileType::WEB_SERVER;
        if (std::getline(ss, token, '|')) p.id = std::stoull(token);
        if (std::getline(ss, token, '|')) p.profile_name = token;
        if (std::getline(ss, token, '|')) p.web_server = token;
        if (std::getline(ss, token, '|')) p.runtime = token;
        if (std::getline(ss, token, '|')) p.template_path = token;
        if (std::getline(ss, token, '|')) p.description = token;
        if (std::getline(ss, token, '|')) p.enabled = (token == "1");
        if (std::getline(ss, token, '|')) p.default_profile = (token == "1");
        p.name = p.profile_name;
        profiles.push_back(std::move(p));
    }
    return profiles;
}

bool Storage::begin_transaction() {
    return false;  // TXT backend does not support transactions
}

std::string Storage::sqlite_db_path() const {
    if (db_path_.empty()) return "containercp.db";
    if (db_path_.back() == '/') return db_path_ + "containercp.db";
    return db_path_ + "/containercp.db";
}

bool Storage::commit_transaction() {
    return false;  // TXT backend does not support transactions
}

bool Storage::rollback_transaction() {
    return false;  // TXT backend does not support transactions
}

bool Storage::backup(const std::string& dest_path) {
    (void)dest_path;
    return false;  // TXT backend does not support backup
}

// ---- Checked loads ----
CheckedSnapshot<node::Node> Storage::load_nodes_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_nodes(); }
CheckedSnapshot<php::PhpVersion> Storage::load_php_versions_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_php_versions(); }
CheckedSnapshot<profile::Profile> Storage::load_profiles_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_profiles(); }
CheckedSnapshot<user::User> Storage::load_users_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_users(); }
CheckedSnapshot<site::Site> Storage::load_sites_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_sites(); }
CheckedSnapshot<domain::Domain> Storage::load_domains_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_domains(); }
CheckedSnapshot<database::Database> Storage::load_databases_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_databases(); }
CheckedSnapshot<backup::Backup> Storage::load_backups_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_backups(); }
CheckedSnapshot<proxy::ReverseProxy> Storage::load_reverse_proxies_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_reverse_proxies(); }
CheckedSnapshot<access::AccessUser> Storage::load_access_users_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_access_users(); }
CheckedSnapshot<access::AccessGrant> Storage::load_access_grants_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_access_grants(); }
CheckedSnapshot<access::AccessKey> Storage::load_access_keys_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_access_keys(); }
CheckedSnapshot<auth::AuthUser> Storage::load_auth_users_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_auth_users(); }
CheckedSnapshot<ssl::SslCertificate> Storage::load_ssl_certificates_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_ssl_certificates(); }
CheckedSnapshot<mail::MailDomain> Storage::load_mail_domains_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_mail_domains(); }
CheckedSnapshot<mail::Mailbox> Storage::load_mailboxes_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_mail_mailboxes(); }
CheckedSnapshot<mail::MailAlias> Storage::load_mail_aliases_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_mail_aliases(); }

CheckedOptionalValue Storage::load_mail_module_state_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_mail_config_key("module_state"); }
CheckedOptionalValue Storage::load_mail_smarthost_checked() { SQLiteSnapshotReader snap(pool_); return snap.read_mail_config_key("smarthost"); }

} // namespace containercp::storage
